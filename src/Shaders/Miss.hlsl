#include "Common.hlsl"
#include "PathTracerCommon.hlsl"
#include "SkyCommon.hlsl"

// Partial cbPass declaration — only the fields we need for the sky.
// Layout must exactly match PassConstants in FrameResource.h.
cbuffer cbPass : register(b0)
{
    float4 _pm[28];                          // 7 float4x4 matrices = 28 float4 rows
    float3 _eye; uint _spp;                  // gEyePosW + SPP
    float4 _rt;                              // RenderTargetSize + InvRenderTargetSize
    float4 _zpad;                            // NearZ + FarZ + MaterialsSize + TexturesSize
    float4 gSunDir;                          // AmbientLight: xyz = direction toward surface, w = firstFrame
    int _dp; int _sm; float _bp; float _lp;  // directPresent + SamplingMode + probs
    int _mb; int _fi; int _nee; int _rl;     // MaxBounces + FrameIndex + UseNEE + UseRL
    int _qt; float3 gSunColor;               // UseQTable + SunColor
};

// SUN_DISC_HALF_ANGLE: half-angle of the solar disc in radians.
// Real sun ≈ 0.0045 rad; increase for a larger, softer disc.
static const float SUN_DISC_HALF_ANGLE = 0.015f;
static const float SUN_DISC_EDGE       = 0.025f; // soft falloff outer edge

[shader("miss")]
void Miss(inout PathPayload payload)
{
    float3 dir = normalize(WorldRayDirection());

    // gSunDir.xyz points toward the surface (downward-ish); negate for sun-toward direction.
    float3 sunUp = normalize(-gSunDir.xyz);

    float3 envColor = SamplePreethamSky(dir, sunUp, SkyTurbidity, SkyIntensity);

    // Solar disc — add a bright cap in the sun direction.
    if (sunUp.y > -0.05f) // only when sun is above or near horizon
    {
        float cosAngle = dot(dir, sunUp);
        float cosDisk  = cos(SUN_DISC_HALF_ANGLE);
        float cosEdge  = cos(SUN_DISC_EDGE);

        if (cosAngle > cosEdge)
        {
            float t        = saturate((cosAngle - cosEdge) / (cosDisk - cosEdge));
            float discMask = smoothstep(0.0f, 1.0f, t);
            // Sun radiance: bright white tinted by gSunColor; scale with intensity.
            float3 sunRadiance = gSunColor * SkyIntensity * 8.0f;
            envColor = lerp(envColor, sunRadiance, discMask);
        }
    }

    payload.hitSomething    = 0;
    payload.tHit            = 1e20f;
    payload.isEmissive      = 1;
    payload.emission        = envColor;
    payload.firstHitAlbedo  = 0.0f;
    payload.bsdfOverPdf     = 0.0f;
    payload.pdf             = 1.0f;
    payload.done            = 1;
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload hit)
{
    hit.isHit = false;
}
