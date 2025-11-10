#include "Common.hlsl"
#define MaxLights 10
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

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);

[shader("raygeneration")]
void RayGen()
{
  // Initialize the ray payload
    HitInfo payload;
    payload.colorAndDistance = float4(0.0, 0.0, 0.0, 0.0);

  // Get the location within the dispatched 2D grid of work items
  // (often maps to pixels, so this could represent a pixel coordinate).
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);
    float2 d = (((launchIndex.xy + 0.5f) / dims.xy) * 2.0f - 1.0f);
    
    RayDesc ray;
    ray.Origin = float3(d.x, -d.y, 22);
    ray.Direction = float3(0, 0, -1);
    ray.TMin = 0;
    ray.TMax = 100000;
    
    TraceRay(
    SceneBVH,
    RAY_FLAG_NONE,
    0XFF,
    0,
    0,
    0,
    ray,
    payload);
    
    gOutput[launchIndex] = float4(payload.colorAndDistance.rgb, 1.f);
}
