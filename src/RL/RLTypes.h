#pragma once

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