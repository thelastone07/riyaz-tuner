#include "PitchContinuityFilter.h"
#include <juce_core/juce_core.h>

class PitchContinuityFilterTests : public juce::UnitTest
{
public:
    PitchContinuityFilterTests() : juce::UnitTest ("PitchContinuityFilter", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("First confident frame passes through unchanged and becomes the reference");
        {
            PitchContinuityFilter filter;
            PitchFrame frame { 0, 220.0f, 0.9f };
            auto result = filter.process (frame);
            expectWithinAbsoluteError (*result.frequencyHz, 220.0f, 0.01f);
        }

        beginTest ("A normal melodic interval (not near an octave multiple) passes through unchanged");
        {
            PitchContinuityFilter filter;
            filter.process (PitchFrame { 0, 220.0f, 0.9f }); // Sa
            auto result = filter.process (PitchFrame { 10, 329.63f, 0.9f }); // Pa, a fifth up (700 cents) - not near an octave
            expectWithinAbsoluteError (*result.frequencyHz, 329.63f, 0.5f);
        }

        beginTest ("A frame landing within 50 cents of exactly double the reference is corrected down an octave");
        {
            PitchContinuityFilter filter;
            filter.process (PitchFrame { 0, 220.0f, 0.9f });
            auto result = filter.process (PitchFrame { 10, 440.0f, 0.9f }); // exactly 1200 cents up - classic octave-error
            expectWithinAbsoluteError (*result.frequencyHz, 220.0f, 1.0f);
        }

        beginTest ("A frame landing within 50 cents of exactly half the reference is corrected up an octave");
        {
            PitchContinuityFilter filter;
            filter.process (PitchFrame { 0, 220.0f, 0.9f });
            auto result = filter.process (PitchFrame { 10, 110.0f, 0.9f }); // exactly -1200 cents
            expectWithinAbsoluteError (*result.frequencyHz, 220.0f, 1.0f);
        }

        beginTest ("Unvoiced frames pass through unchanged and do not reset the reference");
        {
            PitchContinuityFilter filter;
            filter.process (PitchFrame { 0, 220.0f, 0.9f });
            PitchFrame unvoiced { 10, std::nullopt, 0.0f };
            auto result = filter.process (unvoiced);
            expect (! result.frequencyHz.has_value());

            // Reference should still be 220Hz from before the unvoiced gap -
            // an octave-jump frame right after should still get corrected.
            auto corrected = filter.process (PitchFrame { 20, 440.0f, 0.9f });
            expectWithinAbsoluteError (*corrected.frequencyHz, 220.0f, 1.0f);
        }

        beginTest ("reset() clears the reference so the next frame passes through unchanged");
        {
            PitchContinuityFilter filter;
            filter.process (PitchFrame { 0, 220.0f, 0.9f });
            filter.reset();
            auto result = filter.process (PitchFrame { 10, 440.0f, 0.9f }); // would have been corrected pre-reset
            expectWithinAbsoluteError (*result.frequencyHz, 440.0f, 0.01f);
        }
    }
};

static PitchContinuityFilterTests pitchContinuityFilterTestsInstance;
