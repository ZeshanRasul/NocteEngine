#ifndef COMMON_HLSL
#define COMMON_HLSL

#include "MicrofacetBRDFUtils.hlsl"

cbuffer PostProcess : register(b3)
{
    float Exposure;
    int ToneMapMode;
    int DebugMode;
    int IsLastPass;
}

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Ior;
    float Reflectivity;
    float3 Absorption;
    float Roughness;
    float pad;
    float pad1;
    float Metallic;
    bool IsReflective;
    uint IsRefractive;
    float pad2;
    int TexIndex;
};

struct ShadowPayload
{
    bool isHit;
};

float3 LinearToSRGB(float3 x)
{
    const float a = 0.055f;
    float3 lo = 12.92f * x;
    float3 hi = (1.0f + a) * pow(x, 1.0f / 2.4f) - a;
    
    return select(lo, hi, x > 0.0031308f);
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


// Van der Corput radical inverse in base 2
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return float(bits) * 2.3283064365386963e-10f; // 1 / 2^32
}

float2 Hammersley2D(uint i, uint N)
{
    float u = (float) i / (float) N;
    float v = RadicalInverse_VdC(i);
    return float2(u, v);
}

float2 SampleHammersley(uint sampleIdx, uint numSamples, uint pixelSeed, uint frameIndex)
{
    uint index = sampleIdx + pixelSeed * 1315423911u + frameIndex * 2654435761u;
    index %= numSamples;

    return Hammersley2D(index, numSamples);
}

float3 CosineSampleHemisphere(float2 xi)
{
    float r = sqrt(xi.x);
    float phi = 2.0f * 3.14159265f * xi.y;

    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(1.0f - xi.x);

    return float3(x, y, z);
}

float3x3 BuildTangentFrame(float3 N)
{
    float3 up = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    return float3x3(T, B, N);
}

float3 SampleGGXVNDF(float3 V, float2 xi, float roughness)
{
    // Convert view to hemisphere coords
    float3 Vh = normalize(float3(roughness * V.x, roughness * V.y, V.z));

    // Build orthonormal basis
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0.0f ? normalize(float3(-Vh.y, Vh.x, 0.0f)) : float3(1.0f, 0.0f, 0.0f);
    float3 T2 = cross(Vh, T1);

    // Sample point with polar coordinates
    float r = sqrt(xi.x);
    float phi = 2.0f * 3.14159265f * xi.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5f * (1.0f + Vh.z);
    t2 = (1.0f - s) * sqrt(1.0f - t1 * t1) + s * t2;

    // Compute sampled normal
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;

    // Transform back
    float3 N = normalize(float3(roughness * Nh.x, roughness * Nh.y, max(0.0f, Nh.z)));

    return N;
}

float GGX_G1(float3 N, float3 V, float alpha)
{
    float NdotV = saturate(dot(N, V));
    float a = alpha;
    float a2 = a * a;

    float lambda = NdotV == 0.0f ? 0.0f :
        (-1.0f + sqrt(1.0f + (a2 - 1.0f) * (1.0f - NdotV * NdotV) / (NdotV * NdotV))) * 0.5f;

    return 1.0f / (1.0f + lambda);
}

float GGX_PDF(float3 N, float3 V, float3 L, float roughness)
{
    float3 H = normalize(V + L);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    float LdotH = saturate(dot(L, H));
    
    if (NdotH <= 0.0f || VdotH <= 0.0f || LdotH <= 0.0f)
        return 0.0f;
    
    float alpha = max(roughness * roughness, 1e-4f);
    float D = GGX_D(NdotH, alpha);
  //  float G1V = GGX_G1(N, V, alpha);


    // Heitz VNDF-based pdf: D * NdotH / (4 * VdotH)
    float pdf = (D * NdotH) / max(4.0f * VdotH, 1e-4f);
    return pdf;
}

float3 PostProcessColor(float3 hdrColor)
{
    float3 color = ApplyExposure(hdrColor, Exposure);

    if (ToneMapMode == 1)
        color = ToneMapReinhard(color);
    else if (ToneMapMode == 2)
        color = ToneMapACES(color);
    else
        color = saturate(color);

    color = LinearToSRGB(color);
    return color;
}

#endif