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
    }
};

static SwarMapperTests swarMapperTestsInstance;
