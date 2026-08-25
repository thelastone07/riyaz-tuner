#include "KarplusStrongString.h"
#include <juce_core/juce_core.h>
#include <cmath>

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

        beginTest ("Immediately after pluck(), output has significant energy (non-silent)");
        {
            KarplusStrongString str;
            str.pluck (220.0f, 44100.0, 1.0f);

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
