// Resampled Importance Sampling (RIS) – initial candidate sampling.
//
// This is the *initial sampling* stage of ReSTIR DI (Bitterli et al. 2020) and
// nothing more. There is no temporal reuse pass and no spatial reuse pass, so
// this is RIS in the sense of Talbot et al. 2005 rather than full ReSTIR — the
// "spatiotemporal" half of the algorithm is not implemented.
//
// For each screen pixel that hit a surface (gWorldPos.w == 1):
//   1. Draw NUM_CANDIDATES random lights.
//   2. Accept each with WRS probability proportional to p_hat (unshadowed irradiance).
//   3. Compute the unbiased contribution weight W.
//   4. Store the reservoir for the next frame's Hit shader to consume.
//
// No shadow rays are fired here; visibility is tested once, in Hit.hlsl, against
// the single surviving sample.
//
// Known limitation: the reservoir is consumed one frame later and is not
// reprojected, so under camera motion a pixel's reservoir may describe a surface
// that is no longer there.

#include "ReSTIR.hlsl"

#define MAX_AREA_LIGHTS 5
#define NUM_CANDIDATES  32

// --------------------------------------------------------------------------
// RNG (independent from RT shaders – no shared state)
// --------------------------------------------------------------------------
float IS_Rand(inout uint s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return float(s) * (1.0f / 4294967296.0f);
}
float2 IS_Rand2(inout uint s) { return float2(IS_Rand(s), IS_Rand(s)); }

// --------------------------------------------------------------------------
// Data structures
// --------------------------------------------------------------------------
struct AreaLight
{
    float3 Position; float Pad;
    float3 U;        float Pad2;
    float3 V;        float Pad3;
    float3 Radiance; float Area;
};

cbuffer ReSTIRCB : register(b0)
{
    uint   gISWidth;
    uint   gISHeight;
    uint   gISFrameIndex;
    uint   gEnableRIS;  // 0 = write empty reservoirs so Hit.hlsl falls back to uniform NEE
    float3 gISSunDir;   // world-space sun direction (points toward surface)
    float  pad1;
    float3 gISSunColor;
    float  pad2;
}

cbuffer AreaLightsCB : register(b1)
{
    AreaLight gAreaLights[MAX_AREA_LIGHTS];
    uint      gNumAreaLights;
    float3    gAreaLightPad;
}

// G-Buffer written by RayGen – read here while still in UAV state (no barrier needed).
RWTexture2D<float4>           gWorldPos   : register(u0); // xyz=pos, w=1 if surface
RWTexture2D<float4>           gNormal     : register(u1); // encoded (xyz*0.5+0.5), w=isEmissive
RWStructuredBuffer<Reservoir> gReservoirs : register(u2);

// --------------------------------------------------------------------------
// Target function  p_hat(x)
// (No BRDF term – the BRDF is evaluated at shading time in Hit.hlsl)
// --------------------------------------------------------------------------
// Both target functions below return approximate unshadowed *irradiance* at P.
// Keeping them in the same measure is essential: WRS selects proportional to
// p_hat, so if one light type's p_hat is expressed in different units the
// reservoir simply always picks that type.
//
// An earlier version divided the area-light term by d^2 but omitted the light's
// area and its cosine, while the sun term had no geometric factor at all. With
// this scene's lights (area = |U x V| = 810,000 at a distance of a few hundred
// units) that made the sun's p_hat larger by roughly six orders of magnitude, so
// the area lights were never selected and shadowed regions such as the archway
// lost their only light source.
// Hit.hlsl shades the reservoir sample as  f * Li * NdotL * W , which is the
// solid-angle form of the RIS estimator. Both p_hat and the source pdf must
// therefore be solid-angle densities, and crucially the SAME p_hat has to appear
// in the candidate weight (w = p_hat / q) and in the finalisation
// (W = W_sum / (M * p_hat)). Mixing measures between those two places scales the
// result by the subtended solid angle — brighter when that is below 1 sr, darker
// above — which is a bias, not noise, and does not wash out with more samples.
//
// So p_hat is the integrand without the BRDF, in solid angle: luminance * NdotL.
// All geometry (area, light cosine, inverse square) belongs in 1/q instead.
float pHatSolidAngle(float3 N, float3 L, float3 radiance)
{
    float NdotL = max(dot(N, L), 0.0f);
    float lum   = dot(radiance, float3(0.2126f, 0.7152f, 0.0722f));
    return NdotL * lum;
}

// Reciprocal of the solid-angle pdf for a point sampled uniformly over the quad:
//   pdf_sa = d^2 / (Area * cos(theta_light))   ->   1/pdf_sa = Area * cos / d^2
// Also returns the normalised direction to that sample. Emission is one-sided,
// matching SampleAreaLight() in Hit.hlsl.
float AreaSampleInvPdf(AreaLight al, float3 P, float3 pointOnLight, out float3 L)
{
    float3 toLight = pointOnLight - P;
    float  dist2   = max(dot(toLight, toLight), 1e-6f);
    L = toLight * rsqrt(dist2);

    float3 nL   = normalize(cross(al.U, al.V));
    float  cosL = max(dot(nL, -L), 0.0f);

    return (al.Area * cosL) / dist2;
}

// --------------------------------------------------------------------------
// Kernel
// --------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void ReSTIR_IS(uint3 id : SV_DispatchThreadID)
{
    uint2 px = id.xy;
    if (px.x >= gISWidth || px.y >= gISHeight)
        return;

    uint linearIdx = px.y * gISWidth + px.x;

    // RIS disabled: invalidate the reservoir. Hit.hlsl gates on
    // (LightIndex >= 0 && W > 0), so an empty reservoir makes it fall back to
    // uniform light selection with no shader-side branch of its own. This is the
    // A/B baseline for the with/without-RIS comparison.
    if (gEnableRIS == 0u)
    {
        gReservoirs[linearIdx] = EmptyReservoir();
        return;
    }

    // Sky pixel → empty reservoir, nothing to sample.
    float4 wpSample = gWorldPos[px];
    if (wpSample.w < 0.5f)
    {
        gReservoirs[linearIdx] = EmptyReservoir();
        return;
    }

    float3 P = wpSample.xyz;
    float3 N = normalize(gNormal[px].xyz * 2.0f - 1.0f);

    uint seed = px.x * 1973u ^ px.y * 9277u ^ gISFrameIndex * 26699u;

    // Light pool: area lights first, sun last.
    uint totalLights = gNumAreaLights + 1u;

    Reservoir r = EmptyReservoir();

    for (int i = 0; i < NUM_CANDIDATES; ++i)
    {
        uint li = min((uint)(IS_Rand(seed) * float(totalLights)), totalLights - 1u);

        float  w            = 0.0f;
        float3 pointOnLight = P;

        if (li < gNumAreaLights)
        {
            AreaLight al = gAreaLights[li];
            float2    xi = IS_Rand2(seed);
            pointOnLight = al.Position
                         + (xi.x - 0.5f) * al.U
                         + (xi.y - 0.5f) * al.V;

            // q = P(pick this light) * pdf_sa(point) = (1/totalLights) * pdf_sa
            float3 L;
            float  invPdfSA = AreaSampleInvPdf(al, P, pointOnLight, L);
            float  ph       = pHatSolidAngle(N, L, al.Radiance);
            w = ph * float(totalLights) * invPdfSA;
        }
        else
        {
            // The sun is a delta light: selecting it fully determines the
            // direction, so there is no continuous sampling density to divide
            // out and q is simply 1/totalLights.
            float3 L = normalize(-gISSunDir);
            pointOnLight = P + L * 1e6f;
            float ph = pHatSolidAngle(N, L, gISSunColor);
            w = ph * float(totalLights);
        }

        UpdateReservoir(r, (int)li, pointOnLight, w, IS_Rand(seed));
    }

    // Finalize unbiased contribution weight:
    //   W = (1 / p_hat(y)) * (W_sum / M)
    if (r.LightIndex >= 0)
    {
        // Must be the same p_hat used to build the candidate weights above.
        float ph = 0.0f;
        if (r.LightIndex < (int)gNumAreaLights)
        {
            float3 L = normalize(r.PointOnLight - P);
            ph = pHatSolidAngle(N, L, gAreaLights[r.LightIndex].Radiance);
        }
        else
        {
            ph = pHatSolidAngle(N, normalize(-gISSunDir), gISSunColor);
        }

        r.W = (ph > 1e-10f) ? (r.W_sum / (float(r.M) * ph)) : 0.0f;
    }

    gReservoirs[linearIdx] = r;
}
