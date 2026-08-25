#include "TanpuraSynth.h"
#include <cmath>

namespace
{
    float centsToFrequency (float saHz, float cents)
    {
        return saHz * std::pow (2.0f, cents / 1200.0f);
    }
}

void TanpuraSynth::prepare (double sampleRateIn, float saHz, TanpuraTuning tuning, float secondsBetweenPlucks)
{
    sampleRate = sampleRateIn;
    samplesPerPluck = sampleRate * (double) secondsBetweenPlucks;
    samplesSinceLastPluck = 0.0;
    nextStringIndex = 0;

    const float firstStringCents = (tuning == TanpuraTuning::PaSaSaSa) ? -500.0f : -700.0f;
    stringFrequencies[0] = centsToFrequency (saHz, firstStringCents); // Pa or Ma, mandra
    stringFrequencies[1] = centsToFrequency (saHz, -1200.0f);         // Sa, mandra
    stringFrequencies[2] = centsToFrequency (saHz, 0.0f);             // Sa, madhya
    stringFrequencies[3] = centsToFrequency (saHz, 0.0f);             // Sa, madhya

    prepared = true;
}

float TanpuraSynth::renderNextSample()
{
    if (! prepared)
        return 0.0f;

    if (samplesSinceLastPluck <= 0.0)
    {
        strings[(size_t) nextStringIndex].pluck (stringFrequencies[(size_t) nextStringIndex], sampleRate);
        nextStringIndex = (nextStringIndex + 1) % kNumStrings;
        samplesSinceLastPluck = samplesPerPluck;
    }
    samplesSinceLastPluck -= 1.0;

    float mixed = 0.0f;
    for (auto& s : strings)
        mixed += s.renderNextSample();

    return mixed / (float) kNumStrings;
}
