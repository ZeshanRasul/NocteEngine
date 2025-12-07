cbuffer DenoiseParams : register(b0)
{
    float gColorSigma;
    float gNormalSigma;
    float gDepthSigma;
    int gStepSize;
    float2 invResolution;
    int passNum;
    int pad;
}

Texture2D<float4> Input : register(t0);
Texture2D<float4> Normal : register(t1);
Texture2D<float> Depth : register(t2);

RWTexture2D<float4> Output : register(u0);
//RWTexture2D<float4> PongOut : register(u1);
//RWTexture2D<float4> PresentOut : register(u2);

static const float gKernel[5] = { 1.0 / 16.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0 };
static const int gOffsets[5] = { -2, -1, 0, 1, 2 };

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 coord = int2(dispatchThreadId.xy);

    int2 dim;
    Input.GetDimensions(dim.x, dim.y);
    if (coord.x < 0 || coord.y < 0 || coord.x >= dim.x || coord.y >= dim.y)
        return;
    
    float4 centerColor = Input[coord];
    float4 centerN = Normal[coord];
    float centerDepth = Depth[coord];

    // We stored normals encoded to [0,1]; decode to [-1,1]
    float3 N0 = normalize(centerN.xyz * 2.0f - 1.0f);
    float Z0 = centerDepth;

    float3 sum = 0.0f;
    float wsum = 0.0f;

    // Small luminance for range weighting (optional)
    float centerLum = dot(centerColor.rgb, float3(0.299, 0.587, 0.114));

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

            float4 c = Input[p];
            float4 n = Normal[p];
            float z = Depth[p];

            float3 Ni = normalize(n.xyz * 2.0f - 1.0f);
            float Zi = z;

            float k = gKernel[i] * gKernel[j];

            // Normal weight
            float nDiff2 = max(0.0f, 1.0f - dot(N0, Ni)); // 0 if same direction
            float nW = exp(-nDiff2 * gNormalSigma);

            // Optional color/luminance weight
            float lum = dot(c.rgb, float3(0.299, 0.587, 0.114));
            float dl = abs(lum - centerLum);
        //    float lW = exp(-dl * gColorSigma);


            // Depth weight (difference in depth)
            float dz = abs(Zi - Z0);
            nW = exp(-(nDiff2 * nDiff2) / (2.0 * gNormalSigma * gNormalSigma));
            float zW = exp(-(dz * dz) / (2.0 * gDepthSigma * gDepthSigma));
            float lW = exp(-(dl * dl) / (2.0 * gColorSigma * gColorSigma));
            float3 diff = c.rgb - centerColor.rgb;
            float lumDiff = abs(dot(diff, float3(0.299, 0.587, 0.114)));
            if (lumDiff > 0.3f)
                continue;
            
            float w = k * nW * zW * lW;

            sum += c.rgb * w;
            wsum += w;
        }
    }

    float3 result = (wsum > 0.0f) ? (sum / wsum) : centerColor.rgb;

  Output[coord] = float4(result, centerColor.a);
}

