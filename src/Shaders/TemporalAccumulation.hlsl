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

Texture2D<float4> Input : register(t0);
Texture2D<float4> HistoryRadiance : register(t7);

Texture2D<float4> FirstMomentOld : register(t3);
Texture2D<float4> SecondMomentOld : register(t4);

RWTexture2D<float4> Output : register(u0);
RWTexture2D<float4> TARadiance : register(u5);
RWTexture2D<float4> FirstMomentNew : register(u1);
RWTexture2D<float4> SecondMomentNew : register(u2);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 coord = int2(dispatchThreadId.xy);

    int2 dim;
    Input.GetDimensions(dim.x, dim.y);
    if (coord.x < 0 || coord.y < 0 || coord.x >= dim.x || coord.y >= dim.y)
        return;

    float3 C = Input[coord].rgb;

    if (useHistory == 0)
    {
        TARadiance[coord] = float4(C, 1.0f);
        FirstMomentNew[coord] = float4(C, 1.0f);
        SecondMomentNew[coord] = float4(C * C, 1.0f);
        return;
    }

    float3 history = HistoryRadiance[coord].rgb;
    float alpha = 0.05f;
    
    float3 accumulated = lerp(history, C, alpha);
    TARadiance[coord] = float4(accumulated, 1.0f);
    FirstMomentNew[coord] = float4(accumulated, 1.0f);
    SecondMomentNew[coord] = float4(accumulated * accumulated, 1.0f);
}