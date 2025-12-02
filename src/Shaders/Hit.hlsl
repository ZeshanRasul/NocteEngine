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
    AreaLight gAreaLights[8];
    uint gAreaLightCount;
}

cbuffer FrameData : register(b5)
{
    uint frameIndex;
}

float3 SampleAreaLight(uint index, float2 xi)
{
    AreaLight L = gAreaLights[index];
    
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

float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));
    
    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);
    
    return reflectPercent;
}

float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    const float m = mat.Shininess * 256.0f;
    float3 halfVec = normalize(toEye + lightVec);
    
    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec);
    
    float3 specularAlbedo = fresnelFactor * roughnessFactor;
    
    specularAlbedo = specularAlbedo / (specularAlbedo + 1.0f);
    
    return (mat.DiffuseAlbedo.rgb + specularAlbedo) * lightStrength;
}

float3 ComputeDirectionalLight(Light L, float3 normal, float3 toEye, Material mat)
{
    float3 lightVec = -L.Direction;
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
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

//    float3 Cd, F0;
//    ComputeDisneyMetalWorkflow(mat.DiffuseAlbedo.xyz, mat.metallic, Cd, F0);

//// DEBUG: show baseColor and stop
//    payload.emission = float3((materialIndex + 1) / 5.0, 0, 0);
//    payload.done = 1;
//    return;
//    payload.bsdfOverPdf = 0;
//    return;

    
//    float emissiveIntensity = mat.DiffuseAlbedo.w;
//    payload.emission = emissiveIntensity * mat.DiffuseAlbedo.xyz;

    payload.emission = 0.0f;
    
    
    // Sample BSDF
    float2 xi = Rand2(payload.seed);

    float3 wi;
    float pdf;
    
    float3x3 frame = BuildTangentFrame(N);
    float3 VLocal = mul(V, transpose(frame));
    
    BSDFSample bsdf = SampleDisneyGGX(mat, N, V, VLocal, xi, frame);
    
    if (!bsdf.valid || all(bsdf.fOverPdf == 0.0f))
    {
        payload.done = 1;
        return;
    }

    payload.wi = wi;
    payload.bsdfOverPdf = bsdf.fOverPdf;
    payload.pdf = pdf;
    
    
    // Stop if pdf is invalid or throughput will be zero
    if (all(bsdf.fOverPdf == 0.0f) || bsdf.pdf <= 0.0f)
    {
        payload.done = 1;
    }
    else
    {
        payload.done = 0;
    }
}