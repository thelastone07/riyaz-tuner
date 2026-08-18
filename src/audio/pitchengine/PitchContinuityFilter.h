#pragma once
#include "PitchEngine.h"
#include <optional>

class PitchContinuityFilter
{
public:
    PitchFrame process (PitchFrame frame);
    void reset();

private:
    std::optional<float> lastConfidentFrequencyHz;
};
