#pragma once
#include <array>
#include "RLTypes.h"

class RLController
{
public:
    static constexpr int NumStates = 9;
    static constexpr int NumActions = 3;

    void Initialize();
    RLAction SelectAction(int state);
    void Update(int state, RLAction action, float reward, int nextState);

    float GetQValue(int state, int action) const;

    float Alpha = 0.1f;
    float Gamma = 0.9f;
    float Epsilon = 0.1f;

private:
    std::array<std::array<float, NumActions>, NumStates> m_Q{};
};
