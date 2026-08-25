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
    KarplusStrongString();

    void pluck (float frequencyHz, double sampleRateIn, float amplitude = 1.0f);
    float renderNextSample();
    bool isRinging() const;

private:
    static constexpr float kMinFrequencyHz = 20.0f; // Guard against division-by-zero
    static constexpr float kDamping = 0.996f;

    // Hard upper bound on how long a single pluck is allowed to ring, as a
    // DURATION (converted to samples at pluck time from the actual sample
    // rate). It must not be a raw sample count: baking 44100 into a constant
    // silently shrinks the window in real time as the device rate rises
    // (a 44100*4 count is only 1.84s at 96kHz).
    //
    // Be honest about what this cutoff is: it is NOT a formality that fires
    // after the string has already gone quiet. Measured t60 for these strings
    // is roughly 8s (madhya Sa) to 16s (mandra Sa) - substantially LONGER than
    // this 4s window - so the string is still audible when it is cut, and the
    // cut is a step discontinuity (a faint click). Ending the ring on a
    // measured amplitude/RMS floor instead of a fixed duration is the proper
    // fix and is deferred to TODOS.md.
    static constexpr float kMaxRingSeconds = 4.0f;

    // Upper bound on delay-line length (sampleRate / frequencyHz), DERIVED
    // from the two things that actually determine it rather than guessed:
    // the lowest frequency pluck() will ever run (kMinFrequencyHz, which
    // pluck() clamps to) and a generous ceiling on device sample rates.
    // 192000/20 + 1 = 9601 floats = ~38KB per string, ~150KB across the four
    // tanpura strings - trivial for a desktop app. Reserved once at
    // construction so pluck() never allocates on the audio thread.
    static constexpr double kMaxSupportedSampleRate = 192000.0;
    static constexpr size_t kMaxDelayLineLength = (size_t) (kMaxSupportedSampleRate / kMinFrequencyHz) + 1;

    std::vector<float> delayLine;
    size_t readPos = 0;
    int samplesSincePluck = 0;
    int maxRingSamples = 0; // computed in pluck() from the real sample rate
    bool ringing = false;
};
