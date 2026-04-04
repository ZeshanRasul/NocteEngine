#include "SamplingModes.h"

SamplingModeParams GetSamplingModeParams(SamplingMode mode)
{
    switch (mode)
    {
    case SamplingMode::BSDFHeavy:  return { 0.9f, 0.35f };
    case SamplingMode::Balanced:   return { 0.5f, 0.75f };
    case SamplingMode::LightHeavy: return { 0.1f, 1.0f };
    default:                       return { 0.5f, 0.75f };
    }
}