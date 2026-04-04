#include "SamplingModes.h"

SamplingModeParams GetSamplingModeParams(SamplingMode mode)
{
    switch (mode)
    {
    case SamplingMode::BSDFHeavy:  return { 0.9f, 0.1f };
    case SamplingMode::BSDFGentle:  return { 0.7f, 0.3f };
    case SamplingMode::Balanced:   return { 0.5f, 0.5f };
    case SamplingMode::LightGentle: return { 0.3f, 0.7f };
    case SamplingMode::LightHeavy: return { 0.1f, 0.9f };
    default:                       return { 0.5f, 0.5f };
    }
}