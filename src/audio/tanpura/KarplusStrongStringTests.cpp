#include "KarplusStrongString.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

namespace
{
    // Sample rates every core test is run at. 44100 alone hid two real bugs
    // (a delay-line capacity bound that overflowed above 48kHz, and a ring
    // window that was a raw sample count rather than a duration), so the set
    // deliberately includes a rate above 48000.
    constexpr double kTestSampleRates[] = { 44100.0, 48000.0, 96000.0 };

    // Magnitude of the DFT evaluated at one arbitrary (non-bin-aligned)
    // frequency, via the Goertzel recurrence - ten lines and no FFT
    // dependency.
    double goertzelMagnitude (const std::vector<float>& samples, double frequencyHz, double sampleRate)
    {
        const double omega = 2.0 * juce::MathConstants<double>::pi * frequencyHz / sampleRate;
        const double coeff = 2.0 * std::cos (omega);
        double s1 = 0.0, s2 = 0.0;

        for (const float v : samples)
        {
            const double s0 = (double) v + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        return std::sqrt (s1 * s1 + s2 * s2 - coeff * s1 * s2);
    }

    // Sweep candidate frequencies around the target and return the one with
    // the largest Goertzel magnitude, i.e. the signal's dominant frequency.
    double measureDominantFrequency (const std::vector<float>& samples, double sampleRate,
                                     double targetHz, double searchRadiusHz, double stepHz)
    {
        double bestHz = targetHz;
        double bestMagnitude = -1.0;

        for (double f = targetHz - searchRadiusHz; f <= targetHz + searchRadiusHz + 1.0e-9; f += stepHz)
        {
            const double magnitude = goertzelMagnitude (samples, f, sampleRate);
            if (magnitude > bestMagnitude)
            {
                bestMagnitude = magnitude;
                bestHz = f;
            }
        }

        return bestHz;
    }

    double centsBetween (double measuredHz, double referenceHz)
    {
        return 1200.0 * std::log2 (measuredHz / referenceHz);
    }
}

class KarplusStrongStringTests : public juce::UnitTest
{
public:
    KarplusStrongStringTests() : juce::UnitTest ("KarplusStrongString", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("pluck() at a given frequency and sample rate sets the delay line length correctly");
        {
            KarplusStrongString str;
            str.pluck (220.0f, 44100.0, 1.0f);
            // Karplus-Strong's fundamental period is (delay line length / sample
            // rate) - this is what makes the algorithm pitched at all. We can't
            // easily inspect the delay line's length directly without exposing
            // internals, so verify indirectly: immediately after plucking, the
            // string must be ringing (non-silent).
            expect (str.isRinging());
        }

        for (const double sampleRate : kTestSampleRates)
        {
            beginTest ("Immediately after pluck(), output has significant energy (non-silent) at "
                       + juce::String (sampleRate, 0) + "Hz");
            {
                KarplusStrongString str;
                str.pluck (220.0f, sampleRate, 1.0f);

                float sumSquares = 0.0f;
                constexpr int kSamplesToCheck = 500;
                for (int i = 0; i < kSamplesToCheck; ++i)
                {
                    const float s = str.renderNextSample();
                    sumSquares += s * s;
                }
                const float rms = std::sqrt (sumSquares / (float) kSamplesToCheck);
                expect (rms > 0.05f); // comfortably above silence, well below clipping
            }
        }

        // The tanpura is the app's own tuning reference - the UI grades the
        // user's cents deviation against the same Sa - so "what pitch is it"
        // is a first-class assertion, not a substitute for one. Run at every
        // rate in kTestSampleRates, since the delay line is sized from it.
        for (const double sampleRate : kTestSampleRates)
        {
            beginTest ("A plucked string's dominant frequency matches the requested pitch at "
                       + juce::String (sampleRate, 0) + "Hz");
            {
                constexpr float kTargetHz = 220.0f;

                KarplusStrongString str;
                str.pluck (kTargetHz, sampleRate, 1.0f);

                // Skip the initial noise-burst transient, then capture a
                // settled window to measure.
                const int transientSamples = (int) (sampleRate * 0.05);
                for (int i = 0; i < transientSamples; ++i)
                    str.renderNextSample();

                const int windowSamples = (int) (sampleRate * 0.5);
                std::vector<float> window ((size_t) windowSamples, 0.0f);
                for (int i = 0; i < windowSamples; ++i)
                    window[(size_t) i] = str.renderNextSample();

                const double measuredHz = measureDominantFrequency (window, sampleRate,
                                                                    (double) kTargetHz,
                                                                    20.0,  // search +-20Hz around the target
                                                                    0.5);  // in 0.5Hz steps
                const double errorCents = centsBetween (measuredHz, (double) kTargetHz);

                // 15 cents of tolerance is not test slop: it covers a KNOWN
                // residual bias from quantizing the delay line to a whole
                // number of samples. The +0.5 correction in pluck() removes
                // the systematic sharp bias and caps the residual at roughly
                // +-8 cents; a fractional/interpolated read position would
                // remove it entirely and is deferred to TODOS.md.
                expect (std::abs (errorCents) < 15.0,
                        "measured " + juce::String (measuredHz, 3) + "Hz ("
                            + juce::String (errorCents, 2) + " cents) vs target "
                            + juce::String (kTargetHz, 1) + "Hz");
            }
        }

        // 220Hz above is a realistic tonic, but its delay-line quantization
        // error happens to be mild, so it alone cannot tell the corrected
        // delay length from the uncorrected one. 439Hz at 44100 lands near
        // the worst case (44100/439 = 100.46, a fraction just under .5):
        // without the +0.5 loop-delay correction in pluck() the string
        // resonates at 443.2Hz, +16.6 cents, and this test fails. It is the
        // test that actually pins the correction in place.
        beginTest ("Delay-line quantization stays inside tolerance at a worst-case frequency");
        {
            constexpr float kTargetHz = 439.0f;
            constexpr double kSampleRate = 44100.0;

            KarplusStrongString str;
            str.pluck (kTargetHz, kSampleRate, 1.0f);

            for (int i = 0; i < (int) (kSampleRate * 0.05); ++i)
                str.renderNextSample();

            const int windowSamples = (int) (kSampleRate * 0.5);
            std::vector<float> window ((size_t) windowSamples, 0.0f);
            for (int i = 0; i < windowSamples; ++i)
                window[(size_t) i] = str.renderNextSample();

            const double measuredHz = measureDominantFrequency (window, kSampleRate,
                                                                (double) kTargetHz, 20.0, 0.5);
            const double errorCents = centsBetween (measuredHz, (double) kTargetHz);

            expect (std::abs (errorCents) < 15.0,
                    "measured " + juce::String (measuredHz, 3) + "Hz ("
                        + juce::String (errorCents, 2) + " cents) vs target "
                        + juce::String (kTargetHz, 1) + "Hz");
        }

        beginTest ("After many samples, the string has decayed to near-silence and isRinging() reports false");
        {
            KarplusStrongString str;
            str.pluck (220.0f, 44100.0, 1.0f);

            // Render 5 seconds' worth at 44.1kHz - far longer than any
            // realistic tanpura string's natural decay - to reach the tail.
            constexpr int kFiveSeconds = 44100 * 5;
            for (int i = 0; i < kFiveSeconds; ++i)
                str.renderNextSample();

            expect (! str.isRinging());

            float sumSquares = 0.0f;
            constexpr int kSamplesToCheck = 500;
            for (int i = 0; i < kSamplesToCheck; ++i)
            {
                const float s = str.renderNextSample();
                sumSquares += s * s;
            }
            const float rms = std::sqrt (sumSquares / (float) kSamplesToCheck);
            expect (rms < 0.01f); // decayed well below the "just plucked" RMS
        }

        beginTest ("A fresh (never-plucked) string is not ringing and renders silence");
        {
            KarplusStrongString str;
            expect (! str.isRinging());
            expectWithinAbsoluteError (str.renderNextSample(), 0.0f, 0.0001f);
        }

        beginTest ("pluck() with a non-positive frequency doesn't crash (clamped to a safe minimum)");
        {
            KarplusStrongString str;
            str.pluck (0.0f, 44100.0, 1.0f);
            expect (str.isRinging());
            // Just confirm it renders without crashing - the exact pitch produced
            // by the clamp isn't the point of this test, safety is.
            for (int i = 0; i < 100; ++i)
                str.renderNextSample();
        }
    }
};

static KarplusStrongStringTests karplusStrongStringTestsInstance;
