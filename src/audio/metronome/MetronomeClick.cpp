#include "MetronomeClick.h"
#include <juce_core/juce_core.h>
#include <cmath>

void MetronomeClick::trigger (BeatType type, double sampleRateIn)
{
    // Initialize with Plain's values as a safe fallback for corrupted/invalid enum values,
    // matching TaalPattern.cpp's exhaustive-switch idiom. The switch below must handle all
    // real BeatType enumerators; if a new type is added, the compiler will warn unless the
    // switch is updated.
    float frequencyHz = 700.0f;
    float initialAmplitude = 0.30f;
    float durationSeconds = 0.020f;

    switch (type)
    {
        case BeatType::Sam:   frequencyHz = 1200.0f; initialAmplitude = 1.00f; durationSeconds = 0.050f; break;
        case BeatType::Clap:  frequencyHz = 900.0f;  initialAmplitude = 0.75f; durationSeconds = 0.040f; break;
        case BeatType::Khali: frequencyHz = 500.0f;  initialAmplitude = 0.45f; durationSeconds = 0.035f; break;
        case BeatType::Plain: frequencyHz = 700.0f;  initialAmplitude = 0.30f; durationSeconds = 0.020f; break;
    }

    phase = 0.0;
    phaseIncrement = 2.0 * juce::MathConstants<double>::pi * (double) frequencyHz / sampleRateIn;
    amplitude = initialAmplitude;
    samplesRemaining = juce::jmax (1, (int) (sampleRateIn * (double) durationSeconds));

    // decayPerSample is derived so the envelope reaches -60dB (0.001x)
    // exactly at samplesRemaining==0, computed from the ACTUAL sampleRateIn
    // passed here (never a baked-in rate) - this is what keeps the cutoff
    // from ever truncating an audible tail, the exact mistake
    // KarplusStrongString's original kMaxRingSamples made.
    decayPerSample = (float) std::pow (0.001, 1.0 / (double) samplesRemaining);
}

float MetronomeClick::renderNextSample()
{
    if (samplesRemaining <= 0)
        return 0.0f;

    const float out = amplitude * (float) std::sin (phase);

    phase += phaseIncrement;
    if (phase >= 2.0 * juce::MathConstants<double>::pi)
        phase -= 2.0 * juce::MathConstants<double>::pi;

    amplitude *= decayPerSample;
    --samplesRemaining;

    return out;
}

bool MetronomeClick::isSounding() const
{
    return samplesRemaining > 0;
}
