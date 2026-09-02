#include "KarplusStrongString.h"
#include <cmath>

KarplusStrongString::KarplusStrongString()
{
    delayLine.reserve (kMaxDelayLineLength);
}

void KarplusStrongString::pluck (float frequencyHz, double sampleRateIn, float amplitude)
{
    const float clampedFrequency = juce::jmax (kMinFrequencyHz, frequencyHz);

    // Exact desired loop delay, in samples - this is what the algorithm
    // actually needs to equal for the string to resonate exactly at
    // clampedFrequency. The circular buffer below can only hold an integer
    // number of samples, and the averaging (damping) filter's true loop
    // delay is N - 0.5, not N (it updates slot i from slots i and i+1 - see
    // the pluck()-tuning comment this replaced), so N is chosen as
    // exactDelay + 0.5 rounded down, leaving a fractional remainder in
    // [0, 1) for the tuning allpass below to supply exactly - see
    // allpassCoeff's declaration for why this matters more here than in a
    // normal instrument voice.
    const double exactDelay = sampleRateIn / (double) clampedFrequency;
    int delayLineLength = (int) std::floor (exactDelay + 0.5);
    delayLineLength = juce::jmax (2, delayLineLength);

    // Defence in depth for the no-allocation-on-the-audio-thread property.
    // The jassert below catches a violation loudly in Debug, but it compiles
    // away entirely in Release, so the safety property must not depend on it:
    // clamp instead, so even a sample rate beyond kMaxSupportedSampleRate
    // degrades to a wrong pitch rather than a reallocating assign().
    jassert ((size_t) delayLineLength <= kMaxDelayLineLength);
    delayLineLength = juce::jmin (delayLineLength, (int) kMaxDelayLineLength);

    delayLine.assign ((size_t) delayLineLength, 0.0f);

    // Fractional-delay allpass coefficient for the remaining [0, 1)-sample
    // delay (see the comment on exactDelay above and on allpassCoeff in the
    // header). Clamped away from the extremes: at fracDelay == 0 the
    // coefficient tends to 1, which is a marginally stable (not actively
    // unstable, but numerically fragile) allpass.
    const double fracDelay = juce::jlimit (0.001, 0.999, exactDelay + 0.5 - (double) delayLineLength);
    allpassCoeff = (float) ((1.0 - fracDelay) / (1.0 + fracDelay));
    allpassPrevInput = 0.0f;
    allpassPrevOutput = 0.0f;

    // A real plucked string's initial displacement tapers toward the (fixed)
    // bridge and nut rather than jumping to full amplitude and stopping dead
    // at the burst's edges - a uniform-amplitude noise burst's hard edges add
    // a broadband, clicky harshness a real pluck doesn't have. A Hann window
    // over the burst keeps the same noise-burst excitation (still simplest,
    // still no allocation) while shaping it into a softer attack and decay.
    const auto length = delayLine.size();
    for (size_t i = 0; i < length; ++i)
    {
        const float hann = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (length - 1));
        delayLine[i] = amplitude * hann * (2.0f * random.nextFloat() - 1.0f);
    }

    readPos = 0;
    samplesSincePluck = 0;
    envelope = amplitude; // seed at the pluck's own amplitude so the min-ring guard below has a sane starting point
    // Durations, not fixed sample counts - see kMinRingSeconds/kMaxRingSeconds.
    minRingSamples = (int) (sampleRateIn * (double) kMinRingSeconds);
    maxRingSamples = (int) (sampleRateIn * (double) kMaxRingSeconds);
    ringing = true;
}

float KarplusStrongString::renderNextSample()
{
    if (! ringing || delayLine.empty())
        return 0.0f;

    const float out = delayLine[readPos];
    const size_t nextPos = (readPos + 1) % delayLine.size();

    const float damped = kDamping * 0.5f * (delayLine[readPos] + delayLine[nextPos]);

    // Fractional-delay tuning allpass (see allpassCoeff's declaration):
    // y[n] = coeff * (x[n] - y[n-1]) + x[n-1].
    const float allpassOut = allpassCoeff * (damped - allpassPrevOutput) + allpassPrevInput;
    allpassPrevInput = damped;
    allpassPrevOutput = allpassOut;

    delayLine[readPos] = allpassOut;
    readPos = nextPos;

    // Envelope-follow |out| (slow enough to ride over individual noisy
    // samples, fast enough to track the real decay curve within a fraction
    // of a second) and end the ring once it - not a fixed clock - says the
    // string has decayed to true silence. See kSilenceFloor's declaration
    // for why this matters: the old fixed-duration cutoff ended every pluck
    // cycle on an audible step discontinuity (a faint click), because
    // measured decay times are far longer than any duration short enough to
    // avoid excessive CPU/voice buildup.
    envelope = juce::jmax (std::abs (out), envelope * 0.9995f);

    ++samplesSincePluck;
    if (samplesSincePluck >= maxRingSamples
        || (samplesSincePluck >= minRingSamples && envelope < kSilenceFloor))
        ringing = false;

    return out;
}

bool KarplusStrongString::isRinging() const
{
    return ringing;
}
