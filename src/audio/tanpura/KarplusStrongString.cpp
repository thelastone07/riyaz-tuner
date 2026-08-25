#include "KarplusStrongString.h"
#include <cmath>
#include <cstdlib>

KarplusStrongString::KarplusStrongString()
{
    delayLine.reserve (kMaxDelayLineLength);
}

void KarplusStrongString::pluck (float frequencyHz, double sampleRateIn, float amplitude)
{
    const float clampedFrequency = juce::jmax (kMinFrequencyHz, frequencyHz);
    const int delayLineLength = juce::jmax (2, (int) std::lround (sampleRateIn / (double) clampedFrequency));
    jassert ((size_t) delayLineLength <= kMaxDelayLineLength);
    delayLine.assign ((size_t) delayLineLength, 0.0f);

    for (auto& sample : delayLine)
        sample = amplitude * (2.0f * ((float) std::rand() / (float) RAND_MAX) - 1.0f);

    readPos = 0;
    samplesSincePluck = 0;
    ringing = true;
}

float KarplusStrongString::renderNextSample()
{
    if (! ringing || delayLine.empty())
        return 0.0f;

    const float out = delayLine[readPos];
    const size_t nextPos = (readPos + 1) % delayLine.size();

    delayLine[readPos] = kDamping * 0.5f * (delayLine[readPos] + delayLine[nextPos]);
    readPos = nextPos;

    if (++samplesSincePluck >= kMaxRingSamples)
        ringing = false;

    return out;
}

bool KarplusStrongString::isRinging() const
{
    return ringing;
}
