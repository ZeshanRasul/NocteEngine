#pragma once
#include <array>
#include "RLTypes.h"
#include <cstdint>

class RLController
{
public:
    static constexpr int NumStates = 729;
    static constexpr int NumActions = 5;

	RLController() = default;

    void Initialize(float initialQValue = 0.0f);

    RLAction SelectAction(int stateIndex);
    void Update(int stateIndex, RLAction action, float reward, int nextStateIndex);

    float GetQValue(int stateIndex, int actionIndex) const
    {
		return m_Q[stateIndex][actionIndex];
    }

    float GetQValue(int stateIndex, RLAction action) const
    {
        return GetQValue(stateIndex, static_cast<int>(action));
	}

	void SetAlpha(float alpha) { m_Alpha = alpha; }
	float GetAlpha() const { return m_Alpha; }

	void SetGamma(float gamma) { m_Gamma = gamma; }
	float GetGamma() const { return m_Gamma; }

	void SetEpsilon(float epsilon) { m_Epsilon = epsilon; }
	float GetEpsilon() const { return m_Epsilon; }

    int ActionToIndex(RLAction action) const
    {
        return static_cast<int>(action);
	}

    RLAction IndexToAction(int index) const
    {
        return static_cast<RLAction>(index);
    }
    
    inline int GetSPPBucket(int spp)
    {
        if (spp < 16) return 0;
        else if (spp < 64) return 1;
        else return 2;
    }

private:

    int GetBestActionIndex(int stateIndex) const
    {
        int bestActionIndex = 0;
        float bestQValue = m_Q[stateIndex][0];
        for (int actionIndex = 1; actionIndex < NumActions; ++actionIndex)
        {
            if (m_Q[stateIndex][actionIndex] > bestQValue)
            {
                bestQValue = m_Q[stateIndex][actionIndex];
                bestActionIndex = actionIndex;
            }
        }
        return bestActionIndex;
	}

private:
    float m_Alpha = 0.1f;
    float m_Gamma = 0.9f;
    float m_Epsilon = 0.05f;

    std::array<std::array<float, NumActions>, NumStates> m_Q{};
};
