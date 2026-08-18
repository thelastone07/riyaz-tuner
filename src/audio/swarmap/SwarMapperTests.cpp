#include "SwarMapper.h"
#include <juce_core/juce_core.h>

class SwarMapperTests : public juce::UnitTest
{
public:
    SwarMapperTests() : juce::UnitTest ("SwarMapper", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("First call locks to nearest swar with correct deviation");
        {
            SwarMapper mapper (15.0f);
            auto label = mapper.update (5.0f); // close to Sa (0 cents)
            expectEquals ((int) label.swar, (int) Swar::Sa);
            expectEquals (label.octaveIndex, 0);
            expectWithinAbsoluteError (label.centsFromCenter, 5.0f, 0.01f);
        }

        beginTest ("Small oscillation near a boundary does not flip the swar");
        {
            SwarMapper mapper (15.0f); // threshold = 50 + 15/2 = 57.5 cents from center
            mapper.update (0.0f); // lock to Sa
            auto a = mapper.update (52.0f);
            auto b = mapper.update (48.0f);
            auto c = mapper.update (52.0f);
            expectEquals ((int) a.swar, (int) Swar::Sa);
            expectEquals ((int) b.swar, (int) Swar::Sa);
            expectEquals ((int) c.swar, (int) Swar::Sa);
        }

        beginTest ("Deliberate crossing past the hysteresis threshold switches swar");
        {
            SwarMapper mapper (15.0f);
            mapper.update (0.0f); // lock to Sa
            auto label = mapper.update (60.0f); // beyond the 57.5 threshold
            expectEquals ((int) label.swar, (int) Swar::ReKomal);
            expectWithinAbsoluteError (label.centsFromCenter, -40.0f, 0.01f);
        }
    }
};

static SwarMapperTests swarMapperTestsInstance;
