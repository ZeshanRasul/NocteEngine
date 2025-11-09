cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
}

cbuffer cbMaterial : register(b1)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
}

cbuffer cbPass : register(b2)
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
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
};

VertexOut VS(VertexIn vIn)
{
    VertexOut vOut;
    
    float4 posW = mul(float4(vIn.PosL, 1.0f), gWorld);
    vOut.PosW = posW.xyz;
    
    vOut.NormalW = mul(vIn.NormalL, (float3x3)gWorld);
    
    vOut.PosH = mul(posW, gViewProj);
            
    return vOut;
}