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

    // The +0.5 is not a rounding convention - it is a tuning correction. In
    // this in-place filter formulation slot i is updated from slots i and
    // i+1, so the loop delay is N - 0.5 samples, not N, and the string
    // resonates at sr/(N - 0.5): always ABOVE the requested frequency (up to
    // +15 cents at realistic tonics). Adding 0.5 before rounding makes the
    // N - 0.5 loop delay straddle the target instead of always undershooting
    // it, removing the systematic sharp bias and halving the worst-case error
    // to roughly +-8 cents. The proper fix - a fractional (interpolated) read
    // position so the loop delay equals sampleRate/frequencyHz exactly - is
    // deferred to TODOS.md.
    int delayLineLength = juce::jmax (2, (int) std::lround (sampleRateIn / (double) clampedFrequency + 0.5));

    // Defence in depth for the no-allocation-on-the-audio-thread property.
    // The jassert below catches a violation loudly in Debug, but it compiles
    // away entirely in Release, so the safety property must not depend on it:
    // clamp instead, so even a sample rate beyond kMaxSupportedSampleRate
    // degrades to a wrong pitch rather than a reallocating assign().
    jassert ((size_t) delayLineLength <= kMaxDelayLineLength);
    delayLineLength = juce::jmin (delayLineLength, (int) kMaxDelayLineLength);

    delayLine.assign ((size_t) delayLineLength, 0.0f);

    for (auto& sample : delayLine)
        sample = amplitude * (2.0f * ((float) std::rand() / (float) RAND_MAX) - 1.0f);

    readPos = 0;
    samplesSincePluck = 0;
    // A duration, not a fixed sample count - see kMaxRingSeconds.
    maxRingSamples = (int) (sampleRateIn * (double) kMaxRingSeconds);
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

    if (++samplesSincePluck >= maxRingSamples)
        ringing = false;

    return out;
}

bool KarplusStrongString::isRinging() const
{
    return ringing;
}
