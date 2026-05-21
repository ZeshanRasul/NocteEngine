Texture2D<float4> AccumInput : register(t0);
RWTexture2D<float4> Output : register(u0);

cbuffer PostProcess : register(b1)
{
    float Exposure;
    int ToneMapMode;
    int DebugMode;
    int IsLastPass;
    int AccumulatedSPP;
    float3 pad;
}

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 ToneMapACES(float3 color)
{
    return saturate(RRTAndODTFit(color));
}

float3 ToneMapReinhard(float3 color)
{
    return color / (1.0f + color);
}

float3 LinearToSRGB(float3 x)
{
    x = max(x, 0.0f);
    float3 lo = 12.92f * x;
    float3 hi = 1.055f * pow(x, 1.0f / 2.4f) - 0.055f;
    return lerp(lo, hi, step(0.0031308f, x));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    float3 hdr = AccumInput[dtid.xy].rgb;
    hdr /= max((float) AccumulatedSPP, 1.0);

    // EV-based exposure (matches Denoise.hlsl)
    hdr *= exp2(Exposure);

    float3 mapped;
    if (ToneMapMode == 0)
        mapped = ToneMapReinhard(hdr);
    else
        mapped = ToneMapACES(hdr);

    Output[dtid.xy] = float4(LinearToSRGB(mapped), 1.0);
}
