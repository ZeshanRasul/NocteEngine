#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#include "LightingUtil.hlsl"

struct PixelIn
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    uint InstanceID : SV_InstanceID;
};

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gInvWorld;
    int matIndex;
    int poInstanceID;
    int InstanceOffset;
    int pad3;
}

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

StructuredBuffer<Material> materials : register(t0);

float2 EncodeNormalOct(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z) + 1e-6);

    float2 enc = n.xy;
    if (n.z < 0.0f)
    {
        enc = (1.0f - abs(enc.yx)) * float2(
            enc.x >= 0.0f ? 1.0f : -1.0f,
            enc.y >= 0.0f ? 1.0f : -1.0f
        );
    }

    // map from [-1,1] to [0,1] to fit UNORM
    return enc * 0.5f + 0.5f;
}

struct GBuffer
{
    float4 gBufferAlbedoMetal : SV_Target0;
    float4 gBufferNormalRough : SV_Target1;
};

GBuffer PS(PixelIn pIn)
{   
    GBuffer gBuffer;

    Material mat = materials[matIndex + pIn.InstanceID];
    
    gBuffer.gBufferAlbedoMetal.rgb = mat.DiffuseAlbedo.xyz;
    gBuffer.gBufferAlbedoMetal.a = (matIndex) / 255.0f;
    

    
    gBuffer.gBufferNormalRough.rg = EncodeNormalOct(pIn.NormalW);
    gBuffer.gBufferNormalRough.b = 1.0 - mat.Shininess;
    gBuffer.gBufferNormalRough.a = mat.metallic;
    
    return gBuffer;

}