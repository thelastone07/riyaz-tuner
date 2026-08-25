#include "PitchGraphComponent.h"
#include <juce_core/juce_core.h>

class PitchGraphPointBufferTests : public juce::UnitTest
{
public:
    PitchGraphPointBufferTests() : juce::UnitTest ("PitchGraphPointBuffer", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("Points are stored in insertion order and retrievable");
        {
            PitchGraphPointBuffer buffer (8000);
            buffer.addPoint (0, 0.0f);
            buffer.addPoint (100, 5.0f);
            buffer.addPoint (200, -3.0f);

            auto& points = buffer.getPoints();
            expectEquals ((int) points.size(), 3);
            expectEquals ((long long) points[0].timestampMs, (long long) 0);
            expectWithinAbsoluteError (points[2].centsFromSa, -3.0f, 0.01f);
        }

        beginTest ("Points older than maxAgeMs relative to the newest point are evicted");
        {
            PitchGraphPointBuffer buffer (1000); // 1 second window
            buffer.addPoint (0, 0.0f);
            buffer.addPoint (500, 1.0f);
            buffer.addPoint (2500, 2.0f); // newest - window is now [1500, 2500]

            auto& points = buffer.getPoints();
            // Points at 0 and 500 are both older than 2500 - 1000 = 1500, so both evicted.
            expectEquals ((int) points.size(), 1);
            expectEquals ((long long) points[0].timestampMs, (long long) 2500);
        }

        beginTest ("clear() removes all points");
        {
            PitchGraphPointBuffer buffer (8000);
            buffer.addPoint (0, 0.0f);
            buffer.addPoint (100, 1.0f);
            buffer.clear();

            expect (buffer.getPoints().empty());
        }
    }
};

static PitchGraphPointBufferTests pitchGraphPointBufferTestsInstance;
