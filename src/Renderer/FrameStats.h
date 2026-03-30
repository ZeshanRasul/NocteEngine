#pragma once
#include "DirectXMath.h"
#include <vector>

struct FrameStats
{
    float MeanLuminance = 0.0f;
    float LuminanceVariance = 0.0f;
    float LogLuminanceVariance = 0.0f;
    float BrightPixelRatio = 0.0f;
    int Iteration = 0;
};

inline float ComputeLuminance(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

FrameStats ComputeFrameStats(const std::vector<DirectX::XMFLOAT4>& image, int iteration);