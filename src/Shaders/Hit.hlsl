#include "Common.hlsl"
#include "MicrofacetBRDFUtils.hlsl"
#include "PathTracerCommon.hlsl"
#include "BSDF.hlsl"

#define NumLights 1

struct ShadowHitInfo
{
    bool isHit;
    uint depth;
};

struct ReflectionHitInfo
{
    float4 radiance;
};

struct AreaLight
{
    float3 Position;
    float Pad;
    float3 U;
    float Pad2;
    float3 V;
    float Pad3;
    float3 Radiance;
    float Area;
};


struct STriVertex
{
    float3 Vertex;
    float3 Normal;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);
RaytracingAccelerationStructure SceneBVH : register(t2);
StructuredBuffer<Material> materials : register(t3);


cbuffer cbPass : register(b0)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float cbPerObjectPad2;
    float cbPerObjectPad3;
    float4 gAmbientLight;
    
    Light gLights[MaxLights];
};

cbuffer Colors : register(b1)
{
    float3 A[3];
    float3 B[3];
    float3 C[3];
}

cbuffer PerInstance : register(b2)
{
    int materialIndex;
    float3 padding;
}


cbuffer AreaLights : register(b4)
{
    AreaLight gAreaLight;
}

cbuffer FrameData : register(b5)
{
    uint frameIndex;
}

float3 SampleAreaLight(uint index, float2 xi)
{
    AreaLight L = gAreaLight;
    
    float3 p = L.Position + (2.0f * xi.x - 1.0f) * L.U + (2.0f * xi.y - 1.0f) * L.V;
    return p;

}

bool BuildLightSample(
    int lightIndex,
    float3 P,
    float3 N,
    out float3 L,
    out float3 Li,
    out float NdotL)
{
    Light light = gLights[lightIndex];

    L = 0;
    Li = 0;
    NdotL = 0;

    bool isDirectional = true;

    if (isDirectional)
    {
        float3 D = normalize(light.Direction);
        L = -D;

        NdotL = saturate(dot(N, L));
        if (NdotL <= 0)
            return false;

        Li = light.Strength;
        return true;
    }

    // Point or spot light:
    float3 toLight = light.Position - P;
    float distSq = max(dot(toLight, toLight), 1e-6);
    float dist = sqrt(distSq);

    L = toLight / dist;
    NdotL = saturate(dot(N, L));
    if (NdotL <= 0)
        return false;

    // Inverse-square attenuation
    float invSq = 1.0 / distSq;

    float rangeAtt = 1.0;
    if (light.FalloffEnd > light.FalloffStart)
    {
        rangeAtt = saturate((light.FalloffEnd - dist) /
                            (light.FalloffEnd - light.FalloffStart));
    }

    Li = light.Strength * invSq * rangeAtt;

    if (light.SpotPower > 0)
    {
        float3 spotDir = normalize(-light.Direction);
        float cosAngle = saturate(dot(L, spotDir));
        float spotFactor = pow(cosAngle, light.SpotPower);
        if (spotFactor <= 0)
            return false;
        Li *= spotFactor;
    }

    return true;
}

bool IsOccluded(float3 origin, float3 dir, float maxDistance)
{
    ShadowPayload spayload;
    spayload.isHit = true;
    
    RayDesc shadowRay;
    shadowRay.Origin = origin;
    shadowRay.Direction = dir;
    shadowRay.TMin = 0.01f;
    shadowRay.TMax = maxDistance - 0.001f;
    
    TraceRay(
    SceneBVH,
    RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
    RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_OPAQUE,
    0xFF,
    1,
    2,
    1,
    shadowRay,
    spayload);

    return spayload.isHit;
}
struct LightSample
{
    float3 dir; // from hit point to light (normalized)
    float dist; // distance to light point
    float3 Li; // radiance from light along -dir
    float pdf; // pdf in solid angle
};

LightSample SampleAreaLight(float3 p, float3 n, inout uint seed)
{
    LightSample s;
    s.dir = 0;
    s.dist = 0;
    s.Li = 0;
    s.pdf = 0;

    // Sample a point on the rect with [0,1]^2
    float2 xi = Rand2(seed);

    float3 pL = gAreaLight.Position +
                (xi.x - 0.5f) * gAreaLight.U +
                (xi.y - 0.5f) * gAreaLight.V;

    float3 L = pL - p;
    float d = length(L);
    if (d <= 0.0f)
        return s;

    L /= d;

    // Light normal (assuming U,V define the rect plane)
    float3 nL = normalize(cross(gAreaLight.U, gAreaLight.V));

    float cosOnLight = dot(nL, -L);
    if (cosOnLight <= 0.0f)
        return s; // back side

    // Area pdf -> solid angle pdf
    float pdfArea = 1.0f / max(gAreaLight.Area, 1e-4f);
    float pdf = pdfArea * (d * d) / max(cosOnLight, 1e-4f);

    s.dir = L;
    s.dist = d;
    s.Li = gAreaLight.Radiance;
    s.pdf = pdf;

    return s;
}

[shader("closesthit")]
void ShadowClosestHit(inout ShadowPayload hit, Attributes attrib)
{
    hit.isHit = true;
}

[shader("closesthit")]
void ClosestHit(inout PathPayload payload, Attributes attrib)
{
    // Triangle index and vertices
    const uint triIndex = PrimitiveIndex();
    const uint vbase = triIndex * 3;

    STriVertex v0 = BTriVertex[indices[vbase + 0]];
    STriVertex v1 = BTriVertex[indices[vbase + 1]];
    STriVertex v2 = BTriVertex[indices[vbase + 2]];

    // Barycentrics (attrib.bary.x, attrib.bary.y)
    float3 bary = float3(
        1.0f - attrib.bary.x - attrib.bary.y,
        attrib.bary.x,
        attrib.bary.y
    );

    float3 nObj = normalize(
        v0.Normal * bary.x +
        v1.Normal * bary.y +
        v2.Normal * bary.z
    );

    // World-space position and normal
    float3 pW = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    float3 N = TransformNormalToWorld(nObj);
    float3 V = -WorldRayDirection();
    
    if (dot(N, V) < 0.0f)
    {
        N = -N;
    }

    // Fill payload base data
    payload.hitPos = pW;
    payload.normal = N;
    payload.depth++;

    Material mat = materials[materialIndex];
    
    payload.emission = 0.0f;
    
    // Sample BSDF
    float2 xi = Rand2(payload.seed);

    float3 wi;
    float pdf;
    
    float3x3 frame = BuildTangentFrame(N);
    float3 VLocal = mul(V, transpose(frame));
    
    float3 LdDir = 0.0f;
    float3 LdContrib = 0.0f;
    
    LightSample lightSample = SampleAreaLight(pW, N, payload.seed);
    
    if (lightSample.pdf > 0.0f)
    {
        bool occluded = IsOccluded(pW + N * 0.1f, lightSample.dir, lightSample.dist - 1e-4f);
        //if (occluded)
        //{
        //    payload.emission = 0.0f;
        //    payload.done = 1;
        //    return;
        //}
        if (!occluded)
        {
            float3 L = lightSample.dir;
            
            float NdotL = saturate(dot(N, L));
            
            if (NdotL > 0.0f)
            {
                float3 f = EvaluateDisneyBRDF(mat, N, V, L);
                float pdfBSDF = PdfDisneyBRDF(mat, N, V, L);
                
                // Multiple importance sampling weight (balance heuristic)
                float pdfLight = lightSample.pdf;
                float pdfL2 = pdfLight * pdfLight;
                float pdfBSDF2 = pdfBSDF * pdfBSDF;
                float wLight = pdfL2 / max(pdfL2 + pdfBSDF2, 1e-4f);
                
               LdContrib = wLight * f * lightSample.Li * NdotL / max(pdfLight, 1e-4f);
            }
        }
    }
      
    BSDFSample bsdf = SampleDisneyGGX(mat, N, V, VLocal, xi, frame);
    
    if (!bsdf.valid || all(bsdf.fOverPdf == 0.0f))
    {
        payload.done = 1;
        return;
    }

    payload.wi = bsdf.wi;
    payload.bsdfOverPdf = bsdf.fOverPdf;
    payload.pdf = bsdf.pdf;
    
    float3 selfEmit = 0.0f;
    payload.emission = selfEmit + LdContrib;
    
    // Stop if pdf is invalid or if throughput will be zero
    if (all(bsdf.fOverPdf == 0.0f) || bsdf.pdf <= 0.0f)
    {
        payload.done = 1;
    }
    else
    {
        payload.done = 0;
    }
}