#include "Common.hlsl"
#include "PathTracerCommon.hlsl"

float3 SampleEnvironment(float3 dir)
{
    float t = 0.5f * (dir.y + 1.0f);
    float3 skyTop = float3(0.1f, 0.4f, 0.9f);
    float3 skyBottom = float3(0.3f, 0.8f, 0.7f);
    return lerp(skyBottom, skyTop, t);
}

[shader("miss")]
void Miss(inout PathPayload payload)
{
    float3 dir = normalize(WorldRayDirection());

    float3 envColor = SampleEnvironment(dir);
   // envColor = float3(0.0f, 0.0f, 0.0f);

    float maxEnvLum = 22.0f;
    float lum = dot(envColor, float3(0.2126, 0.7152, 0.0722));

    if (lum > maxEnvLum)
    {
        envColor *= maxEnvLum / lum;
    }
    
    payload.bsdfOverPdf = 0.0f;
    payload.pdf = 1.0f;
    payload.done = 1;
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload hit)
{
    hit.isHit = false;
}