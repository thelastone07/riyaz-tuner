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
    // The two madhya Sa strings are tuned a few cents apart, not identical.
    // A real tanpura's strings are never perfectly unison - the slow,
    // shimmering beating between very-nearly-identical pitches is a large
    // part of its characteristic sound (the "jhala"/shimmer), not a flaw to
    // eliminate. +-3 cents beats at roughly (3 cents worth of Hz) per
    // second - a few tenths of a Hz at a typical Sa - slow enough to read as
    // shimmer rather than as a chorus effect or an out-of-tune pair.
    stringFrequencies[2] = centsToFrequency (saHz, -3.0f);            // Sa, madhya (slightly flat)
    stringFrequencies[3] = centsToFrequency (saHz, 3.0f);             // Sa, madhya (slightly sharp)

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
