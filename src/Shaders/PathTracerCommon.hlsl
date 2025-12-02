#ifndef PATHTRACER_COMMON_HLSL
#define PATHTRACER_COMMON_HLSL

#include "Common.hlsl"
#include "MicrofacetBRDFUtils.hlsl"

//---------------------------------------------------------------------
// Payload
//---------------------------------------------------------------------
struct PathPayload
{
    float3 radiance; // unused in this minimal core, but handy later
    float3 throughput; // path throughput
    float3 hitPos; // world-space hit
    float3 normal; // shading normal
    float3 wi; // sampled next direction
    float3 emission; // emission at this hit (or env)
    float3 bsdfOverPdf; // f * cosTheta / pdf

    float pdf; // last sample pdf (optional, for debugging)
    uint depth; // bounce depth
    uint done; // 0 = continue, 1 = terminate
    uint seed; // RNG state
    uint pad; // keep payload size aligned
};

struct Attributes
{
    float2 bary;
};

//---------------------------------------------------------------------
// RNG
//---------------------------------------------------------------------

// Simple integer hash
uint Hash(uint x)
{
    x ^= x >> 17;
    x *= 0xed5ad4bbu;
    x ^= x >> 11;
    x *= 0xac4c1b51u;
    x ^= x >> 15;
    x *= 0x31848babu;
    x ^= x >> 14;
    return x;
}

// Advance RNG and return float in [0,1)
//float Rand(inout uint state)
//{
//    state = Hash(state);
//    return (state & 0x00FFFFFFu) / 16777216.0f; // 2^24
//}

//float2 Rand2(inout uint state)
//{
//    return float2(Rand(state), Rand(state));
//}

float Rand(inout uint state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (float(state) * (1.0 / 4294967296.0)); // [0,1)
}

float2 Rand2(inout uint state)
{
    return float2(Rand(state), Rand(state));
}

//---------------------------------------------------------------------
// Utility: transform normal to world (depends on how you store matrices)
// Here we assume WorldToObject3x4() is inverse(world), and world is rigid.
//---------------------------------------------------------------------
float3 TransformNormalToWorld(float3 nObj)
{
    float3x3 objToWorld = (float3x3) ObjectToWorld3x4();
    float3 N = normalize(mul(nObj, objToWorld));
    return N;
}

#endif // PATHTRACER_COMMON_HLSL