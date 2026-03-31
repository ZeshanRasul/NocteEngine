#pragma once
#include "../Renderer/RenderSettings.h"

struct RLTransitionGPU
{
    uint32_t StateIndex = 0;
    uint32_t ActionIndex = 0;
    float Reward = 0.0f;
	uint32_t NextStateIndex = 0;
    uint32_t Valid = 0;
	uint32_t Terminated = 0;
};

struct RLQValue
{
    float Value;
};

enum class RLAction
{
    BSDFHeavy = 0,
    Balanced = 1,
    LightHeavy = 2,
    Count
};

struct RLStepResult
{
    int State = 0;
    int NextState = 0;
    RLAction Action = RLAction::Balanced;
    float Reward = 0.0f;
};

inline const char* GetActionName(RLAction action)
{
    switch (action)
    {
    case RLAction::BSDFHeavy:  return "BSDFHeavy";
    case RLAction::Balanced:   return "Balanced";
    case RLAction::LightHeavy: return "LightHeavy";
    default:                   return "Unknown";
    }
}

inline SamplingMode ToSamplingMode(RLAction action)
{
    switch (action)
    {
    case RLAction::BSDFHeavy:  return SamplingMode::BSDFHeavy;
    case RLAction::Balanced:   return SamplingMode::Balanced;
    case RLAction::LightHeavy: return SamplingMode::LightHeavy;
    default:                   return SamplingMode::Balanced;
    }
}