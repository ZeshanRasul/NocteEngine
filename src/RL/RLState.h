#pragma once

#include "../Renderer/FrameStats.h"

struct DiscreteState
{
    int BounceBucket = 0;
    int SurfaceClassBucket = 0;
    int CosThetaBucket = 0;
    int ThroughputBucket = 0;
    int RoughnessBucket = 0;

    int ToIndex() const
    {
        return BounceBucket +
            3 * SurfaceClassBucket +
            9 * CosThetaBucket +
            27 * ThroughputBucket +
            81 * RoughnessBucket;
    }
};

DiscreteState BucketizeState(const FrameStats& stats, int maxIterations);

const char* GetBounceStateName(const DiscreteState& state);
const char* GetSurfaceClassStateName(const DiscreteState& state);
const char* GetCosThetaStateName(const DiscreteState& state);
const char* GetThroughputStateName(const DiscreteState& state);
const char* GetRoughnessStateName(const DiscreteState& state);
