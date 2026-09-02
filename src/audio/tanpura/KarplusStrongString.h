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

    // Silence floor an amplitude ENVELOPE FOLLOWER (see envelope below) must
    // decay past before the ring is allowed to end - not a fixed duration.
    // Measured amplitude at the old fixed 4s cutoff was 0.043-0.100 for the
    // lower strings (i.e. still clearly audible, ending in an audible click);
    // this floor is far below that, so the ring now ends at true silence
    // instead of on an arbitrary clock. kMinRingSeconds guards against ending
    // early during the noise burst's own natural low-energy moments, before
    // the envelope follower has had time to settle onto the real decay curve.
    static constexpr float kSilenceFloor = 0.02f;
    static constexpr float kMinRingSeconds = 0.3f;

    // Hard upper bound, kept as a safety net (not the primary end-of-ring
    // mechanism any more - see kSilenceFloor above) in case damping is ever
    // configured such that the envelope never crosses the floor.
    static constexpr float kMaxRingSeconds = 20.0f;

    // Upper bound on delay-line length (sampleRate / frequencyHz), DERIVED
    // from the two things that actually determine it rather than guessed:
    // the lowest frequency pluck() will ever run (kMinFrequencyHz, which
    // pluck() clamps to) and a generous ceiling on device sample rates.
    // 192000/20 + 1 = 9601 floats = ~38KB per string, ~150KB across the four
    // tanpura strings - trivial for a desktop app. Reserved once at
    // construction so pluck() never allocates on the audio thread.
    static constexpr double kMaxSupportedSampleRate = 192000.0;
    static constexpr size_t kMaxDelayLineLength = (size_t) (kMaxSupportedSampleRate / kMinFrequencyHz) + 1;

    // Owned instance rather than juce::Random::getSystemRandom(): the noise
    // burst is generated on the audio thread inside pluck(), and the shared
    // system instance is not documented lock-free, while a per-string
    // instance's next() calls definitely are.
    juce::Random random;

    std::vector<float> delayLine;
    size_t readPos = 0;
    int samplesSincePluck = 0;
    int minRingSamples = 0; // computed in pluck() from the real sample rate
    int maxRingSamples = 0; // computed in pluck() from the real sample rate
    bool ringing = false;

    // One-pole envelope follower of |output|, used to end the ring on
    // measured silence (see kSilenceFloor) instead of a fixed duration.
    float envelope = 0.0f;

    // Fractional-delay tuning allpass ("C filter" in Jaffe & Smith's Extended
    // Karplus-Strong): the circular buffer above can only hold an INTEGER
    // number of samples, so on its own the string always resonates at
    // sampleRate/N for whatever integer N was chosen - up to +-8 cents off
    // the requested pitch after the +0.5 rounding correction in pluck() (see
    // the comment there, and TODOS.md). This one-sample all-pass filter adds
    // the missing fractional part of the delay so the total loop delay
    // (buffer + averaging filter + this) equals sampleRate/frequency exactly,
    // removing the residual error rather than just halving it. The tanpura
    // is the app's own tuning reference and the UI grades the user's cents
    // deviation against it, so an out-of-tune drone would have the app
    // disagreeing with itself.
    float allpassCoeff = 0.0f;
    float allpassPrevInput = 0.0f;
    float allpassPrevOutput = 0.0f;
};
