#ifndef BSDF_HLSL
#define BSDF_HLSL

#include "Common.hlsl"
#include "MicrofacetBRDFUtils.hlsl"
#include "PathTracerCommon.hlsl"

// Sample result
struct BSDFSample
{
    float3 wi; // sampled direction (world space)
    float3 fOverPdf; // (f_spec + f_diff) * NdotL / pdf_total
    float pdf; // total pdf of sampling this wi
    bool valid;
};

// Cosine hemisphere pdf
static const float PI = 3.14159265f;

//----------------------------------------------------------------------------
// Evaluate both lobes (Disney diffuse + GGX spec)
//----------------------------------------------------------------------------

void EvaluateDisneyGGX(
    Material mat,
    float3 N,
    float3 V,
    float3 L,
    out float3 fSpec,
    out float3 fDiff,
    out float pdfSpec,
    out float pdfDiff)
{
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float LdotH = saturate(dot(L, H));

    if (NdotL <= 0.0f || NdotV <= 0.0f)
    {
        fSpec = 0.0f;
        fDiff = 0.0f;
        pdfSpec = 0.0f;
        pdfDiff = 0.0f;
        return;
    }

    float roughness = saturate(mat.Roughness);

    // Disney metal workflow helpers
    float3 Cd, F0;
    ComputeDisneyMetalWorkflow(mat.DiffuseAlbedo.xyz, mat.Metallic, Cd, F0);

    // Specular (GGX)
    float3 F = Fresnel_Schlick(F0, LdotH);
    float D = GGX_D(NdotH, roughness);
    float G = GGX_G_Smith(NdotV, NdotL, roughness);

    float denom = max(4.0f * NdotL * NdotV, 1e-4f);
    fSpec = (D * G * F) / denom;

    // Diffuse (Disney)
    float diffBRDF = DisneyDiffuse(NdotV, NdotL, LdotH, roughness);
    // Diffuse energy is only from the non-metal part
    fDiff = Cd * diffBRDF * (1.0f - mat.Metallic);

    // PDFs for each lobe given this direction
    pdfSpec = GGX_PDF(N, V, L, roughness); // microfacet lobe pdf
    pdfDiff = NdotL / PI; // cosine hemisphere
}

//----------------------------------------------------------------------------
// Importance sample the mixture lobe
//   - choose spec vs diffuse with some probability
//   - still evaluate BOTH lobes for energy-correct f / pdf
//----------------------------------------------------------------------------

BSDFSample SampleDisneyGGX(
    Material mat,
    float3 N,
    float3 V,
    float3 VLocal,
    float2 xi,
    float3x3 frame)
{
    BSDFSample s;
    s.valid = false;
    s.wi = 0.0;
    s.fOverPdf = 0.0;
    s.pdf = 1.0;

    float roughness = saturate(mat.Roughness);

    // Compute weights for picking a lobe.
    // This does NOT change energy, only affects variance.
    float3 Cd, F0;
    ComputeDisneyMetalWorkflow(mat.DiffuseAlbedo.xyz, mat.Metallic, Cd, F0);
    float specWeight = max(F0.r, max(F0.g, F0.b));
    float diffWeight = max(Cd.r, max(Cd.g, Cd.b)) * (1.0f - mat.Metallic);
    float sum = specWeight + diffWeight + 1e-6f;
    float specProb = saturate(specWeight / sum);
    //specProb = clamp(specProb, 0.1f, 0.9f);

    float2 xiRemap;
    float3 L;

    // 1) Sample one lobe to choose L
    if (xi.x < specProb)
    {
        // Specular
        xiRemap = float2(xi.x / specProb, xi.y);

        float3 HLocal = SampleGGXVNDF(VLocal, xiRemap, roughness);
        float3 LLocal = reflect(-VLocal, HLocal);
        L = normalize(mul(LLocal, frame));
    }
    else
    {
        // Diffuse
        xiRemap = float2((xi.x - specProb) / (1.0f - specProb), xi.y);

        float3 local = CosineSampleHemisphere(xiRemap);
        float3x3 newFrame = BuildTangentFrame(N);
        L = normalize(mul(local, newFrame));
    }

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    if (NdotL <= 0.0f || NdotV <= 0.0f)
    {
        return s; // invalid
    }

    // 2) Evaluate both lobes for this chosen direction
    float3 fSpec, fDiff;
    float pdfSpec, pdfDiff;
    EvaluateDisneyGGX(mat, N, V, L, fSpec, fDiff, pdfSpec, pdfDiff);

    // 3) Mixture pdf
    float pdfTotal = specProb * pdfSpec + (1.0f - specProb) * pdfDiff;
    if (pdfTotal <= 0.0f)
    {
        return s;
    }

    // Full BRDF (spec + diff)
    float3 fTotal = fSpec + fDiff;

    // f * cosTheta / pdf
    float3 fOverPdf = fTotal * (NdotL / pdfTotal);

    s.valid = true;
    s.wi = L;
    s.fOverPdf = fOverPdf;
    s.pdf = pdfTotal;
    return s;
}

float3 EvaluateDisneyBRDF(
    Material mat,
    float3 N,
    float3 V,
    float3 L)
{
    float3 fSpec, fDiff;
    float pdfSpec, pdfDiff;
    EvaluateDisneyGGX(mat, N, V, L, fSpec, fDiff, pdfSpec, pdfDiff);
    return fSpec + fDiff;
}

float PdfDisneyBRDF(
    Material mat,
    float3 N,
    float3 V,
    float3 L)
{
    float roughness = saturate(mat.Roughness);
    // Disney metal workflow helpers
    float3 Cd, F0;
    ComputeDisneyMetalWorkflow(mat.DiffuseAlbedo.xyz, mat.Metallic, Cd, F0);
    // Weights
    float specWeight = max(F0.r, max(F0.g, F0.b));
    float diffWeight = max(Cd.r, max(Cd.g, Cd.b)) * (1.0f - mat.Metallic);
    float sum = specWeight + diffWeight + 1e-6f;
    float specProb = saturate(specWeight / sum);
  //  specProb = clamp(specProb, 0.1f, 0.9f);
    // PDFs
    float pdfSpec = GGX_PDF(N, V, L, roughness);
    float NdotL = saturate(dot(N, L));
    float pdfDiff = NdotL / PI;
    // Mixture
    float pdfTotal = specProb * pdfSpec + (1.0f - specProb) * pdfDiff;
    return pdfTotal;
}

#endif // BSDF_HLSL
