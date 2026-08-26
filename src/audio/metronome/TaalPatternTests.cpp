#include "TaalPattern.h"
#include <juce_core/juce_core.h>

class TaalPatternTests : public juce::UnitTest
{
public:
    TaalPatternTests() : juce::UnitTest ("TaalPattern", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("PlainClick has a single beat, always classified as Plain, no cycle");
        {
            TaalPattern pattern (TaalType::PlainClick);
            expectEquals (pattern.beatCount(), 1);
            expect (pattern.classify (0) == BeatType::Plain);
        }

        beginTest ("Teentaal (16 beats): Sam at 0, Khali at 8, Clap at 4 and 12, Plain elsewhere");
        {
            TaalPattern pattern (TaalType::Teentaal);
            expectEquals (pattern.beatCount(), 16);

            const BeatType expected[16] = {
                BeatType::Sam,   BeatType::Plain, BeatType::Plain, BeatType::Plain,
                BeatType::Clap,  BeatType::Plain, BeatType::Plain, BeatType::Plain,
                BeatType::Khali, BeatType::Plain, BeatType::Plain, BeatType::Plain,
                BeatType::Clap,  BeatType::Plain, BeatType::Plain, BeatType::Plain
            };

            for (int i = 0; i < 16; ++i)
                expect (pattern.classify (i) == expected[i], "beat " + juce::String (i));
        }

        beginTest ("Jhaptaal (10 beats): Sam at 0, Khali at 2, Clap at 5 and 7, Plain elsewhere");
        {
            TaalPattern pattern (TaalType::Jhaptaal);
            expectEquals (pattern.beatCount(), 10);

            const BeatType expected[10] = {
                BeatType::Sam,  BeatType::Plain, BeatType::Khali, BeatType::Plain, BeatType::Plain,
                BeatType::Clap, BeatType::Plain, BeatType::Clap,  BeatType::Plain, BeatType::Plain
            };

            for (int i = 0; i < 10; ++i)
                expect (pattern.classify (i) == expected[i], "beat " + juce::String (i));
        }

        beginTest ("Ektaal (12 beats): Sam at 0, Khali at 2 and 6, Clap at 4, 8 and 10, Plain elsewhere");
        {
            TaalPattern pattern (TaalType::Ektaal);
            expectEquals (pattern.beatCount(), 12);

            const BeatType expected[12] = {
                BeatType::Sam,  BeatType::Plain, BeatType::Khali, BeatType::Plain,
                BeatType::Clap, BeatType::Plain, BeatType::Khali, BeatType::Plain,
                BeatType::Clap, BeatType::Plain, BeatType::Clap,  BeatType::Plain
            };

            for (int i = 0; i < 12; ++i)
                expect (pattern.classify (i) == expected[i], "beat " + juce::String (i));
        }
    }
};

static TaalPatternTests taalPatternTestsInstance;
