#include "CrepeDecode.h"
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

class CrepeDecodeTests : public juce::UnitTest
{
public:
    CrepeDecodeTests() : juce::UnitTest ("CrepeDecode", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("Single confident bin decodes to that bin's exact cents/frequency");
        {
            std::vector<float> probs (360, 0.0f);
            probs[0] = 1.0f; // isolated spike at bin 0, all others exactly 0

            auto result = decodeCrepeOutput (probs.data(), 360);

            // Bin 0's cents value directly from the documented formula.
            const float expectedCents = 20.0f * 0.0f + 1997.3794084376191f;
            const float expectedFreq = 10.0f * std::pow (2.0f, expectedCents / 1200.0f);

            expectWithinAbsoluteError (result.frequencyHz, expectedFreq, 0.01f);
            expectWithinAbsoluteError (result.confidence, 1.0f, 0.0001f);
        }

        beginTest ("Weighted average pulls frequency toward a strong neighboring bin");
        {
            std::vector<float> probs (360, 0.0f);
            probs[200] = 0.6f;
            probs[201] = 0.6f; // equal-strength neighbor should pull the average up

            auto result = decodeCrepeOutput (probs.data(), 360);

            const float cents200 = 20.0f * 200.0f + 1997.3794084376191f;
            const float cents201 = 20.0f * 201.0f + 1997.3794084376191f;
            const float expectedCents = (cents200 * 0.6f + cents201 * 0.6f) / (0.6f + 0.6f);
            const float expectedFreq = 10.0f * std::pow (2.0f, expectedCents / 1200.0f);

            expectWithinAbsoluteError (result.frequencyHz, expectedFreq, 0.01f);
            expectWithinAbsoluteError (result.confidence, 0.6f, 0.0001f);
        }

        beginTest ("Window clamps at array bounds near bin 0 and bin 359");
        {
            std::vector<float> probs (360, 0.0f);
            probs[0] = 1.0f;
            probs[1] = 0.3f; // only 1 neighbor exists on the low side (no bin -1..-4)

            auto result = decodeCrepeOutput (probs.data(), 360);
            // Just confirm it doesn't crash/read out of bounds and produces a
            // frequency between bin 0's and bin 1's pure values.
            const float freq0 = 10.0f * std::pow (2.0f, (1997.3794084376191f) / 1200.0f);
            const float freq1 = 10.0f * std::pow (2.0f, (20.0f + 1997.3794084376191f) / 1200.0f);
            expect (result.frequencyHz >= freq0 && result.frequencyHz <= freq1);
        }
    }
};

static CrepeDecodeTests crepeDecodeTestsInstance;
