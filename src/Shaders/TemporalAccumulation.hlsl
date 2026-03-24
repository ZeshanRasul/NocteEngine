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
    float3 history = HistoryRadiance[coord].rgb; // Previous accumulated
    
    float3 m1Prev = FirstMomentOld[coord].rgb;
    float3 m2Prev = SecondMomentOld[coord].rgb;

    // Use the useHistory flag directly - don't rely on checking for zeros
    bool hasHistory = (useHistory != 0);
    
    float alpha = 0.05f; // Blend factor: lower = more temporal smoothing
    
    if (!hasHistory)
    {
        // First frame - initialize everything
        m1Prev = C;
        m2Prev = C * C;
        history = C;
        alpha = 1.0f; // Use current frame entirely
    }
    else
    {
        // Compute variance-based clamping to reject outliers (fireflies)
        float3 mean = m1Prev;
        float3 variance = max(m2Prev - mean * mean, 0.0.xxx);
        float3 sigma = sqrt(variance + 1e-6.xxx);

        float k = gColorSigma; // Controls how aggressive clamping is (2-4 typical)
        float3 lo = mean - k * sigma;
        float3 hi = mean + k * sigma;

        // Clamp current sample to reject fireflies
        C = clamp(C, lo, hi);
    }

    // Update moments with exponential moving average
    float3 m1 = lerp(m1Prev, C, alpha);
    float3 m2 = lerp(m2Prev, C * C, alpha);

    // Blend history with clamped current sample
    float3 accumulated = lerp(history, C, alpha);

    // Output
    TARadiance[coord] = float4(accumulated, 1.0f);
    FirstMomentNew[coord] = float4(m1, 1.0f);
    SecondMomentNew[coord] = float4(m2, 1.0f);
}