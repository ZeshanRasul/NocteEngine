#pragma once

#include "RLController.h"
#include "../Renderer/FrameStats.h"
#include "../Renderer/RenderSettings.h"

class Renderer;

class RLExperiment
{
public:
    void Initialize();
    void BeginRun();
    void Step(Renderer& renderer);
    void EndRun();

    bool Enabled = true;
    int MaxIterations = 32;

private:
    RLController m_Controller;
    float m_PreviousVariance = 0.0f;
    int m_CurrentIteration = 0;
};