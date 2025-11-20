#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#include "LightingUtil.hlsl"

struct PixelIn
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
};

cbuffer cbMaterial : register(b1)
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Ior;
    float Reflectivity;
    float3 Absorption;
    float Shininess;
    float pad;
    float pad1;
    float metallic;
    bool IsReflective;
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
    
    Light gLights[MaxLights];
};

struct GBuffer
{
    float4 gBufferAlbedoMetal : SV_Target0;
    float4 gBufferNormalRough : SV_Target1;
};

GBuffer PS(PixelIn pIn)
{   
    GBuffer gBuffer;

    gBuffer.gBufferAlbedoMetal.xyz = DiffuseAlbedo.xyz;
    gBuffer.gBufferAlbedoMetal.w = metallic;
    
    gBuffer.gBufferNormalRough.xyz = pIn.NormalW;
    gBuffer.gBufferNormalRough.w = 1.0 - Shininess;
    
    return gBuffer;

}