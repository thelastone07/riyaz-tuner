#include "TanpuraSynth.h"
#include <juce_core/juce_core.h>
#include <cmath>

class TanpuraSynthTests : public juce::UnitTest
{
public:
    TanpuraSynthTests() : juce::UnitTest ("TanpuraSynth", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("First renderNextSample() call triggers the first pluck and produces non-silent output within one pluck interval");
        {
            TanpuraSynth synth;
            synth.prepare (44100.0, 220.0f, TanpuraTuning::PaSaSaSa, 0.05f); // 50ms between plucks, fast for test speed

            bool foundSound = false;
            const int samplesInOnePluckInterval = (int) (44100.0 * 0.05);
            for (int i = 0; i < samplesInOnePluckInterval; ++i)
            {
                if (std::abs (synth.renderNextSample()) > 0.01f)
                {
                    foundSound = true;
                    break;
                }
            }
            expect (foundSound);
        }

        beginTest ("Over several pluck intervals, output remains non-silent (strings overlap - not a series of isolated blips)");
        {
            TanpuraSynth synth;
            synth.prepare (44100.0, 220.0f, TanpuraTuning::PaSaSaSa, 0.05f);

            // Render past the 4th pluck (all 4 strings triggered at least
            // once) and confirm the LAST 100 samples of that span still have
            // energy - proving strings are still ringing/overlapping, not
            // that the synth went silent between isolated plucks.
            const int samplesFor4Plucks = (int) (44100.0 * 0.05 * 4);
            float sumSquares = 0.0f;
            for (int i = 0; i < samplesFor4Plucks; ++i)
            {
                const float s = synth.renderNextSample();
                if (i >= samplesFor4Plucks - 100)
                    sumSquares += s * s;
            }
            const float rms = std::sqrt (sumSquares / 100.0f);
            expect (rms > 0.01f);
        }

        beginTest ("A silent/unprepared synth renders silence");
        {
            TanpuraSynth synth;
            expectWithinAbsoluteError (synth.renderNextSample(), 0.0f, 0.0001f);
        }
    }
};

static TanpuraSynthTests tanpuraSynthTestsInstance;
