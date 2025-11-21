#include "Common.hlsl"
#define MaxLights 16
struct Light
{
    float3 Strength;
    float FalloffStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
};
// Raytracing output texture, accessed as a UAV
RWTexture2D<float4> gOutput : register(u0);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);
Texture2D<float4> GBufferAlbedoMetal : register(t4);
Texture2D<float4> GBufferNormalRough : register(t5);
Texture2D<float4> GBufferDepth : register(t6);

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

[shader("raygeneration")]
void RayGen()
{
  // Initialize the ray payload
    HitInfo payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);
    payload.depth = 0;
    payload.eta = 1.0f;

    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;

    // Normalized coordinates in NDC (-1..1)
    float2 pixelCenter = (float2(launchIndex) + 0.5f) / float2(dims);
    float2 d = pixelCenter * 2.0f - 1.0f; // NDC coords

    d.y = -d.y;

    // Construct a ray through the pixel in world space
    float4 originVS = float4(0, 0, 0, 1);
    float4 targetVS = mul(float4(d.x, d.y, 1.0, 1.0), gInvProj);
    targetVS /= targetVS.w;

    float3 originWS = mul(originVS, gInvView).xyz;
    float3 targetWS = mul(targetVS, gInvView).xyz;
    float3 dirWS = normalize(targetWS - originWS);

    RayDesc ray;
    ray.Origin = originWS;
    ray.Direction = dirWS;
    ray.TMin = 0.1f;
    ray.TMax = 1e38f;
    
    TraceRay(
    SceneBVH,
    RAY_FLAG_NONE,
    0XFF,
    0,
    3,
    0,
    ray,
    payload);
    
    gOutput[launchIndex] = float4(payload.colorAndDistance.rgb, 1.f);
}
