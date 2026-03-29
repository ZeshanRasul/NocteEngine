#pragma once

#include "../Renderer/FrameStats.h"

struct DiscreteState
{
    int VarianceBucket = 0;
    int ProgressBucket = 0;

    int ToIndex() const;
};

DiscreteState BucketizeState(const FrameStats& stats, int maxIterations);