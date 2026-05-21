#include "Common.hlsl"
#include "MicrofacetBRDFUtils.hlsl"
#include "PathTracerCommon.hlsl"
#include "BSDF.hlsl"

#define NumLights 1
#define MAX_AREA_LIGHTS 5

struct DirectionalLight
{
    float3 direction; // *towards* the surface
    float3 radiance; 
};

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
    float2 UV;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);
RaytracingAccelerationStructure SceneBVH : register(t2);
StructuredBuffer<Material> materials : register(t3);
StructuredBuffer<int> matIndices : register(t4);
Texture2D<float4> gAlbedoHistory : register(t5);

Texture2D textures[] : register(t6);

SamplerState sampAniso : register(s0);

cbuffer cbPass : register(b0)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4x4 gPrevViewProj;
    float3 gEyePosW;
    uint SPP;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float cbPerObjectPad2;
    float cbPerObjectPad3;
    float4 gSunDir;
    int directPresent;
    int SamplingMode;
    float BSDFSampleProbability;
    float LightSampleProbability;

    int MaxBounces;
    int FrameIndex;
    int UseNEE;
    int gUseRL;

    int UseQTable;
    float3 gSunColor;

    Light gLights[MaxLights];
};

cbuffer Colors : register(b1)
{
    float3 A[3];
    float3 B[3];
    float3 C[3];
}

uint GetDebugMaterialCount()
{
    return (uint) max(0.0f, cbPerObjectPad2);
}

uint GetDebugTextureCount()
{
    return (uint) max(0.0f, cbPerObjectPad3);
}

bool IsDebugValidationFrame()
{
    return gSunDir.w > 0.5f;
}

cbuffer PerInstance : register(b2)
{
    int materialIndex;
    int triMaterialOffset;
    float2 padding;
}


cbuffer AreaLights : register(b4)
{
    AreaLight gAreaLights[MAX_AREA_LIGHTS];
    uint gNumAreaLights;
    float3 gAreaLightPadding;
}

cbuffer FrameData : register(b5)
{
    uint frameIndex;
}

bool IsOccluded(float3 origin, float3 dir, float maxDistance)
{
    ShadowPayload spayload;
    spayload.isHit = true;
    
    RayDesc shadowRay;
    shadowRay.Origin = origin;
    shadowRay.Direction = dir;
    shadowRay.TMin = 0.1f;
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

float3 EvaluateDirectionalLightNEE(
    float3 p,
    float3 N,
    float3 Ng,
    float3 V,
    Material mat,
    DirectionalLight sun)
{
    float3 wi = normalize(-sun.direction); // surface -> light
    float NdotL = saturate(dot(Ng, wi));
    float NdotV = saturate(dot(Ng, V));

    if (NdotL <= 0.0f || NdotV <= 0.0f)
        return 0.0f.xxx;

    bool occluded = IsOccluded(p + Ng * 0.001f, wi, 100000.0f);
    if (occluded)
        return 0.0f.xxx;

    float3 f = EvaluateDisneyBRDF(mat, N, V, wi);
    return f * sun.radiance * NdotL;
}

// Handles a refractive material (glass) as a specular BSDF
void HandleRefractiveHit(
    Material mat,
    float3 pW,
    float3 N,
    float3 V,
    bool frontFace,
    inout PathPayload payload)
{
    // Assume we are in air outside
    float etaI = 1.0f;
    float etaT = mat.Ior;
    float eta = frontFace ? (etaI / etaT) : (etaT / etaI);

    float3 Nn = N; // shading normal already oriented to oppose V
    float cosI = saturate(dot(V, Nn));
    float sin2T = eta * eta * (1.0f - cosI * cosI);

    // Fresnel term from stored F0
    float3 F = Fresnel_Schlick(mat.FresnelR0, cosI);

    // Use luminance / max channel for branch probability
    float reflProb = max(F.r, max(F.g, F.b));
    reflProb = saturate(reflProb);
    reflProb = clamp(reflProb, 0.05f, 0.95f);

    float3 dir;
    float3 weight;

    // Total internal reflection when exiting the glass
    if (!frontFace && sin2T > 1.0f)
    {
        dir = reflect(-V, Nn);
        weight = 1.0f; // all energy reflected
        reflProb = 1.0f; // pdf = 1, only reflection
    }
    else
    {
        float r = Rand(payload.seed);

        if (r < reflProb)
        {
            // Reflection branch
            dir = reflect(-V, Nn);
            weight = F / max(reflProb, 1e-4f);
        }
        else
        {
            // Refraction branch
            float cosT = sqrt(max(0.0f, 1.0f - sin2T));
            float3 refrDir = eta * (-V) + (eta * cosI - cosT) * Nn;
            dir = normalize(refrDir);

            float3 oneMinusF = 1.0f - F;
            float transProb = max(1.0f - reflProb, 1e-4f);
            weight = oneMinusF / transProb;

            float thickness = 0.1f;
            float3 sigmaA = float3(0.02, 0.01, 0.01);
            weight *= exp(-sigmaA * thickness);

            // float eta2 = eta * eta;
            // weight *= eta2;
        }
    }

    // Tint the glass by base color
    weight *= mat.DiffuseAlbedo.rgb;
  
    payload.wi = dir;
    payload.bsdfOverPdf = weight; // f * cos / pdf collapsed into this scalar weight
    payload.pdf = 1.0f; // implicit delta BSDF
    payload.lastBounceWasDelta = 1;
    payload.prevBsdfPdf = 1.0f;
    payload.prevHitPos = pW;
    
    // Glass itself does not emit
    payload.emission = 0.0f;

    // If we somehow ended with zero weight, terminate
    if (all(weight <= 0.0f))
        payload.done = 1;
    else
        payload.done = 0;
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

struct LightSample
{
    float3 dir; // from hit point to light (normalized)
    float dist; // distance to light point
    float3 Li; // radiance from light along -dir
    float pdf; // pdf in solid angle
};

LightSample SampleAreaLight(uint lightIndex, float3 p, float3 n, inout uint seed)
{
    LightSample s = (LightSample) 0;

    AreaLight light = gAreaLights[lightIndex];

    float2 xi = Rand2(seed);

    float3 pL = light.Position +
                (xi.x - 0.5f) * light.U +
                (xi.y - 0.5f) * light.V;

    float3 L = pL - p;
    float d = length(L);
    if (d <= 0.0f)
        return s;

    L /= d;

    float3 nL = normalize(cross(light.U, light.V));
    float cosOnLight = dot(nL, -L);
    if (cosOnLight <= 0.0f)
        return s;

    float pdfArea = 1.0f / max(light.Area, 1e-4f);

    float pdf = pdfArea * (d * d) / max(cosOnLight, 1e-4f);

    pdf *= (1.0f / gNumAreaLights);

    s.dir = L;
    s.dist = d;
    s.Li = light.Radiance;
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
    uint prevWasDelta = payload.lastBounceWasDelta;
    float prevSegmentPdf = payload.prevBsdfPdf;
    float3 prevSegmentOrigin = payload.prevHitPos;
    
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

    float3 nObjInterp =
        v0.Normal * bary.x +
        v1.Normal * bary.y +
        v2.Normal * bary.z;

    float3 nObj = float3(0.0f, 1.0f, 0.0f);
    if (dot(nObjInterp, nObjInterp) > 1e-10f && all(isfinite(nObjInterp)))
    {
        nObj = normalize(nObjInterp);
    }
    else
    {
        float3 eObj1 = v1.Vertex - v0.Vertex;
        float3 eObj2 = v2.Vertex - v0.Vertex;
        float3 nObjFace = cross(eObj1, eObj2);
        if (dot(nObjFace, nObjFace) > 1e-10f && all(isfinite(nObjFace)))
            nObj = normalize(nObjFace);
    }

    float2 uv =
        v0.UV * bary.x +
        v1.UV * bary.y +
        v2.UV * bary.z;
    
    // World-space position and normal
    float3 pW = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    float3 Ngeom = TransformNormalToWorld(nObj);
    float3 V = -WorldRayDirection();
    
    bool frontFace = dot(Ngeom, V) > 0.0f;
    float3 Ng = frontFace ? Ngeom : -Ngeom;
    float3 N = Ng;

    // Fill payload base data
    payload.hitPos = pW;
    payload.normal = N;
    payload.depth++;
    payload.hitSomething = 1;
    payload.tHit = RayTCurrent();

    float3 selfEmit = 0.0f;
    
    uint materialCount = GetDebugMaterialCount();
    uint textureCount = GetDebugTextureCount();

    int matIdx = matIndices[triIndex];
    bool invalidMatIndex = (matIdx < 0) || (materialCount > 0 && (uint) matIdx >= materialCount);

    if (invalidMatIndex)
    {
        payload.hitPos = pW;
        payload.normal = Ng;
        payload.emission = IsDebugValidationFrame() ? float3(1.0f, 0.0f, 1.0f) : 0.0f;
        payload.bsdfOverPdf = 0.0f;
        payload.pdf = 1.0f;
        payload.prevHitPos = pW;
        payload.lastBounceWasDelta = 1;
        payload.prevBsdfPdf = 1.0f;
        payload.done = 1;
        payload.isEmissive = 1;
        return;
    }

    Material mat = materials[matIdx];

    bool invalidTexIndex = false;
    if (textureCount > 0)
    {
        invalidTexIndex =
            (mat.TexIndex >= 0 && (uint) mat.TexIndex >= textureCount) ||
            (mat.NormalIndex >= 0 && (uint) mat.NormalIndex >= textureCount) ||
            (mat.SpecularIndex >= 0 && (uint) mat.SpecularIndex >= textureCount) ||
            (mat.AlphaIndex >= 0 && (uint) mat.AlphaIndex >= textureCount);
    }

    if (invalidTexIndex && IsDebugValidationFrame())
    {
        payload.hitPos = pW;
        payload.normal = Ng;
        payload.emission = float3(1.0f, 1.0f, 0.0f);
        payload.bsdfOverPdf = 0.0f;
        payload.pdf = 1.0f;
        payload.prevHitPos = pW;
        payload.lastBounceWasDelta = 1;
        payload.prevBsdfPdf = 1.0f;
        payload.done = 1;
        payload.isEmissive = 1;
        return;
    }
    
    payload.emission = 0.0f;
    payload.isEmissive = 0;

    if (mat.TexIndex >= 0)
    {
        mat.DiffuseAlbedo = textures[mat.TexIndex].SampleLevel(sampAniso, uv, 0);
    }
    
    if (mat.NormalIndex >= 0)
    {
        float3 Nbase = normalize(N);

        float4 nSample = textures[mat.NormalIndex].SampleLevel(sampAniso, uv, 4.0f);

        float3x3 objToWorld = (float3x3) ObjectToWorld3x4();

        float3 e1 = mul(v1.Vertex - v0.Vertex, objToWorld);
        float3 e2 = mul(v2.Vertex - v0.Vertex, objToWorld);

        float2 duv1 = v1.UV - v0.UV;
        float2 duv2 = v2.UV - v0.UV;

        float det = duv1.x * duv2.y - duv1.y * duv2.x;

        if (abs(det) > 1e-6f)
        {
            float invDet = rcp(det);
            float3 T = (duv2.y * e1 - duv1.y * e2) * invDet;

            if (dot(T, T) > 1e-8f && all(isfinite(T)))
            {
                T = normalize(T - Nbase * dot(Nbase, T));

                float handedness = (det < 0.0f) ? -1.0f : 1.0f;
                float3 B = normalize(cross(Nbase, T)) * handedness;

                if (dot(B, B) > 1e-8f && all(isfinite(B)))
                {
                    float3x3 TBN = transpose(float3x3(T, B, Nbase));
                    
                    float3 nTexRGB = nSample.xyz * 2.0f - 1.0f;
                    nTexRGB.y = -nTexRGB.y;
                    float2 nXY_AG = float2(nSample.a, nSample.g) * 2.0f - 1.0f;
                    float3 nTexAG = float3(nXY_AG, sqrt(saturate(1.0f - dot(nXY_AG, nXY_AG))));
                    float3 bestN = Nbase;
                    float bestDot = 0.05f;

                    if (dot(nTexRGB, nTexRGB) > 1e-8f && all(isfinite(nTexRGB)))
                    {
                        float3 NmRGB = normalize(mul(TBN, nTexRGB));
                        float dRGB = dot(NmRGB, Nbase);
                        if (all(isfinite(NmRGB)) && dRGB > bestDot)
                        {
                            bestDot = dRGB;
                            bestN = NmRGB;
                        }
                    }

                    if (dot(nTexAG, nTexAG) > 1e-8f && all(isfinite(nTexAG)))
                    {
                        float3 NmAG = normalize(mul(TBN, normalize(nTexAG)));
                        float dAG = dot(NmAG, Nbase);
                        if (all(isfinite(NmAG)) && dAG > bestDot)
                        {
                            bestDot = dAG;
                            bestN = NmAG;
                        }
                    }

                    N = bestN;
                }
                else
                {
                    N = Nbase;
                }
            }
            else
            {
                N = Nbase;
            }
        }
        else
        {
            N = Nbase;
        }
    }
    
    float NoV = saturate(dot(Ng, V));
    float normalBlend = saturate((0.25f - NoV) / 0.25f);

// At grazing angles, fade normal map back to geometric normal.
    N = normalize(lerp(N, Ng, normalBlend));
    
    if (mat.SpecularIndex >= 0)
    {
        float gloss = textures[mat.SpecularIndex].SampleLevel(sampAniso, uv, 0).r;
        mat.Roughness = 1.0f - gloss;
    }
    
    if (mat.AlphaIndex >= 0)
    {
        float alpha = textures[mat.AlphaIndex].SampleLevel(sampAniso, uv, 0).a;
        if (alpha < 0.5f)
        {
            float3 rayDir = normalize(WorldRayDirection());
            payload.wi = rayDir;
            payload.bsdfOverPdf = 1.0f;
            payload.pdf = 1.0f;
            payload.prevHitPos = pW + rayDir * 0.01f;
            payload.hitPos = payload.prevHitPos;
            payload.normal = rayDir;
            payload.lastBounceWasDelta = 1;
            payload.prevBsdfPdf = 1.0f;
            payload.emission = 0.0f;
            payload.done = 0;
            return;
        }
    }
    
    // Refractive materials (glass) – handle with dedicated BSDF
    if (mat.IsRefractive != 0)
    {
        HandleRefractiveHit(mat, pW, N, V, frontFace, payload);
        return;
    }
    
    // Sample BSDF
    float2 xi = Rand2(payload.seed);

    float3x3 frame = BuildTangentFrame(N);
    float3 VLocal = mul(V, transpose(frame));
    
    int lightIndex = min((uint) (Rand(payload.seed) * gNumAreaLights), gNumAreaLights - 1);

    bool isNEELight = (mat.LightIndex >= 0);
    bool sameLight = (mat.LightIndex == lightIndex);
    
    bool isEmitter = any(mat.EmissiveColor.rgb > 0.0f);

    if (isEmitter)
    {
        payload.isEmissive = 1;
        float3 Le = mat.EmissiveColor.rgb;

        if (!mat.isNEELight || !sameLight || prevWasDelta != 0 || payload.depth == 1)
        {
        // No MIS against NEE for primary hits or after delta events
            selfEmit = Le;
        }
        else
        {
        // Compute light pdf for having sampled this exact point via NEE
            float3 toLight = pW - prevSegmentOrigin;
            float dist2 = dot(toLight, toLight);
            float dist = sqrt(max(dist2, 1e-8f));
            float3 wiToLight = toLight / dist;

            float3 nLight = N; // for a flat emissive area light this is fine
            float cosOnLight = saturate(dot(nLight, -wiToLight));

            float pdfLight = 0.0f;
            if (cosOnLight > 0.0f)
            {
                float pdfArea = 1.0f / max(gAreaLights[lightIndex].Area, 1e-8f);

                pdfLight = pdfArea * dist2 / max(cosOnLight, 1e-8f);

                pdfLight *= (1.0f / gNumAreaLights);
            }

            float pdfBSDF = max(prevSegmentPdf, 0.0f);

            float wBsdf = (pdfBSDF * pdfBSDF) /
                      max(pdfBSDF * pdfBSDF + pdfLight * pdfLight, 1e-8f);

            selfEmit = Le * wBsdf;
        }
    }
    
    float3 LdContrib = 0.0f;
    
    DirectionalLight sun;
    sun.direction = normalize(float3(gSunDir.rgb));
    sun.radiance = gSunColor;

    float3 L = normalize(-sun.direction);

    float3 lighting = EvaluateDirectionalLightNEE(
    pW,
    N,
    Ng,
    V,
    mat,
    sun);
        
    LightSample lightSample = SampleAreaLight(lightIndex, pW, N, payload.seed);
        
    if (lightSample.pdf > 0.0f)
    {
        bool occluded = IsOccluded(pW + Ng * 0.01f, lightSample.dir, lightSample.dist - 1e-4f);
        
        
        if (!occluded)
        {
            float3 L = lightSample.dir;
            
            float NdotL = saturate(dot(Ng, lightSample.dir));
            
            if (NdotL > 0.0f)
            {
                float3 f = EvaluateDisneyBRDF(mat, Ng, V, L);
                float pdfBSDF = PdfDisneyBRDF(mat, Ng, V, L);
                pdfBSDF = max(pdfBSDF, 0.0f);
                
 
                // Multiple importance sampling weight (power heuristic)
                float pdfLight = lightSample.pdf;
                float pdfL2 = pdfLight * pdfLight;
                float pdfBSDF2 = pdfBSDF * pdfBSDF;
                
                float wLight = pdfL2 / max(pdfL2 + pdfBSDF2, 1e-8f);
                
                LdContrib = wLight * f * lightSample.Li * NdotL / max(pdfLight, 1e-4f);
                
            }
        }
    }
     
    float3 direct = LdContrib + lighting;
    
    BSDFSample bsdf = SampleDisneyGGX(mat, Ng, V, VLocal, xi, frame);
    
    if (!bsdf.valid || all(bsdf.fOverPdf == 0.0f) || bsdf.pdf <= 0.0f)
    {
        payload.emission = selfEmit + direct;
        payload.done = 1;
        payload.bsdfOverPdf = 0.0f;
        return;
    }

    payload.wi = bsdf.wi;
    payload.bsdfOverPdf = bsdf.fOverPdf;
    payload.pdf = bsdf.pdf;
        
    payload.emission = selfEmit + direct;
    payload.prevHitPos = pW;
    payload.lastBounceWasDelta = (bsdf.delta == 1) ? 1 : 0;
    payload.prevBsdfPdf = bsdf.pdf;
    payload.done = 0;
}