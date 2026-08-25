#pragma once
#include <vector>
#include <juce_core/juce_core.h>

// A single plucked-string voice using the Karplus-Strong algorithm: a burst
// of noise circulates through a damped delay line. The delay line's length
// (sampleRate / frequencyHz) sets the fundamental pitch; the damping factor
// applied each pass sets how quickly the string decays to silence.
class KarplusStrongString
{
public:
    void pluck (float frequencyHz, double sampleRateIn, float amplitude = 1.0f);
    float renderNextSample();
    bool isRinging() const;

private:
    static constexpr float kMinFrequencyHz = 20.0f; // Guard against division-by-zero
    static constexpr float kDamping = 0.996f;
    static constexpr int kMaxRingSamples = 44100 * 4; // ~4s upper bound - real decay is usually faster

    std::vector<float> delayLine;
    size_t readPos = 0;
    int samplesSincePluck = 0;
    bool ringing = false;
};
