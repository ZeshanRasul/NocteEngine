#include "RLState.h"

#include <cmath>

enum class SurfaceClass
{
    Diffuse = 0,
    Reflective = 1,
    Refractive = 2
};

namespace
{
    int BucketizeBounce(int bounce)
    {
        if (bounce <= 1) return 0;
        if (bounce <= 3) return 1;
        return 2;
    }

    int BucketizeSurfaceClass(bool isRefractive, bool isReflective, float roughness)
    {
        if (isRefractive)
            return static_cast<int>(SurfaceClass::Refractive);

        if (isReflective || roughness < 0.08f)
            return static_cast<int>(SurfaceClass::Reflective);

        return static_cast<int>(SurfaceClass::Diffuse);
    }

    int BucketizeCosTheta(float cosTheta)
    {
        cosTheta = std::abs(cosTheta);

        if (cosTheta < 0.25f) return 0;
        if (cosTheta < 0.75f) return 1;
        return 2;
    }

    int BucketizeThroughput(float throughputLum)
    {
        if (throughputLum < 0.1f) return 0;
        if (throughputLum < 0.5f) return 1;
        return 2;
    }

    int BucketizeRoughness(float roughness)
    {
        if (roughness < 0.05f) return 0;
        if (roughness < 0.3f) return 1;
        return 2;
    }
}

DiscreteState BucketizeState(const FrameStats& stats, int maxIterations)
{
    (void)maxIterations;

    DiscreteState state;
    state.BounceBucket = BucketizeBounce(stats.Bounce);
    state.SurfaceClassBucket = BucketizeSurfaceClass(
        stats.IsRefractive,
        stats.IsReflective,
        stats.Roughness
    );
    state.CosThetaBucket = BucketizeCosTheta(stats.CosTheta);
    state.ThroughputBucket = BucketizeThroughput(stats.ThroughputLuminance);
    state.RoughnessBucket = BucketizeRoughness(stats.Roughness);

    return state;
}

const char* GetBounceStateName(const DiscreteState& state)
{
    switch (state.BounceBucket)
    {
    case 0: return "Early Bounce";
    case 1: return "Mid Bounce";
    case 2: return "Late Bounce";
    default: return "Unknown Bounce";
    }
}

const char* GetSurfaceClassStateName(const DiscreteState& state)
{
    switch (state.SurfaceClassBucket)
    {
    case 0: return "Diffuse";
    case 1: return "Reflective";
    case 2: return "Refractive";
    default: return "Unknown Surface";
    }
}

const char* GetCosThetaStateName(const DiscreteState& state)
{
    switch (state.CosThetaBucket)
    {
    case 0: return "Grazing Angle";
    case 1: return "Moderate Angle";
    case 2: return "Near Normal";
    default: return "Unknown Angle";
    }
}

const char* GetThroughputStateName(const DiscreteState& state)
{
    switch (state.ThroughputBucket)
    {
    case 0: return "Low Throughput";
    case 1: return "Medium Throughput";
    case 2: return "High Throughput";
    default: return "Unknown Throughput";
    }
}

const char* GetRoughnessStateName(const DiscreteState& state)
{
    switch (state.RoughnessBucket)
    {
    case 0: return "Smooth";
    case 1: return "Glossy";
    case 2: return "Rough";
    default: return "Unknown Roughness";
    }
}
