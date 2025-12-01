#ifndef MICROFACET_BRDF_UTILS_HLSL
#define MICROFACET_BRDF_UTILS_HLSL


#define MaxLights 16

struct Light
{
    float3 Strength;
    float FalloffStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
};

float RoughnessToAlpha(float r)
{
    r = saturate(r);
    return r * r;
}

float GGX_D(float NdotH, float roughness)
{
    float a = RoughnessToAlpha(roughness);
    float a2 = a * a;
    float nh2 = NdotH * NdotH;

    float denom = nh2 * (a2 - 1.0) + 1.0;
    denom = 3.14159 * denom * denom;

    return a2 / max(denom, 1e-6);
}

float G1_SchlickGGX(float NdotX, float roughness)
{
    float a = RoughnessToAlpha(roughness);
    // k choice from Epic/Disney style remap:
    float k = (a + 1.0);
    k = (k * k) / 8.0; 

    return NdotX / (NdotX * (1.0 - k) + k);
}

float GGX_G_Smith(float NdotV, float NdotL, float roughness)
{
    float gv = G1_SchlickGGX(NdotV, roughness);
    float gl = G1_SchlickGGX(NdotL, roughness);
    return gv * gl;
}

float3 Fresnel_Schlick(float3 F0, float cosTheta)
{
    float m = saturate(1.0 - cosTheta);
    float m2 = m * m;
    float m5 = m2 * m2 * m;
    return F0 + (1.0 - F0) * m5;
}


float DisneyDiffuse(float NdotV, float NdotL, float LdotH, float roughness)
{
    NdotV = saturate(NdotV);
    NdotL = saturate(NdotL);
    LdotH = saturate(LdotH);

    float FD90 = 0.5 + 2.0 * LdotH * LdotH * roughness;

    float oneMinusNV = 1.0 - NdotV;
    float oneMinusNL = 1.0 - NdotL;

    float FV = pow(oneMinusNV, 5.0);
    float FL = pow(oneMinusNL, 5.0);

    float Fd = (1.0 + (FD90 - 1.0) * FV) * (1.0 + (FD90 - 1.0) * FL);

    return Fd * (1.0 / 3.14159);
}

void ComputeDisneyMetalWorkflow(float3 baseColor, float metallic, out float3 Cd, out float3 F0)
{
    metallic = saturate(metallic);

    float3 dielectricF0 = float3(0.04, 0.04, 0.04);

    F0 = lerp(dielectricF0, baseColor, metallic);

    Cd = baseColor * (1.0 - metallic);
}

#endif