#ifndef PATHTRACER_COMMON_HLSL
#define PATHTRACER_COMMON_HLSL

#include "Common.hlsl"
#include "MicrofacetBRDFUtils.hlsl"

//---------------------------------------------------------------------
// Payload
//---------------------------------------------------------------------
struct PathPayload
{
    float3 radiance; // unused in this minimal core, but handy later
    float3 throughput; // path throughput
    float3 hitPos; // world-space hit
    float3 normal; // shading normal
    float3 wi; // sampled next direction
    float3 emission; // emission at this hit (or env)
    float3 bsdfOverPdf; // f * cosTheta / pdf

    float pdf; // last sample pdf (optional, for debugging)
    uint depth; // bounce depth
    uint done; // 0 = continue, 1 = terminate
    uint seed; // RNG state
    uint pad; // keep payload size aligned
};



struct Attributes
{
    float2 bary;
};

//---------------------------------------------------------------------
// RNG
//---------------------------------------------------------------------

// Simple integer hash
uint Hash(uint x)
{
    x ^= x >> 17;
    x *= 0xed5ad4bbu;
    x ^= x >> 11;
    x *= 0xac4c1b51u;
    x ^= x >> 15;
    x *= 0x31848babu;
    x ^= x >> 14;
    return x;
}

// Advance RNG and return float in [0,1)
//float Rand(inout uint state)
//{
//    state = Hash(state);
//    return (state & 0x00FFFFFFu) / 16777216.0f; // 2^24
//}

//float2 Rand2(inout uint state)
//{
//    return float2(Rand(state), Rand(state));
//}

float Rand(inout uint state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (float(state) * (1.0 / 4294967296.0)); // [0,1)
}

float2 Rand2(inout uint state)
{
    return float2(Rand(state), Rand(state));
}

//---------------------------------------------------------------------
// Utility: transform normal to world (depends on how you store matrices)
// Here we assume WorldToObject3x4() is inverse(world), and world is rigid.
//---------------------------------------------------------------------
float3 TransformNormalToWorld(float3 nObj)
{
    float3x3 objToWorld = (float3x3) ObjectToWorld3x4();
    float3 N = normalize(mul(nObj, objToWorld));
    return N;
}

//---------------------------------------------------------------------
// BSDF sampling: Disney diffuse + GGX specular (metal/rough)
// Returns bsdfOverPdf = f * NdotL / pdf
//---------------------------------------------------------------------
//void BSDF_Sample(
//    Material mat,
//    float3 N,
//    float3 wo,
//    float2 xi,
//    out float3 wi,
//    out float3 bsdfOverPdf,
//    out float outPdf)
//{
//    bsdfOverPdf = 0.0;
//    outPdf = 1.0;

//    float3 V = normalize(wo);

//    // Disney metal workflow
//    float3 Cd, F0;
//    ComputeDisneyMetalWorkflow(mat.DiffuseAlbedo.xyz, mat.metallic, Cd, F0);

//    float roughness = saturate(1.0f - mat.Shininess); // adapt to your data

//    // Probability of sampling specular vs diffuse
//    float specProb = saturate(mat.metallic);
//    specProb = clamp(specProb, 0.05f, 0.95f);

//    float2 xiRemap;
//    float3 H;
//    float pdfSpec = 0.0;
//    float pdfDiff = 0.0;

//    if (xi.x < specProb)
//    {
//        // Specular branch
//        xiRemap = float2(xi.x / specProb, xi.y);

//        H = SampleGGXVNDF(V, xiRemap, roughness);
//        wi = reflect(-V, H);
//        wi = normalize(wi);

//        float NdotL = saturate(dot(N, wi));
//        float NdotV = saturate(dot(N, V));
//        if (NdotL <= 0.0f || NdotV <= 0.0f)
//        {
//            bsdfOverPdf = 0.0;
//            outPdf = 1.0;
//            return;
//        }

//        float3 Hn = normalize(V + wi);
//        float NdotH = saturate(dot(N, Hn));
//        float LdotH = saturate(dot(wi, Hn));

//        float3 F = Fresnel_Schlick(F0, LdotH);
//        float D = GGX_D(NdotH, roughness);
//        float G = GGX_G_Smith(NdotV, NdotL, roughness);

//        float denom = max(4.0f * NdotL * NdotV, 1e-4f);
//        float3 fSpec = (D * G * F) / denom;
//        pdfSpec = GGX_PDF(N, V, wi, roughness);
//        outPdf = pdfSpec * specProb;
//        bsdfOverPdf = fSpec * NdotL / max(outPdf, 1e-6f);
//    }
//    else
//    {
//        // Diffuse branch
//        xiRemap = float2((xi.x - specProb) / (1.0f - specProb), xi.y);

//        float3 local = CosineSampleHemisphere(xiRemap);
//        float3x3 frame = BuildTangentFrame(N);
//        wi = normalize(mul(local, frame));

//        float NdotL = saturate(dot(N, wi));
//        float NdotV = saturate(dot(N, V));
//        if (NdotL <= 0.0f || NdotV <= 0.0f)
//        {
//            bsdfOverPdf = 0.0;
//            outPdf = 1.0;
//            return;
//        }

//        float3 Hn = normalize(V + wi);
//        float LdotH = saturate(dot(wi, Hn));

//        float diffBRDF = DisneyDiffuse(NdotV, NdotL, LdotH, roughness);
//        float3 fDiff = Cd * diffBRDF;

//        pdfDiff = NdotL / 3.14159265f;
//        outPdf = pdfDiff * (1.0f - specProb);
//        bsdfOverPdf = fDiff * NdotL / max(outPdf, 1e-6f);
//    }
//}

#endif // PATHTRACER_COMMON_HLSL
