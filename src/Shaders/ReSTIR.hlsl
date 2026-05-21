#ifndef RESTIR_HLSL
#define RESTIR_HLSL

// Reservoir for Resampled Importance Sampling.
// LightIndex < 0  → invalid (no sample selected yet).
// LightIndex == gNumAreaLights → sun/directional light.
struct Reservoir
{
    int    LightIndex;    // index into light pool
    float3 PointOnLight;  // sampled point on area light surface
    int    M;             // candidates seen so far
    float  W_sum;         // running weight sum
    float  W;             // unbiased contribution weight  (1/p_hat) * (W_sum/M)
    float  pad;
};

Reservoir EmptyReservoir()
{
    Reservoir r;
    r.LightIndex   = -1;
    r.PointOnLight = float3(0.0f, 0.0f, 0.0f);
    r.M            = 0;
    r.W_sum        = 0.0f;
    r.W            = 0.0f;
    r.pad          = 0.0f;
    return r;
}

// WRS update.  randomVal must be uniform in [0,1) — caller generates it.
// Returns true when the new candidate is selected.
bool UpdateReservoir(inout Reservoir r,
                     int    lightIndex,
                     float3 pointOnLight,
                     float  w,
                     float  randomVal)
{
    r.W_sum += w;
    r.M     += 1;
    if (randomVal * r.W_sum < w)
    {
        r.LightIndex   = lightIndex;
        r.PointOnLight = pointOnLight;
        return true;
    }
    return false;
}

#endif // RESTIR_HLSL
