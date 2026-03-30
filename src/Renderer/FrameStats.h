#pragma once
#include "DirectXMath.h"
#include <vector>

struct FrameStats
{
    int Bounce = 0;
    bool IsReflective = false;
    bool IsRefractive = false;
    float Roughness = 1.0f;
    float CosTheta = 1.0f;
    float ThroughputLuminance = 0.0f;
};

FrameStats ComputeFrameStats(const std::vector<DirectX::XMFLOAT4>& image, int iteration);