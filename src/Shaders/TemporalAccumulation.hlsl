cbuffer DenoiseParams : register(b0)
{
    float gColorSigma;
    float gNormalSigma;
    float gDepthSigma;
    int gStepSize;
    float2 invResolution;
    int passNum;
    int useHistory;
}

cbuffer PostProcess : register(b1)
{
    float Exposure;
    int ToneMapMode;
    int DebugMode;
    int IsLastPass;
}

Texture2D<float4> Input : register(t0); // Current frame raw radiance
Texture2D<float4> HistoryRadiance : register(t7); // Previous accumulated result

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

    float3 C = Input[coord].rgb; // Current frame raw radiance
    
    // CRITICAL: Check useHistory FIRST before reading any history
    if (useHistory == 0)
    {
        // First frame or camera moved - use current frame directly, no blending
        TARadiance[coord] = float4(C, 1.0f);
        FirstMomentNew[coord] = float4(C, 1.0f);
        SecondMomentNew[coord] = float4(C * C, 1.0f);
        return;
    }
    
    // We have valid history - read it
    float3 history = HistoryRadiance[coord].rgb;
    float3 m1Prev = FirstMomentOld[coord].rgb;
    float3 m2Prev = SecondMomentOld[coord].rgb;
    
    // Compute variance-based clamping to reject outliers (fireflies)
    float3 mean = m1Prev;
    float3 variance = max(m2Prev - mean * mean, 0.0.xxx);
    float3 sigma = sqrt(variance + 1e-6.xxx);

    float k = max(gColorSigma, 1.0); // Controls how aggressive clamping is
    float3 lo = mean - k * sigma;
    float3 hi = mean + k * sigma;

    // Clamp current sample to reject fireflies
    float3 clamped = clamp(C, lo, hi);
    
    // Blend factor: lower = more temporal smoothing
    float alpha = 0.05f;
    
    // Blend history with clamped current sample
    float3 accumulated = lerp(history, clamped, alpha);
    
    // Update moments with exponential moving average
    float3 m1 = lerp(m1Prev, clamped, alpha);
    float3 m2 = lerp(m2Prev, clamped * clamped, alpha);

    history = HistoryRadiance[coord].rgb;
    alpha = 0.05f;
    accumulated = lerp(history, C, alpha);
    TARadiance[coord] = float4(accumulated, 1.0f);
    FirstMomentNew[coord] = float4(C, 1.0f);
    SecondMomentNew[coord] = float4(C * C, 1.0f);
}
