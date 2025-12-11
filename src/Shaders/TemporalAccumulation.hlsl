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

Texture2D<float4> Input : register(t0); // current HDR

Texture2D<float4> FirstMomentOld : register(t3); 
Texture2D<float4> SecondMomentOld : register(t4);

RWTexture2D<float4> Output : register(u0);
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
    float3 m1Prev = FirstMomentOld[coord].rgb; 
    float3 m2Prev = SecondMomentOld[coord].rgb;

    bool hasHistory = any(m1Prev != 0.0.xxx);

    if (useHistory == 0)
        hasHistory = false;
    
    if (!hasHistory)
    {
        m1Prev = C;
        m2Prev = C * C;
    }
    else
    {
        float3 mean = m1Prev;

        float3 variance = m2Prev - mean * mean;
        variance = max(variance, 0.0.xxx); // numerical safety

        float3 sigma = sqrt(variance + 1e-6.xxx); // avoid zero

        float k = gColorSigma;

        float3 lo = mean - k * sigma;
        float3 hi = mean + k * sigma;

        C = clamp(C, lo, hi);
    }

    float alpha = 0.05f; // tune in 0.05-0.2 range

    float3 m1 = lerp(m1Prev, C, alpha);
    float3 m2 = lerp(m2Prev, C * C, alpha);

    float3 newHistory = m1; // use mean as temporally filtered color

    Output[coord] = float4(newHistory, 1.0f);
    FirstMomentNew[coord] = float4(m1, 1.0f);
    SecondMomentNew[coord] = float4(m2, 1.0f);
}
