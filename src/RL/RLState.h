#pragma once

#include "../Renderer/FrameStats.h"

struct DiscreteState
{
    int VarianceBucket = 0;
    int ProgressBucket = 0;

    int ToIndex() const
    {
		return ProgressBucket * 3 + VarianceBucket;
    };
};

DiscreteState BucketizeState(const FrameStats& stats, int maxIterations);

const char* GetVarianceBucketName(int bucket);
const char* GetProgressBucketName(int bucket);