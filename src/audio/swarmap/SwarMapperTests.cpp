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

        beginTest ("Large jump snaps directly to the new nearest swar, not stepwise");
        {
            SwarMapper mapper (15.0f);
            mapper.update (0.0f); // lock to Sa
            auto label = mapper.update (645.0f); // far jump, e.g. silence then a different pitch
            expectEquals ((int) label.swar, (int) Swar::MaTivra);
            expectWithinAbsoluteError (label.centsFromCenter, 45.0f, 0.01f);
        }

        beginTest ("Octave register naming for mandra/madhya/taar and numeric fallback");
        {
            SwarMapper mapper (15.0f);
            auto madhya = mapper.update (0.0f);
            expect (madhya.octaveRegister == OctaveRegister::Madhya);

            mapper.reset();
            auto mandra = mapper.update (-150.0f);
            expect (mandra.octaveRegister == OctaveRegister::Mandra);

            mapper.reset();
            auto taar = mapper.update (1245.0f);
            expect (taar.octaveRegister == OctaveRegister::Taar);

            mapper.reset();
            auto farOut = mapper.update (2500.0f);
            expect (farOut.octaveRegister == OctaveRegister::Other);
        }

        beginTest ("reset() clears the locked center so the next update re-locks fresh, not treated as a jump");
        {
            SwarMapper mapper (15.0f);
            mapper.update (0.0f); // lock to Sa (madhya)
            mapper.reset();
            auto label = mapper.update (1140.0f); // fresh lock, nearest to 1100 (Ni, madhya)
            expectEquals ((int) label.swar, (int) Swar::Ni);
            expect (label.octaveRegister == OctaveRegister::Madhya);
        }
    }
};

static SwarMapperTests swarMapperTestsInstance;
