#include "AlankarPattern.h"
#include <juce_core/juce_core.h>

namespace
{
    bool stepsEqual (const std::vector<AlankarStep>& actual, const std::vector<AlankarStep>& expected)
    {
        if (actual.size() != expected.size())
            return false;
        for (size_t i = 0; i < actual.size(); ++i)
            if (actual[i].swar != expected[i].swar || actual[i].octaveOffset != expected[i].octaveOffset)
                return false;
        return true;
    }
}

class AlankarPatternTests : public juce::UnitTest
{
public:
    AlankarPatternTests() : juce::UnitTest ("AlankarPattern", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("Alankar 1: full sequence (8 ascending + 8 descending) matches the verified table exactly");
        {
            AlankarPattern pattern (AlankarPatternId::Alankar1);
            expectEquals (pattern.name(), juce::String ("Alankar 1"));

            const std::vector<AlankarStep> expected = {
                { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 },
                { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 },
                { Swar::Sa, 1 }, { Swar::Ni, 0 }, { Swar::Dha, 0 }, { Swar::Pa, 0 },
                { Swar::Ma, 0 }, { Swar::Ga, 0 }, { Swar::Re, 0 }, { Swar::Sa, 0 }
            };

            expect (stepsEqual (pattern.fullSequence(), expected));
        }

        beginTest ("Alankar 2: full sequence (14 ascending + 14 descending) matches the verified table exactly");
        {
            AlankarPattern pattern (AlankarPatternId::Alankar2);
            expectEquals (pattern.name(), juce::String ("Alankar 2"));

            const std::vector<AlankarStep> expected = {
                { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 },
                { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                { Swar::Ni, 0 }, { Swar::Sa, 1 },
                { Swar::Sa, 1 }, { Swar::Ni, 0 }, { Swar::Ni, 0 }, { Swar::Dha, 0 },
                { Swar::Dha, 0 }, { Swar::Pa, 0 }, { Swar::Pa, 0 }, { Swar::Ma, 0 },
                { Swar::Ma, 0 }, { Swar::Ga, 0 }, { Swar::Ga, 0 }, { Swar::Re, 0 },
                { Swar::Re, 0 }, { Swar::Sa, 0 }
            };

            expect (stepsEqual (pattern.fullSequence(), expected));
        }

        beginTest ("Alankar 3: full sequence (18 ascending + 18 descending) matches the verified table exactly");
        {
            AlankarPattern pattern (AlankarPatternId::Alankar3);
            expectEquals (pattern.name(), juce::String ("Alankar 3"));

            const std::vector<AlankarStep> expected = {
                { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 },
                { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 },
                { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 },
                { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 },
                { Swar::Sa, 1 }, { Swar::Ni, 0 }, { Swar::Dha, 0 },
                { Swar::Ni, 0 }, { Swar::Dha, 0 }, { Swar::Pa, 0 },
                { Swar::Dha, 0 }, { Swar::Pa, 0 }, { Swar::Ma, 0 },
                { Swar::Pa, 0 }, { Swar::Ma, 0 }, { Swar::Ga, 0 },
                { Swar::Ma, 0 }, { Swar::Ga, 0 }, { Swar::Re, 0 },
                { Swar::Ga, 0 }, { Swar::Re, 0 }, { Swar::Sa, 0 }
            };

            expect (stepsEqual (pattern.fullSequence(), expected));
        }

        beginTest ("Alankar 4: full sequence (20 ascending + 20 descending) matches the verified table exactly");
        {
            AlankarPattern pattern (AlankarPatternId::Alankar4);
            expectEquals (pattern.name(), juce::String ("Alankar 4"));

            const std::vector<AlankarStep> expected = {
                { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 },
                { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 },
                { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 },
                { Swar::Sa, 1 }, { Swar::Ni, 0 }, { Swar::Dha, 0 }, { Swar::Pa, 0 },
                { Swar::Ni, 0 }, { Swar::Dha, 0 }, { Swar::Pa, 0 }, { Swar::Ma, 0 },
                { Swar::Dha, 0 }, { Swar::Pa, 0 }, { Swar::Ma, 0 }, { Swar::Ga, 0 },
                { Swar::Pa, 0 }, { Swar::Ma, 0 }, { Swar::Ga, 0 }, { Swar::Re, 0 },
                { Swar::Ma, 0 }, { Swar::Ga, 0 }, { Swar::Re, 0 }, { Swar::Sa, 0 }
            };

            expect (stepsEqual (pattern.fullSequence(), expected));
        }

        beginTest ("Alankar 5: full sequence (20 ascending + 20 descending) matches the verified table exactly");
        {
            AlankarPattern pattern (AlankarPatternId::Alankar5);
            expectEquals (pattern.name(), juce::String ("Alankar 5"));

            const std::vector<AlankarStep> expected = {
                { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 },
                { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 },
                { Swar::Sa, 1 }, { Swar::Ni, 0 }, { Swar::Dha, 0 }, { Swar::Pa, 0 }, { Swar::Ma, 0 },
                { Swar::Ni, 0 }, { Swar::Dha, 0 }, { Swar::Pa, 0 }, { Swar::Ma, 0 }, { Swar::Ga, 0 },
                { Swar::Dha, 0 }, { Swar::Pa, 0 }, { Swar::Ma, 0 }, { Swar::Ga, 0 }, { Swar::Re, 0 },
                { Swar::Pa, 0 }, { Swar::Ma, 0 }, { Swar::Ga, 0 }, { Swar::Re, 0 }, { Swar::Sa, 0 }
            };

            expect (stepsEqual (pattern.fullSequence(), expected));
        }
    }
};

static AlankarPatternTests alankarPatternTestsInstance;
