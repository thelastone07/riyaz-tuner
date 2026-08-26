#include "MetronomeClick.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

namespace
{
    // 44100 alone previously hid two real Tanpura bugs that were purely
    // sample-rate-dependent - the set deliberately includes a rate above
    // 48000.
    constexpr double kTestSampleRates[] = { 44100.0, 48000.0, 96000.0 };

    struct ClickSpec { BeatType type; const char* name; float frequencyHz; float durationSeconds; };
    constexpr ClickSpec kClickSpecs[] = {
        { BeatType::Sam,   "Sam",   1200.0f, 0.050f },
        { BeatType::Clap,  "Clap",  900.0f,  0.040f },
        { BeatType::Khali, "Khali", 500.0f,  0.035f },
        { BeatType::Plain, "Plain", 700.0f,  0.020f },
    };

    // Magnitude of the DFT evaluated at one arbitrary (non-bin-aligned)
    // frequency, via the Goertzel recurrence.
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

class MetronomeClickTests : public juce::UnitTest
{
public:
    MetronomeClickTests() : juce::UnitTest ("MetronomeClick", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("A fresh (never-triggered) click is not sounding and renders silence");
        {
            MetronomeClick click;
            expect (! click.isSounding());
            expectWithinAbsoluteError (click.renderNextSample(), 0.0f, 0.0001f);
        }

        for (const auto& spec : kClickSpecs)
        {
            for (const double sampleRate : kTestSampleRates)
            {
                beginTest (juce::String ("Immediately after trigger(), ") + spec.name
                           + " has significant energy (non-silent) at " + juce::String (sampleRate, 0) + "Hz");
                {
                    MetronomeClick click;
                    click.trigger (spec.type, sampleRate);
                    expect (click.isSounding());

                    float sumSquares = 0.0f;
                    constexpr int kSamplesToCheck = 20;
                    for (int i = 0; i < kSamplesToCheck; ++i)
                    {
                        const float s = click.renderNextSample();
                        sumSquares += s * s;
                    }
                    const float rms = std::sqrt (sumSquares / (float) kSamplesToCheck);
                    expect (rms > 0.05f);
                }
            }
        }

        for (const auto& spec : kClickSpecs)
        {
            for (const double sampleRate : kTestSampleRates)
            {
                beginTest (juce::String (spec.name) + "'s dominant frequency matches its intended pitch at "
                           + juce::String (sampleRate, 0) + "Hz");
                {
                    MetronomeClick click;
                    click.trigger (spec.type, sampleRate);

                    const int windowSamples = (int) (sampleRate * (double) spec.durationSeconds);
                    std::vector<float> window ((size_t) windowSamples, 0.0f);
                    for (int i = 0; i < windowSamples; ++i)
                        window[(size_t) i] = click.renderNextSample();

                    const double measuredHz = measureDominantFrequency (window, sampleRate,
                                                                        (double) spec.frequencyHz, 30.0, 1.0);
                    const double errorCents = centsBetween (measuredHz, (double) spec.frequencyHz);

                    // A generous tolerance: the decay envelope shortens the
                    // effective analysis window and can smear the spectral
                    // peak slightly, especially for the shortest burst
                    // (Plain, ~14 cycles at 700Hz). 30 cents is still easily
                    // enough to catch a wrong-frequency regression.
                    expect (std::abs (errorCents) < 30.0,
                            "measured " + juce::String (measuredHz, 3) + "Hz ("
                                + juce::String (errorCents, 2) + " cents) vs target "
                                + juce::String (spec.frequencyHz, 1) + "Hz");
                }
            }
        }

        for (const auto& spec : kClickSpecs)
        {
            for (const double sampleRate : kTestSampleRates)
            {
                beginTest (juce::String (spec.name) + " sounds for exactly its intended duration at "
                           + juce::String (sampleRate, 0) + "Hz (bounded from BOTH sides)");
                {
                    MetronomeClick click;
                    click.trigger (spec.type, sampleRate);

                    // The duration MUST be derived from the sample rate that
                    // was passed to trigger(), so it is bounded on both sides
                    // against THIS type's own durationSeconds - never against
                    // a shared upper bound like the longest type's duration.
                    //
                    // An upper bound alone is not a timing guard: a baked-in
                    // 44100 in the samplesRemaining calculation (the exact
                    // shape of KarplusStrongString's kMaxRingSamples bug)
                    // makes the click SHORTER at every rate above 44100 -
                    // less than half its intended length at 96kHz - and a
                    // one-sided "is it finished yet?" assertion stays green
                    // through all of it.
                    const int intendedSamples = (int) (sampleRate * (double) spec.durationSeconds);

                    // Lower bound: still sounding just before its own
                    // intended duration ends.
                    for (int i = 0; i < intendedSamples - 2; ++i)
                        click.renderNextSample();

                    expect (click.isSounding(),
                            juce::String (spec.name) + " at " + juce::String (sampleRate, 0)
                                + "Hz ended BEFORE its intended duration of "
                                + juce::String (intendedSamples) + " samples - the duration is not being"
                                  " derived from the actual sample rate passed to trigger()");

                    // Upper bound: finished just after it, and silent from
                    // then on.
                    for (int i = 0; i < 4; ++i) // (intendedSamples - 2) + 4 = intendedSamples + 2
                        click.renderNextSample();

                    expect (! click.isSounding(),
                            juce::String (spec.name) + " at " + juce::String (sampleRate, 0)
                                + "Hz was still sounding AFTER its intended duration of "
                                + juce::String (intendedSamples) + " samples");
                    expectWithinAbsoluteError (click.renderNextSample(), 0.0f, 0.0001f);
                }
            }
        }
    }
};

static MetronomeClickTests metronomeClickTestsInstance;
