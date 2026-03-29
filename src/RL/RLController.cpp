#include "RLController.h"

void RLController::Initialize(float initialQValue)
{
	for (auto& row : m_Q)
	{
		row.fill(initialQValue);
	}
}


RLAction RLController::SelectAction(int stateIndex)
{
	// Epsilon-greedy action selection
	if (static_cast<float>(rand()) / RAND_MAX < m_Epsilon)
	{
		// Explore: select a random action
		int randomActionIndex = rand() % NumActions;
		return IndexToAction(randomActionIndex);
	}
	else
	{
		// Exploit: select the best action
		int bestActionIndex = GetBestActionIndex(stateIndex);
		return IndexToAction(bestActionIndex);
	}
}

void RLController::Update(int stateIndex, RLAction action, float reward, int nextStateIndex)
{
	const int actionIndex = ActionToIndex(action);

	const float currentQ = m_Q[stateIndex][actionIndex];

	float maxNextQ = m_Q[nextStateIndex][0];
	for (int a = 1; a < NumActions; ++a)
	{
		maxNextQ = std::max(maxNextQ, m_Q[nextStateIndex][a]);
	}
	const float tdTarget = reward + m_Gamma * maxNextQ;
	const float tdError = tdTarget - currentQ;

	m_Q[stateIndex][actionIndex] = currentQ + m_Alpha * tdError;
}