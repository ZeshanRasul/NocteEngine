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
    float gAlbedoSigma;
    int gStepSize;
    float2 invResolution;
    int passNum;
    int useHistory;
    int frameIndex;
    int useTemporalAccumulation;
    int useDenoising;
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

    float linearDepth = Depth[coord]; 
    
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 pClip = float4(ndc, 1.0f, 1.0f);
    float4 pView = mul(pClip, gInvProj);
    pView /= pView.w;
    
    float3 rayDirVS = normalize(pView.xyz);
    
    bool depthValid = isfinite(linearDepth) && linearDepth > 1e-5f;
    //if (!depthValid)
    //{
    //    Output[coord] = float4(1, 0, 1, 1);
    //    return;
    //}

    
    float t = linearDepth / max(1e-6f, rayDirVS.z);
    float3 hitPosVS = rayDirVS * t;
    float4 worldH = mul(float4(hitPosVS, 1.0f), gInvView);
    
    bool worldValid =
    isfinite(worldH.x) &&
    isfinite(worldH.y) &&
    isfinite(worldH.z) &&
    isfinite(worldH.w) &&
    abs(worldH.w) > 1e-6f;

    //if (!worldValid)
    //{
    //    TARadiance[coord] = float4(1, 0, 1, 1);
    //    return;
    //}
    
    float3 worldPos = worldH.xyz / worldH.w;

    float4 prevClip = mul(float4(worldPos, 1.0f), gPrevViewProj);
    bool valid =
    isfinite(prevClip.x) &&
    isfinite(prevClip.y) &&
    isfinite(prevClip.z) &&
    isfinite(prevClip.w) &&
    abs(prevClip.w) > 1e-6f;


    
    float3 C = Input[coord].rgb;

    if (useHistory == 0)
    {
        TARadiance[coord] = float4(C, 1.0f);
        FirstMomentNew[coord] = float4(C, 1.0f);
        SecondMomentNew[coord] = float4(C * C, 1.0f);
        Output[coord] = float4(C, 1.0f);
        return;
    }

    if (!valid || !worldValid || !depthValid)
    {
        TARadiance[coord] = float4(C, 1.0f);
        FirstMomentNew[coord] = float4(C, 1.0f);
        SecondMomentNew[coord] = float4(C * C, 1.0f);
        Output[coord] = float4(C, 1.0f);
        return;
    }
    
    float2 prevUV = prevClip.xy / prevClip.w;
    prevUV = prevUV * 0.5f + 0.5f;
    prevUV.y = 1.0f - prevUV.y; // if needed

    valid = valid && all(prevUV >= 0.0f) && all(prevUV <= 1.0f);
    
    float3 history = C;
    float3 m1Prev = C;
    float3 m2Prev = C * C;

    if (valid)
    {
        history = HistoryRadiance.SampleLevel(LinearClampSampler, prevUV, 0).rgb;
        m1Prev = FirstMomentOld.SampleLevel(LinearClampSampler, prevUV, 0).rgb;
        m2Prev = SecondMomentOld.SampleLevel(LinearClampSampler, prevUV, 0).rgb;
    }
    

    
    float3 mu = 0;
    float3 var = 0;

// mean
    for (int j = -1; j <= 1; ++j)
    {
        for (int i = -1; i <= 1; ++i)
        {
            int2 p = clamp(coord + int2(i, j), int2(0, 0), dim - 1);
            float3 c = Input[p].rgb;
            mu += c;
        }
    }
    mu /= 9.0;

// variance
    for (int g = -1; g <= 1; ++g)
    {
        for (int i = -1; i <= 1; ++i)
        {
            int2 p = clamp(coord + int2(i, g), int2(0, 0), dim - 1);
            float3 c = Input[p].rgb;
            float3 d = c - mu;
            var += d * d;
        }
    }
    var /= 9.0;

    float3 sigma = sqrt(var);

// clamp
    float k = 5.75;
//    history = clamp(history, mu - k * sigma, mu + k * sigma);
 //   float3 accumulated = lerp(history, C, alpha);
    
    float historyWeight = (float) frameIndex;
    float alpha = 1.0f / (historyWeight + 1.0f);
    float3 accumulated = lerp(history, C, alpha);
    
    float3 m1 = lerp(m1Prev, C, alpha);
    float3 m2 = lerp(m2Prev, C * C, alpha);

    bool inBounds =
    prevUV.x >= 0.0f && prevUV.x <= 1.0f &&
    prevUV.y >= 0.0f && prevUV.y <= 1.0f;
  
    if (useTemporalAccumulation == 0)
    {
        TARadiance[coord] = float4(history, 1.0f);
        Output[coord] = float4(history, 1.0f);
        return;
    }
    
    linearDepth = Depth[coord];
    depthValid = isfinite(linearDepth) && linearDepth > 1e-5f;
    
    float2 uvScaled = prevUV * 20.0f;
    float checker = fmod(floor(uvScaled.x) + floor(uvScaled.y), 2.0f);
  //  TARadiance[coord] = checker > 0.5f ? float4(1, 1, 1, 1) : float4(0, 0, 0, 1);
  //  TARadiance[coord] = float4(saturate(hitPosVS.z / 10.0f).xxx, 1);
    
    
    
 //   TARadiance[coord] = inBounds ? float4(prevUV, 0, 1) : float4(1, 0, 0, 1);
 //   Output[coord] = inBounds ? float4(prevUV, 0, 1) : float4(1, 0, 0, 1);
    TARadiance[coord] = float4(accumulated, 1.0f);
  //  TARadiance[coord] = float4(prevUV.x.xxx, 1.0f);
    FirstMomentNew[coord] = float4(m1, 1.0f);
    SecondMomentNew[coord] = float4(m2, 1.0f);
    Output[coord] = float4(accumulated, 1.0f);
//    Output[coord] = float4(accumulated, 1.0f);

}