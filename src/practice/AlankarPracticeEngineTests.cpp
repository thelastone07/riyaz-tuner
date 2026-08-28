#include "AlankarPracticeEngine.h"
#include <juce_core/juce_core.h>
#include <algorithm>

class AlankarPracticeEngineTests : public juce::UnitTest
{
public:
    AlankarPracticeEngineTests() : juce::UnitTest ("AlankarPracticeEngine", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("A fresh engine starts at step 0, not finished");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1); // 16 steps total
            expect (! engine.isFinished());
            expectEquals (engine.currentStepIndex(), 0);
            expectEquals (engine.totalSteps(), 16);
        }

        beginTest ("onBeatElapsed() advances exactly one step per call (kBeatsPerStep == 1), and isFinished() becomes true at exactly the pattern's length");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1); // 16 steps

            for (int i = 0; i < 15; ++i)
            {
                expect (! engine.isFinished(), "should not be finished before all 16 steps have had their beat");
                engine.onBeatElapsed();
            }

            expect (! engine.isFinished(), "15 beats have elapsed - the 16th (last) step hasn't been advanced past yet");
            expectEquals (engine.currentStepIndex(), 15);

            engine.onBeatElapsed(); // the 16th beat
            expect (engine.isFinished());
        }

        beginTest ("onPitchReading() within tolerance counts as in-tune; outside tolerance does not");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1);
            // Step 0 is Sa (0 cents). Within +-25 cents = in tune.
            engine.onPitchReading (10.0f);  // in tune
            engine.onPitchReading (-20.0f); // in tune
            engine.onPitchReading (30.0f);  // NOT in tune (outside 25)
            engine.onPitchReading (0.0f);   // in tune

            const auto summary = engine.getSummary();
            expectEquals (summary.perStep[0].framesTotal, 4);
            expectEquals (summary.perStep[0].framesInTune, 3);
        }

        beginTest ("A step that never receives a pitch reading reports 0% (framesTotal == 0), not a crash");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1);
            engine.onBeatElapsed(); // advance past step 0 without ever calling onPitchReading() for it

            const auto summary = engine.getSummary();
            expectEquals (summary.perStep[0].framesTotal, 0);
            expectEquals (summary.perStep[0].framesInTune, 0);
        }

        beginTest ("overallTimeInTunePercent aggregates correctly across steps, and is 0 with no frames at all");
        {
            AlankarPracticeEngine freshEngine (AlankarPatternId::Alankar1);
            expectWithinAbsoluteError (freshEngine.getSummary().overallTimeInTunePercent, 0.0f, 0.0001f);

            AlankarPracticeEngine engine (AlankarPatternId::Alankar1);
            engine.onPitchReading (0.0f);   // step 0 (Sa, 0 cents) - in tune
            engine.onPitchReading (0.0f);   // step 0 - in tune
            engine.onBeatElapsed();          // advance to step 1 (Re, 200 cents)
            engine.onPitchReading (500.0f); // step 1 - NOT in tune (way off)

            const auto summary = engine.getSummary();
            // 2 in-tune out of 3 total frames = 66.67%
            expectWithinAbsoluteError (summary.overallTimeInTunePercent, 66.6667f, 0.01f);
        }

        beginTest ("perSwarTimeInTunePercent aggregates across steps that repeat the same swar (different positions)");
        {
            // Alankar 3's full sequence starts: Sa, Re, Ga, Re, Ga, Ma, ... -
            // "Re" appears at both step index 1 and step index 3. Feed
            // different accuracy to each occurrence and confirm the per-swar
            // total combines both.
            AlankarPracticeEngine engine (AlankarPatternId::Alankar3);

            engine.onPitchReading (0.0f);    // step 0 = Sa (0c) - in tune
            engine.onBeatElapsed();           // -> step 1 = Re (200c)
            engine.onPitchReading (200.0f);  // step 1 = Re - in tune
            engine.onBeatElapsed();           // -> step 2 = Ga (400c)
            engine.onPitchReading (400.0f);  // step 2 = Ga - in tune
            engine.onBeatElapsed();           // -> step 3 = Re (200c)
            engine.onPitchReading (900.0f);  // step 3 = Re - NOT in tune (way off)

            const auto summary = engine.getSummary();
            const auto reEntry = std::find_if (summary.perSwarTimeInTunePercent.begin(),
                                               summary.perSwarTimeInTunePercent.end(),
                                               [] (const auto& p) { return p.first == Swar::Re; });
            expect (reEntry != summary.perSwarTimeInTunePercent.end());
            // Re: 1 in-tune out of 2 total frames (steps 1 and 3 combined) = 50%
            expectWithinAbsoluteError (reEntry->second, 50.0f, 0.01f);
        }

        beginTest ("currentStepTargetCents() matches centsFromSaForSwar() for the current step");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1); // step 0 = Sa, 0 cents
            expectWithinAbsoluteError (engine.currentStepTargetCents(), 0.0f, 0.0001f);

            for (int i = 0; i < 6; ++i)
                engine.onBeatElapsed(); // Alankar 1: Sa Re Ga Ma Pa Dha Ni Sa'... - advance to step 6 = Ni

            expectEquals (engine.currentStepIndex(), 6);
            expectWithinAbsoluteError (engine.currentStepTargetCents(), 1100.0f, 0.0001f);
        }
    }
};

static AlankarPracticeEngineTests alankarPracticeEngineTestsInstance;
