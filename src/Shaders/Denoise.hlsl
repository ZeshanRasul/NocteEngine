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

Texture2D<float4> Input : register(t0);
Texture2D<float4> Normal : register(t1);
Texture2D<float> Depth : register(t2);
Texture2D<float4> FirstMomentOld : register(t3);
Texture2D<float4> SecondMomentOld : register(t4);
Texture2D<float4> TARadiance : register(t7);
Texture2D<float4> Albedo : register(t8);

RWTexture2D<float4> Output : register(u0);
RWTexture2D<float4> FirstMomentNew : register(u1);
RWTexture2D<float4> SecondMomentNew : register(u2);
//RWTexture2D<float4> PongOut : register(u1);
//RWTexture2D<float4> PresentOut : register(u2);

static const float gKernel[5] = { 1.0 / 16.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0 };
static const int gOffsets[5] = { -2, -1, 0, 1, 2 };

float3 SafeAlbedo(float3 a)
{
    return max(a, float3(0.04f, 0.04f, 0.04f));
}

float3 LinearToSRGB(float3 x)
{
    const float a = 0.055f;
    const float threshold = 0.0031308f;

    // Clamp to [0, +inf) to avoid weirdness with negative values
    x = max(x, 0.0f.xxx);

    float3 lo = 12.92f * x;
    float3 hi = (1.0f + a) * pow(x, 1.0f / 2.4f) - a;

    // step(edge, x) returns 0.0 if x < edge, 1.0 otherwise, per component
    float3 mask = step(threshold.xxx, x);

    // For each channel: x < threshold ? lo : hi
    return lerp(lo, hi, mask);
}

float3 ApplyExposure(float3 color, float exposure)
{
    return color * exp2(exposure);
}

float3 ToneMapReinhard(float3 x)
{
    return x / (1.0f + x);
}

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 ToneMapACES(float3 color)
{
    color = RRTAndODTFit(color);
    return saturate(color);

}

float3 PostProcessColor(float3 hdrColor)
{
    float3 color = ApplyExposure(hdrColor, Exposure);

    color = ToneMapACES(color);

    color = LinearToSRGB(color);
    
    return color;
}

[numthreads(8, 8, 1)]

    void CSMain
    (
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 coord = int2(dispatchThreadId.xy);

    if (useDenoising == 0)
    {
        float4 color = Input[coord];
        if (IsLastPass == 1)
        {
            color.rgb = PostProcessColor(color.rgb);
            Output[coord] = color;
        }
        else
        {
            Output[coord] = Input[coord];
        }

        return;
    }
    
    if (Normal[coord].w > 0.0f)
    {
        if (passNum == 0)
        {
            Output[coord] = Input[coord];
            return;
        }
        else
        {
            Output[coord] = Input[coord];
            return;
        }
    }
    
    
    int2 dim;
    Input.GetDimensions(dim.x, dim.y);
    
    if (coord.x < 0 || coord.y < 0 || coord.x >= dim.x || coord.y >= dim.y)
        return;
    float4 centerColor = 0.0f;
    if (passNum == 0)
    {
        centerColor = Input[coord];
    }
    else
    {
        centerColor = Input[coord];
    }
    
    float4 centerN = Normal[coord];
    float centerDepth = Depth[coord];
    float4 centerAlbedo4 = Albedo[coord];
    float3 centerAlbedo = centerAlbedo4.rgb;
    
    float3 centerBase = SafeAlbedo(centerAlbedo);
    float3 centerDemod = centerColor.rgb / centerBase;
    
    centerDemod = clamp(centerDemod, 0.0f, 10.0f);
    
    float3 N0 = normalize(centerN.xyz * 2.0f - 1.0f);
    float Z0 = centerDepth;

    float3 sum = 0.0f;
    float wsum = 0.0f;

    float centerLum = dot(centerDemod, float3(0.299, 0.587, 0.114));
    float3 m1 = FirstMomentOld[coord].rgb;
    float3 m2 = SecondMomentOld[coord].rgb;

    [unroll]
    for (int j = 0; j < 5; ++j)
    {
        [unroll]
        for (int i = 0; i < 5; ++i)
        {
            int2 off = int2(gOffsets[i], gOffsets[j]) * gStepSize;
            int2 p = coord + off;

            if (p.x < 0 || p.y < 0 || p.x >= dim.x || p.y >= dim.y)
                continue;

            float4 c = 0.0f;
            
            if (passNum == 0)
            {
                c = Input[p];
            }
            else
            {
                c = Input[p];
            }
            
            
            float4 n = Normal[p];
            float z = Depth[p];
            float3 albedoI = Albedo[p].rgb;
            float3 albedoSafeI = SafeAlbedo(albedoI);
            float3 cDemod = c.rgb / albedoSafeI;

            float3 Ni = normalize(n.xyz * 2.0f - 1.0f);
            float Zi = z;

            float k = gKernel[i] * gKernel[j];

            // Normal weight
            float nDiff2 = max(0.0f, 1.0f - dot(N0, Ni)); // 0 if same direction

            // Optional color/luminance weight
            float lum = dot(cDemod, float3(0.299, 0.587, 0.114));
            float dl = abs(lum - centerLum);
        //    float lW = exp(-dl * gColorSigma);


            // Depth weight (difference in depth)
            float dz = abs(Zi - Z0);
            float nW = exp(-(nDiff2 * nDiff2) / (2.0 * gNormalSigma * gNormalSigma));
            float zW = exp(-(dz * dz) / (2.0 * gDepthSigma * gDepthSigma));
            float3 da = albedoI - centerAlbedo;
            float albedoDiff2 = dot(da, da);
            float aW = 1.0f;

            float3 variance = max(m2 - m1 * m1, 0.0f);
            float varScalar = max(dot(variance, float3(0.3333, 0.3333, 0.3333)), 1e-6f);

            float varNorm = sqrt(varScalar);
            varNorm = min(varNorm, 1.0f);

            float sigma = gColorSigma * (1.0f + 0.5f * varNorm);
            float sigmaSafe = max(sigma, 1e-4f);
            
            float lW = exp(-(dl * dl) / (2.0 * sigmaSafe * sigmaSafe));
            float w = k * nW * zW * lW * aW;
            sum += cDemod.rgb * w;
            wsum += w;
        }
    }

    float3 filteredDemod = (wsum > 0.0f) ? (sum / wsum) : centerDemod;
    float3 result = filteredDemod * centerBase;
    
        

    if (IsLastPass == 1)
    {
   //     result = PostProcessColor(result);
        Output[coord] = float4(result, centerColor.a);
    }
    else
    {
        Output[coord] = float4(result, centerColor.a);
    }
    //float4 n = Normal[coord];
    //float mask = n.w > 0.5f ? 1.0f : 0.0f;
    //Output[coord] = float4(mask, mask, mask, 1.0f);
}

