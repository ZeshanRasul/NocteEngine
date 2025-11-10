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

struct STriVertex
{
    float3 vertex;
    float3 normal;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);


[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) 
{
    float3 barycentrics = float3(1.0f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    
    const float3 A = float3(1.0, 0.0, 0.0);
    const float3 B = float3(0.0, 1.0, 0.0);
    const float3 C = float3(0.0, 0.0, 1.0);
    
    uint vertId = 3 * PrimitiveIndex();
    float3 hitColor = BTriVertex[vertId + 0].normal * barycentrics.x +
                  BTriVertex[vertId + 1].normal * barycentrics.y +
                  BTriVertex[vertId + 2].normal * barycentrics.z;
    
    payload.colorAndDistance = float4(gAmbientLight);
}
