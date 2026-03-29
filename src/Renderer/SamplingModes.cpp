#include "SamplingModes.h"

SamplingModeParams GetSamplingModeParams(SamplingMode mode)
{
    switch (mode)
    {
    case SamplingMode::BSDFHeavy: return { 0.8f, 0.2f };
    case SamplingMode::Balanced:  return { 0.5f, 0.5f };
    case SamplingMode::LightHeavy:return { 0.2f, 0.8f };
    default:                      return { 0.5f, 0.5f };
    }
}