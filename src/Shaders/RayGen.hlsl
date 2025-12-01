#include "Common.hlsl"
#include "MicrofacetBRDFUtils.hlsl"

#define MaxLights 16
#define NumLights 3

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

// Raytracing output texture, accessed as a UAV
RWTexture2D<float4> gOutput : register(u0);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);
StructuredBuffer<Material> materials : register(t1);
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
    
    Light gLights[NumLights];
};

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

float2 SampleHammersley(uint sampleIdx, uint numSamples)
{
    uint index = sampleIdx + 1315423911u + 2654435761u;
    index %= numSamples;

    return Hammersley2D(index, numSamples);
}

float3 SampleAreaLight(uint index, float2 xi)
{
    AreaLight L = gAreaLight;
    
    float3 p = L.Position + (2.0f * xi.x - 1.0f) * L.U + (2.0f * xi.y - 1.0f) * L.V;
    return p;

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

    // Directional only for now
    float3 D = normalize(light.Direction);
    L = -D;

    NdotL = saturate(dot(N, L));
    if (NdotL <= 0)
        return false;

    Li = light.Strength;
    return true;
}

float3 SampleGGXVNDF(float3 V, float2 xi, float roughness)
{
    float3 Vh = normalize(float3(roughness * V.x, roughness * V.y, V.z));

    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0.0f ? normalize(float3(-Vh.y, Vh.x, 0.0f)) : float3(1.0f, 0.0f, 0.0f);
    float3 T2 = cross(Vh, T1);

    float r = sqrt(xi.x);
    float phi = 2.0f * 3.14159265f * xi.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5f * (1.0f + Vh.z);
    t2 = (1.0f - s) * sqrt(1.0f - t1 * t1) + s * t2;

    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;

    float3 N = normalize(float3(roughness * Nh.x, roughness * Nh.y, max(0.0f, Nh.z)));
    return N;
}

float GGX_G1(float3 N, float3 V, float alpha)
{
    float NdotV = saturate(dot(N, V));
    float a = alpha;
    float a2 = a * a;

    float lambda = (NdotV == 0.0f) ? 0.0f :
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

    float pdf = (D * G1V * VdotH) / (4.0f * LdotH);
    return pdf;
}

// Octahedral normal decode matching your encoder
float3 DecodeNormalOct(float2 e)
{
    e = e * 2.0f - 1.0f;

    float3 n = float3(e.x, e.y, 1.0f - abs(e.x) - abs(e.y));
    if (n.z < 0.0f)
    {
        float oldX = n.x;
        float oldY = n.y;

        n.x = (1.0f - abs(oldY)) * (oldX >= 0.0f ? 1.0f : -1.0f);
        n.y = (1.0f - abs(oldX)) * (oldY >= 0.0f ? 1.0f : -1.0f);
    }

    return normalize(n);
}

// ----------------------------------------------------------
// RAY GENERATION
// ----------------------------------------------------------
[shader("raygeneration")]
void RayGen()
{
    HitInfo payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);
    payload.depth = 0;
    payload.eta = 1.0f;

    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;

    float2 pixelCenter = (float2(launchIndex) + 0.5f) / float2(dims);

    // ------------------------------------------------------------------
    // G-BUFFER FETCH
    // ------------------------------------------------------------------
    float4 g0 = GBufferAlbedoMetal.SampleLevel(gLinearClampSampler, pixelCenter, 0);
    float4 g1 = GBufferNormalRough.SampleLevel(gLinearClampSampler, pixelCenter, 0);
    float depth = GBufferDepth.SampleLevel(gLinearClampSampler, pixelCenter, 0).x;

    // If we hit the sky, early out
    if (depth >= 1.0f - 1e-5f)
    {
        float3 skyColor = float3(0.1f, 0.3f, 0.7f);
        gOutput[launchIndex] = float4(PostProcess(skyColor), 1.0f);
        return;
    }

    float3 albedo = g0.rgb;
    uint materialID = (uint) round(saturate(g0.a) * 255.0f);

    float3 Ng = DecodeNormalOct(g1.rg); // geometric normal
    float roughness = saturate(g1.b);
    float metallic = saturate(g1.a);

    roughness = max(roughness, 0.02f);

    // ------------------------------------------------------------------
    // RECONSTRUCT WORLD POSITION
    // ------------------------------------------------------------------
    float2 ndc;
    ndc.x = pixelCenter.x * 2.0f - 1.0f;
    ndc.y = 1.0f - pixelCenter.y * 2.0f;

    float4 clipPos = float4(ndc.x, ndc.y, depth, 1.0f);
    float4 viewPosH = mul(clipPos, gInvProj);
    viewPosH /= viewPosH.w;
    float4 worldPosH = mul(viewPosH, gInvView);
    float3 worldPos = worldPosH.xyz / worldPosH.w;

    float3 V = normalize(gEyePosW - worldPos);

    // Separate shading normal (Ns) and geometric normal (Ng)
    float3 Ns = Ng;
    if (dot(Ns, V) < 0.0f)
        Ns = -Ns;

    // ------------------------------------------------------------------
    // MATERIAL AND F0
    // ------------------------------------------------------------------
    Material mat = materials[materialID];
    float3 Cd;
    float3 F0;
    ComputeDisneyMetalWorkflow(albedo, metallic, Cd, F0);

    // ------------------------------------------------------------------
    // DIRECT LIGHTING (directional)
    // ------------------------------------------------------------------
    float3 Lo = 0.0f;

    [unroll]
    for (int i = 0; i < NumLights; ++i)
    {
        float3 L;
        float3 Li;
        float NdotL;
        if (!BuildLightSample(i, worldPos, Ns, L, Li, NdotL))
            continue;

        float3 H = normalize(V + L);
        float NdotV = saturate(dot(Ns, V));
        float NdotH = saturate(dot(Ns, H));
        float LdotH = saturate(dot(L, H));

        if (NdotL <= 0.0f || NdotV <= 0.0f)
            continue;

        float3 F = Fresnel_Schlick(F0, LdotH);
        float D = GGX_D(NdotH, RoughnessToAlpha(roughness));
        float G = GGX_G_Smith(NdotV, NdotL, roughness);

        float denom = max(4.0f * NdotL * NdotV, 1e-4f);
        float3 specBRDF = (D * G * F) / denom;

        float diffBRDF = DisneyDiffuse(NdotV, NdotL, LdotH, roughness);
        float3 diffTerm = Cd * diffBRDF;

        float3 f = diffTerm + specBRDF;

        Lo += f * Li * NdotL;
    }

    // ------------------------------------------------------------------
    // AREA LIGHT WITH SOFT SHADOWS
    // ------------------------------------------------------------------
    float3 areaLightContribution = 0.0f;
    const uint shadowSamples = 8;

    for (uint s = 0; s < shadowSamples; ++s)
    {
        float2 xi = SampleHammersley(s, shadowSamples);

        float3 lightPoint = SampleAreaLight(0, xi);
        float3 toLight = lightPoint - worldPos;
        float dist = length(toLight);
        float3 dir = toLight / dist;

        RayDesc shadowRay;
        shadowRay.Origin = worldPos + Ns * 0.001f;
        shadowRay.Direction = dir;
        shadowRay.TMin = 0.01f;
        shadowRay.TMax = dist - 0.01f;

        ShadowHitInfo shadowPayload;
        shadowPayload.isHit = false;
        shadowPayload.depth = 0;

        TraceRay(
            SceneBVH,
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
            RAY_FLAG_SKIP_CLOSEST_HIT_SHADER |
            RAY_FLAG_FORCE_OPAQUE,
            0xFF,
            1, 3, 1,
            shadowRay,
            shadowPayload
        );

        if (!shadowPayload.isHit)
        {
            float NdotL = saturate(dot(Ns, dir));
            if (NdotL > 0.0f)
            {
                float3 lightNormal = normalize(cross(gAreaLight.U, gAreaLight.V));
                float LnDotL = saturate(dot(-dir, lightNormal));
                float dist2 = dist * dist;

                float pdf = 1.0f / gAreaLight.Area;

                float3 Li = gAreaLight.Radiance * (LnDotL / dist2);

                areaLightContribution += Li * NdotL / pdf;
            }
        }
    }

    areaLightContribution /= shadowSamples;

    // ------------------------------------------------------------------
    // REFLECTION (one bounce)
    // ------------------------------------------------------------------
    float3 reflectionColor = 0.0f;

    {
        float2 xi = SampleHammersley(0, 1);
        float3 Nm = SampleGGXVNDF(V, xi, roughness);
        float3 R = reflect(-V, Nm);

        HitInfo reflectionPayload;
        reflectionPayload.colorAndDistance = float4(0, 0, 0, 0);
        reflectionPayload.depth = 1;
        reflectionPayload.eta = mat.Ior;

        RayDesc reflectionRay;
        reflectionRay.Origin = worldPos + Ns * 0.001f;
        reflectionRay.Direction = R;
        reflectionRay.TMin = 0.01f;
        reflectionRay.TMax = 1e38f;

        TraceRay(
            SceneBVH,
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
            0xFF,
            2, 3, 0,
            reflectionRay,
            reflectionPayload
        );

        reflectionColor = reflectionPayload.colorAndDistance.xyz;
    }

    // ------------------------------------------------------------------
    // REFRACTION (one bounce, air -> material)
    // ------------------------------------------------------------------
    float3 refractionColor = 0.0f;

    if (mat.Ior > 1.01f && metallic < 0.99f)
    {
        float ior = max(mat.Ior, 1.0f);
        float eta = 1.0f / ior; // air -> material

        float3 I = -V;

        float cosI = dot(I, Ng);
        float sin2T = eta * eta * (1.0f - cosI * cosI);
        bool tir = (sin2T > 1.0f);

        if (!tir)
        {
            float3 refractDir = refract(I, -Ng, eta);
            refractDir = normalize(refractDir);

            HitInfo refrPayload;
            refrPayload.colorAndDistance = float4(0, 0, 0, 0);
            refrPayload.depth = 1;
            refrPayload.eta = ior;

            RayDesc refractionRay;
            refractionRay.Origin = worldPos + Ng * 0.001f;
            refractionRay.Direction = refractDir;
            refractionRay.TMin = 0.01f;
            refractionRay.TMax = 1e38f;

            TraceRay(
                SceneBVH,
                RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
                0xFF,
                2, 3, 0,
                refractionRay,
                refrPayload
            );

            refractionColor = refrPayload.colorAndDistance.xyz;
        }
        else
        {
            // If we ever hit TIR, all energy goes to reflection.
            // We handle this in the Fresnel blend below.
        }
    }

    // ------------------------------------------------------------------
    // FRESNEL BLEND reflection vs refraction
    // ------------------------------------------------------------------
    float3 F;
    {
        float cosThetaI = saturate(dot(V, Ns)); // view vs shading normal
        F = F0 + (1.0f - F0) * pow(1.0f - cosThetaI, 5.0f);
    }

    // If there is no refraction (or we had TIR), treat as pure reflection.
    bool hasRefraction = (mat.Ior > 1.01f && metallic < 0.99f);
    if (!hasRefraction)
    {
        refractionColor = 0.0f;
        F = 1.0f.xxx;
    }

    float3 surfaceRTColor = lerp(refractionColor, reflectionColor, F);

    // ------------------------------------------------------------------
    // FINAL COMPOSITE + POST
    // ------------------------------------------------------------------
    float3 finalColor = Lo + areaLightContribution + surfaceRTColor;
    finalColor = PostProcess(finalColor);

    gOutput[launchIndex] = float4(finalColor, 1.0f);
}
