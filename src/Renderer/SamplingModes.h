#pragma once

#include "RenderSettings.h"

struct SamplingModeParams
{
    float BsdfProbability;
    float LightProbability;
};

SamplingModeParams GetSamplingModeParams(SamplingMode mode);