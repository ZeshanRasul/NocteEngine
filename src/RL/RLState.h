#pragma once

#include "../Renderer/FrameStats.h"

struct DiscreteState
{
    int VarianceBucket = 0;
    int ProgressBucket = 0;
    int LuminanceBucket = 0;
    int BrightPixelRatioBucket = 0;

    int ToIndex() const
    {
        return VarianceBucket
            + 5 * ProgressBucket
            + 5 * 3 * LuminanceBucket
            + 5 * 3 * 3 * BrightPixelRatioBucket;
    };
};

DiscreteState BucketizeState(const FrameStats& stats, int maxIterations);

const char* GetVarianceBucketName(int bucket);
const char* GetProgressBucketName(int bucket);
const char* GetLuminanceBucketName(int bucket);
const char* GetBrightPixelRatioBucketName(int bucket);