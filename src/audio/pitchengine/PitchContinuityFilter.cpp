#include "PitchContinuityFilter.h"
#include <cmath>

namespace
{
    constexpr float kOctaveToleranceCents = 50.0f;

    float centsBetween (float a, float b)
    {
        return 1200.0f * std::log2 (a / b);
    }
}

PitchFrame PitchContinuityFilter::process (PitchFrame frame)
{
    if (! frame.frequencyHz.has_value())
        return frame;

    if (! lastConfidentFrequencyHz.has_value())
    {
        lastConfidentFrequencyHz = *frame.frequencyHz;
        return frame;
    }

    const float cents = centsBetween (*frame.frequencyHz, *lastConfidentFrequencyHz);
    const float nearestOctaveMultiple = std::round (cents / 1200.0f);

    if (nearestOctaveMultiple != 0.0f)
    {
        const float distanceFromMultiple = std::abs (cents - nearestOctaveMultiple * 1200.0f);
        if (distanceFromMultiple < kOctaveToleranceCents)
        {
            const float corrected = *frame.frequencyHz / std::pow (2.0f, nearestOctaveMultiple);
            frame.frequencyHz = corrected;
            lastConfidentFrequencyHz = corrected;
            return frame;
        }
    }

    lastConfidentFrequencyHz = *frame.frequencyHz;
    return frame;
}

void PitchContinuityFilter::reset()
{
    lastConfidentFrequencyHz.reset();
}
