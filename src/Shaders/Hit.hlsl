#include "Common.hlsl"
#include "MicrofacetBRDFUtils.hlsl"

#define NumLights 3

struct ReflectionHitInfo
{
    float4 colorAndDistance;
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

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Ior;
    float Reflectivity;
    float3 Absorption;
    float Shininess;
    float pad;
    float pad1;
    float metallic;
    bool IsReflective;
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
Texture2D<float4> GBufferAlbedoMetal : register(t4);
Texture2D<float4> GBufferNormalRough : register(t5);
Texture2D<float4> GBufferDepth : register(t6);

SamplerState gLinearClampSampler : register(s0);

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
}

cbuffer PostProcess : register(b3)
{
    float Exposure;
    int ToneMapMode;
    int DebugMode;
    float pad;
}

cbuffer AreaLights : register(b4)
{
    AreaLight gAreaLight;
}

float3 LinearToSRGB(float3 x)
{
    const float a = 0.055f;
    float3 lo = 12.92f * x;
    float3 hi = (1.0f + a) * pow(x, 1.0f / 2.4f) - a;
    
    return select(lo, hi, x > 0.0031308f);
}

float3 ApplyExposure(float3 color, float exposure)
{
    return color * exp2(exposure);
}

float3 ToneMapReinhard(float3 x)
{
    return x / (1.0f + x);
}

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 ToneMapACES(float3 color)
{
    color = RRTAndODTFit(color);
    return saturate(color);

}

float3 PostProcess(float3 hdrColor)
{
    float3 color = ApplyExposure(hdrColor, Exposure);

    if (ToneMapMode == 1)
    {
        color = ToneMapReinhard(color);
    }
    else if (ToneMapMode == 2)
    {
        color = ToneMapACES(color);
    }
    else
    {
        color = saturate(color);
    }

    color = LinearToSRGB(color);

    return color;
}

// Van der Corput radical inverse in base 2
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return float(bits) * 2.3283064365386963e-10f; // 1 / 2^32
}

float2 Hammersley2D(uint i, uint N)
{
    float u = (float) i / (float) N;
    float v = RadicalInverse_VdC(i);
    return float2(u, v);
}

float2 SampleHammersley(uint sampleIdx, uint numSamples, uint pixelSeed, uint frameIndex)
{
    uint index = sampleIdx + pixelSeed * 1315423911u + frameIndex * 2654435761u;
    index %= numSamples;

    return Hammersley2D(index, numSamples);
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

float3 CosineSampleHemisphere(float2 xi)
{
    float r = sqrt(xi.x);
    float phi = 2.0f * 3.14159265f * xi.y;

    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(1.0f - xi.x);

    return float3(x, y, z);
}

float3x3 BuildTangentFrame(float3 N)
{
    float3 up = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    return float3x3(T, B, N);
}

float3 SampleGGXVNDF(float3 V, float2 xi, float roughness)
{
    // Convert view to hemisphere coords
    float3 Vh = normalize(float3(roughness * V.x, roughness * V.y, V.z));

    // Build orthonormal basis
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0.0f ? normalize(float3(-Vh.y, Vh.x, 0.0f)) : float3(1.0f, 0.0f, 0.0f);
    float3 T2 = cross(Vh, T1);

    // Sample point with polar coordinates
    float r = sqrt(xi.x);
    float phi = 2.0f * 3.14159265f * xi.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5f * (1.0f + Vh.z);
    t2 = (1.0f - s) * sqrt(1.0f - t1 * t1) + s * t2;

    // Compute sampled normal
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;

    // Transform back
    float3 N = normalize(float3(roughness * Nh.x, roughness * Nh.y, max(0.0f, Nh.z)));

    return N;
}

float GGX_G1(float3 N, float3 V, float alpha)
{
    float NdotV = saturate(dot(N, V));
    float a = alpha;
    float a2 = a * a;

    float lambda = NdotV == 0.0f ? 0.0f :
        (-1.0f + sqrt(1.0f + (a2 - 1.0f) * (1.0f - NdotV * NdotV) / (NdotV * NdotV))) * 0.5f;

    return 1.0f / (1.0f + lambda);
}

float GGX_PDF(float3 N, float3 V, float3 L, float roughness)
{
    float3 H = normalize(V + L);
    float NdotH = saturate(dot(N, H));
    float alpha = RoughnessToAlpha(roughness);
    float D = GGX_D(NdotH, alpha);
    float G1V = GGX_G1(N, V, alpha);

    float VdotH = saturate(dot(V, H));
    float LdotH = saturate(dot(L, H));

    // Convert from half-vector pdf to direction pdf
    float pdf = (D * G1V * VdotH) / (4.0f * LdotH);

    return pdf;
}

//float3 ComputeIndirectGI(float3 worldPos,
//                         float3 N,
//                         float3 V,
//                         Material mat,
//                         inout uint rng)
//{
//   // float2 xi = Random2D(rng);

//    float3 L;
//    float pdf;

//    uint pixelSeed = (DispatchRaysIndex().x * 73856093u) ^
//                 (DispatchRaysIndex().y * 19349663u);
        
//    float2 xi = SampleHammersley(4, 8, pixelSeed, 3);
    
//    // Mix diffuse and glossy sampling
    
//    float roughness = 1 - mat.Shininess;
    
//    float specWeight = saturate(mat.metallic);
//    if (xi.x < specWeight)
//    {
//        float3 H = SampleGGXVNDF(V, xi, roughness);
//        L = reflect(-V, H);
//        pdf = GGX_PDF(N, V, L, roughness);
//    }
//    else
//    {
//        float3 Ts = CosineSampleHemisphere(xi);
//        float3x3 frame = BuildTangentFrame(N);
//        L = normalize(mul(Ts, frame));
//        pdf = saturate(dot(N, L)) / 3.14159265f;
//    }
    
//    float3 H = normalize(V + L);

//    // Trace the indirect ray
//    HitInfo giHit;
//    giHit.isHit = false;
//    RayDesc giRay;
//    giRay.Origin = worldPos + N * 0.001f; // bias to avoid self-shadowing
//    giRay.Direction = L;
//    giRay.TMin = 0.01f;
//    giRay.TMax = 100000.0f;
    
//    TraceRay(SceneBVH,
//             RAY_FLAG_NONE,
//             0xFF,
//             0, 3, 0,
//             worldPos, 
//             L,
//             giRay,
//             giHit);

//    if (!giHit.isHit)
//        return 0.0f;

//    float3 Li = giHit.colorAndDistance.xyz; // returned shading
    
//    float NdotV = saturate(dot(N, V));
//    float NdotL = saturate(dot(N, L));
//    float LdotH = saturate(dot(L, H));
    
//    float3 f = DisneyDiffuse(NdotV, NdotL, LdotH, roughness);

    

//    if (NdotL <= 0.0f)
//        return 0.0f;

//    // Monte Carlo estimator
//    return (Li * f * NdotL) / pdf;
//}


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
void ClosestHit(inout HitInfo payload, Attributes attrib)
{


    payload.colorAndDistance = float4(0.0, 1.0, 1.0, RayTCurrent());
}

[shader("closesthit")]
void PlaneClosestHit(inout HitInfo payload, Attributes attrib)
{

    
    payload.colorAndDistance = float4(0.0, 1.0, 0.0, RayTCurrent());
    
}

[shader("closesthit")]
void ReflectionClosestHit(inout HitInfo payload, Attributes attrib)
{
      // Triangle index in this geometry
    const uint triIndex = PrimitiveIndex();
    const uint vbase = triIndex * 3;

    // Fetch the triangle’s vertices (object space)
    STriVertex v0 = BTriVertex[indices[vbase + 0]];

    STriVertex v1 = BTriVertex[indices[vbase + 1]];

    STriVertex v2 = BTriVertex[indices[vbase + 2]];

    // Full barycentric triple
    float3 bary = float3(1.0f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

    // Interpolate vertex normal in object space
    float3 nObj = normalize(v0.Normal * bary.x + v1.Normal * bary.y + v2.Normal * bary.z);
    float3 N = normalize(mul((float3x3) ObjectToWorld3x4(), nObj));
    float3 wo = -normalize(WorldRayDirection());
    
    // Hit position in world space (from the ray)
    float3 pW = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
  //  payload.isHit = true;

    float3 V = normalize(gEyePosW - pW.xyz);
    
    float3 areaLightContribution = float3(0, 0, 0);
    
    float3 Lo = 0.0;
    
    for (int i = 0; i < 1; ++i)
    {
        float3 L;
        float3 Li = 0.0;
        float NdotL = 0.0;
        
        if (!BuildLightSample(i, pW.xyz, N, L, Li, NdotL))
            continue;
        
        float3 H = normalize(V + L);
        float NdotV = saturate(dot(N, V));
        float NdotH = saturate(dot(N, H));
        float LdotH = saturate(dot(L, H));
        
        float3 Cd;
        float3 F0;
        ComputeDisneyMetalWorkflow(materials[materialIndex].DiffuseAlbedo.xyz, materials[materialIndex].Shininess, Cd, F0);
        
        float3 F = Fresnel_Schlick(F0, LdotH);
        float D = GGX_D(NdotH, 1 - materials[materialIndex].Shininess);
        float G = GGX_G_Smith(NdotV, NdotL, 1 - materials[materialIndex].Shininess);
        
        float denom = max(4.0 * NdotL * NdotV, 1e-4);
        float3 specBRDF = (D * G * F) / denom;
        
        float diffBRDF = DisneyDiffuse(NdotV, NdotL, LdotH, 1 - materials[materialIndex].Shininess);
        float3 diffTerm = Cd * diffBRDF;

        float3 f = diffTerm + specBRDF;

        Lo += f * Li * NdotL;
    }
    
    float3 radiance = 0.0;
    radiance += Lo;
    
    

    payload.colorAndDistance = float4(radiance, RayTCurrent());
}