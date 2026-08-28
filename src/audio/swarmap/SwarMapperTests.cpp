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
            expectEquals ((int) mandra.swar, (int) Swar::NiKomal);
            expectEquals (mandra.octaveIndex, -1);

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

        beginTest ("A different hysteresis margin changes the boundary threshold");
        {
            SwarMapper mapper (60.0f); // threshold = 50 + 60/2 = 80 cents from center
            mapper.update (0.0f); // lock to Sa
            auto stillLocked = mapper.update (70.0f); // 70 < 80, should NOT cross
            expectEquals ((int) stillLocked.swar, (int) Swar::Sa);

            auto crossed = mapper.update (90.0f); // 90 > 80, should cross
            expectEquals ((int) crossed.swar, (int) Swar::ReKomal);
        }

        beginTest ("swarToString and registerToString produce the expected labels");
        {
            expectEquals (swarToString (Swar::Sa), juce::String ("S"));
            expectEquals (swarToString (Swar::ReKomal), juce::String ("r"));
            expectEquals (swarToString (Swar::MaTivra), juce::String ("M'"));
            expectEquals (registerToString (OctaveRegister::Madhya), juce::String ("madhya"));
            expectEquals (registerToString (OctaveRegister::Other), juce::String ("other"));
        }

        beginTest ("centsFromSaForSwar() gives the standard 12-EDO cents value for every swar, plus octaveOffset*1200");
        {
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Sa, 0), 0.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::ReKomal, 0), 100.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Re, 0), 200.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::GaKomal, 0), 300.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Ga, 0), 400.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Ma, 0), 500.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::MaTivra, 0), 600.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Pa, 0), 700.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::DhaKomal, 0), 800.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Dha, 0), 900.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::NiKomal, 0), 1000.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Ni, 0), 1100.0f, 0.0001f);

            // Non-zero octaveOffset: Sa one octave up (Taar) is 1200 cents;
            // Ga one octave down (Mandra) is 400 - 1200 = -800 cents.
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Sa, 1), 1200.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Ga, -1), -800.0f, 0.0001f);
        }
    }
};

static SwarMapperTests swarMapperTestsInstance;
