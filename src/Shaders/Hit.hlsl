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

struct STriVertex
{
    float3 Vertex;
    float3 Normal;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);

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

[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) 
{
    float3 barycentrics = float3(1.0f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    
    //const float3 A = float3(1.0, 0.0, 0.0);
    //const float3 B = float3(0.0, 1.0, 0.0);
    //const float3 C = float3(0.0, 0.0, 1.0);
    
   uint vertId = 3 * PrimitiveIndex();
   float3 hitColor = BTriVertex[indices[vertId + 0]].Normal * barycentrics.x +
               BTriVertex[indices[vertId + 1]].Normal * barycentrics.y +
               BTriVertex[indices[vertId + 2]].Normal * barycentrics.z;
    float3 col = gLights[0].Direction * gAmbientLight.xyz * 0.9;
    payload.colorAndDistance = float4(hitColor, RayTCurrent());
}
