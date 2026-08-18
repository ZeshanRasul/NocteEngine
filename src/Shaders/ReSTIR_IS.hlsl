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
    uint   pad0;
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
// Approximate unshadowed irradiance: luminance × NdotL / dist²
// (No BRDF – BRDF evaluated at shading time in Hit.hlsl)
// --------------------------------------------------------------------------
float pHatArea(float3 N, float3 P, float3 lightPos, float3 radiance)
{
    float3 toLight = lightPos - P;
    float  dist2   = max(dot(toLight, toLight), 1e-6f);
    float  NdotL   = max(dot(N, normalize(toLight)), 0.0f);
    float  lum     = dot(radiance, float3(0.2126f, 0.7152f, 0.0722f));
    return NdotL * lum / dist2;
}

float pHatSun(float3 N, float3 sunDir, float3 sunColor)
{
    float NdotL = max(dot(N, normalize(-sunDir)), 0.0f);
    float lum   = dot(sunColor, float3(0.2126f, 0.7152f, 0.0722f));
    return NdotL * lum;
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

            float ph = pHatArea(N, P, pointOnLight, al.Radiance);
            // Source PDF = 1 / totalLights → WRS weight = p_hat / q = p_hat * totalLights
            w = ph * float(totalLights);
        }
        else
        {
            // Sun treated as directional (virtual point far along L direction).
            pointOnLight = P + normalize(-gISSunDir) * 1e6f;
            float ph = pHatSun(N, gISSunDir, gISSunColor);
            w = ph * float(totalLights);
        }

        UpdateReservoir(r, (int)li, pointOnLight, w, IS_Rand(seed));
    }

    // Finalize unbiased contribution weight:
    //   W = (1 / p_hat(y)) * (W_sum / M)
    if (r.LightIndex >= 0)
    {
        float ph = 0.0f;
        if (r.LightIndex < (int)gNumAreaLights)
            ph = pHatArea(N, P, r.PointOnLight, gAreaLights[r.LightIndex].Radiance);
        else
            ph = pHatSun(N, gISSunDir, gISSunColor);

        r.W = (ph > 1e-10f) ? (r.W_sum / (float(r.M) * ph)) : 0.0f;
    }

    gReservoirs[linearIdx] = r;
}
