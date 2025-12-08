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

  //  payload.emission = SampleEnvironment(dir);
    payload.emission = float3(0, 0, 0);
    payload.bsdfOverPdf = 0.0f;
    payload.pdf = 1.0f;
    payload.done = 1;
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload hit)
{
    hit.isHit = false;
}