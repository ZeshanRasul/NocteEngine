struct Light
{
    float3 Strength;
    float FalloffStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
};

#define MaxLights 16

cbuffer DenoiseParams : register(b0)
{
    float gColorSigma;
    float gNormalSigma;
    float gDepthSigma;
    int gStepSize;
    float2 invResolution;
    int passNum;
    int useHistory;
    int frameIndex;
}

cbuffer PostProcess : register(b1)
{
    float Exposure;
    int ToneMapMode;
    int DebugMode;
    int IsLastPass;
}

cbuffer cbPass : register(b2)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4x4 gPrevViewProj;
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

Texture2D<float4> Input : register(t0);
Texture2D<float> Depth : register(t2);
Texture2D<float4> HistoryRadiance : register(t7);

Texture2D<float4> FirstMomentOld : register(t3);
Texture2D<float4> SecondMomentOld : register(t4);

RWTexture2D<float4> Output : register(u0);
RWTexture2D<float4> TARadiance : register(u5);
RWTexture2D<float4> FirstMomentNew : register(u1);
RWTexture2D<float4> SecondMomentNew : register(u2);

SamplerState LinearClampSampler : register(s0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 coord = int2(dispatchThreadId.xy);

    int2 dim;
    Input.GetDimensions(dim.x, dim.y);
    if (coord.x < 0 || coord.y < 0 || coord.x >= dim.x || coord.y >= dim.y)
        return;

    float2 uv = (float2(coord) + 0.5f) * invResolution;

    float depth = Depth[coord]; 

    float4 currClip;
    currClip.xy = uv * 2.0f - 1.0f;
    currClip.y *= -1.0f; // depends on convention
    currClip.z = depth;
    currClip.w = 1.0f;

    float4 worldH = mul(currClip, gInvViewProj);
    float3 worldPos = worldH.xyz / worldH.w;
    
    float4 prevClip = mul(float4(worldPos, 1.0f), gPrevViewProj);

    bool valid = prevClip.w > 0.0f;

    float2 prevUV = prevClip.xy / prevClip.w;
    prevUV = prevUV * 0.5f + 0.5f;
    prevUV.y = 1.0f - prevUV.y; // if needed

    valid = all(prevUV >= 0.0f) && all(prevUV <= 1.0f);
    
    float3 C = Input[coord].rgb;

    if (useHistory == 0)
    {
        TARadiance[coord] = float4(C, 1.0f);
        FirstMomentNew[coord] = float4(C, 1.0f);
        SecondMomentNew[coord] = float4(C * C, 1.0f);
        Output[coord] = float4(C, 1.0f);
        return;
    }

    
    float3 history = C;

    if (valid)
    {
        history = HistoryRadiance.SampleLevel(LinearClampSampler, prevUV, 0).rgb;
    }
    
    float3 m1Prev = FirstMomentOld[coord].rgb;
    float3 m2Prev = SecondMomentOld[coord].rgb;

    float alpha = valid ? 0.1f : 1.0f;
    float3 accumulated = lerp(history, C, alpha);
    
    float3 minC = 1000000;
    float3 maxC = -1000000;

    for (int j = -1; j <= 1; ++j)
    {
    
        for (int i = -1; i <= 1; ++i)
        {
            int2 p = clamp(coord + int2(i, j), int2(0, 0), dim - 1);
            float3 c = Input[p].rgb;
            minC = min(minC, c);
            maxC = max(maxC, c);
        }
    }
    
    history = clamp(history, minC, maxC);
 //   float3 accumulated = lerp(history, C, alpha);
    float3 m1 = lerp(m1Prev, C, alpha);
    float3 m2 = lerp(m2Prev, C * C, alpha);

    TARadiance[coord] = float4(accumulated, 1.0f);
    FirstMomentNew[coord] = float4(m1, 1.0f);
    SecondMomentNew[coord] = float4(m2, 1.0f);
//    Output[coord] = float4(accumulated, 1.0f);
    Output[coord] = float4(accumulated, 1.0f);

}