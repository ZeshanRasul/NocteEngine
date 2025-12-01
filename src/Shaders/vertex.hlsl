#include "LightingUtil.hlsl"

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gInvWorld;
    int materialIndex;
    int poInstanceID;
    int InstanceOffset;
    int pad3;
}

struct InstanceData
{
    float4x4 instWorld;
    float4x4 instInvWorld;
    uint MaterialIndex;
    uint InstanceID;
    uint pad;
    uint pad2;
};

cbuffer cbPass : register(b1)
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

StructuredBuffer<InstanceData> instancesData : register(t1);


struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    uint InstanceID : SV_InstanceID;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    uint InstanceID : SV_InstanceID;
};

VSOutput VS(VertexIn vIn)
{
    VSOutput vso;

    uint idx = InstanceOffset + vIn.InstanceID;
    
    InstanceData inst = instancesData[idx];
    
    float4 homogPosW = mul(float4(vIn.PosL, 1.0f), transpose(inst.instWorld));

    vso.PosW = homogPosW.xyz / homogPosW.w;
  
    float3x3 instW3x3 = (float3x3) inst.instWorld;
    
    float3x3 invTrans = transpose(instW3x3);
    vso.NormalW = normalize(mul(invTrans, vIn.NormalL));
    
    vso.PosH = mul(float4(vso.PosW, 1.0f), gViewProj);
    
    vso.InstanceID = vIn.InstanceID;
    
    return vso;
}