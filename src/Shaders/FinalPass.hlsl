Texture2D<float4> AccumInput : register(t0);
RWTexture2D<float4> Output : register(u0);

cbuffer PostProcess : register(b0)
{
    float Exposure;
    int AccumulatedSPP;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    float3 hdr = AccumInput[dtid.xy].rgb;
 //   hdr /= max((float) AccumulatedSPP, 1.0);
    hdr *= Exposure;

    float3 mapped = hdr / (1.0 + hdr);
    mapped = pow(mapped, 1.0 / 2.2);

    Output[dtid.xy] = float4(mapped, 1.0);
}