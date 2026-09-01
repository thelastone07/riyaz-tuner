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
            uint64_t t = 1000;

            for (int i = 0; i < 15; ++i)
            {
                expect (! engine.isFinished(), "should not be finished before all 16 steps have had their beat");
                t += 500;
                engine.onBeatElapsed (t);
            }

            expect (! engine.isFinished(), "15 beats have elapsed - the 16th (last) step hasn't been advanced past yet");
            expectEquals (engine.currentStepIndex(), 15);

            t += 500;
            engine.onBeatElapsed (t); // the 16th beat
            expect (engine.isFinished());
        }

        beginTest ("onPitchReading() weights by elapsed time since the previous reading, not by count; within-tolerance readings count toward msInTune");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1);
            // Step 0 is Sa (0 cents), tolerance +-25 cents.
            engine.onPitchReading (10.0f, 1000);  // first reading ever -> weight 0
            engine.onPitchReading (-20.0f, 1010); // weight 10, in tune
            engine.onPitchReading (30.0f, 1030);  // weight 20, NOT in tune (outside 25)
            engine.onPitchReading (0.0f, 1040);   // weight 10, in tune

            const auto summary = engine.getSummary();
            expectEquals ((int) summary.perStep[0].msTotal, 40);   // 0 + 10 + 20 + 10
            expectEquals ((int) summary.perStep[0].msInTune, 20);  // 10 (second reading) + 10 (fourth reading)
        }

        beginTest ("time-weighting: a known gap between two readings produces a proportional msTotal");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1);
            engine.onPitchReading (0.0f, 1000); // first reading ever -> weight 0
            engine.onPitchReading (0.0f, 1150); // weight 150, in tune

            const auto summary = engine.getSummary();
            expectEquals ((int) summary.perStep[0].msTotal, 150);
            expectEquals ((int) summary.perStep[0].msInTune, 150);
        }

        beginTest ("the gap cap: a reading-to-reading gap far larger than the cap contributes at most the capped amount");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1);
            engine.onPitchReading (0.0f, 1000); // first reading ever -> weight 0
            engine.onPitchReading (0.0f, 5000); // 4000ms gap, capped to 250ms (kMaxReadingGapMs)

            const auto summary = engine.getSummary();
            expectEquals ((int) summary.perStep[0].msTotal, 250);
        }

        beginTest ("late-straggler attribution: a reading timestamped before the recorded step-start lands in the previous step");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1);
            engine.onBeatElapsed (2000); // advance to step 1; step 1's recorded start = 2000

            // Both readings are timestamped BEFORE step 1's start - stragglers
            // from step 0's tail, arriving after step 1 has already begun.
            engine.onPitchReading (0.0f, 1400); // first reading ever -> weight 0, target = step 0 (1400 < 2000)
            engine.onPitchReading (0.0f, 1500); // weight 100, target = step 0 still, in tune

            const auto summary = engine.getSummary();
            expectEquals ((int) summary.perStep[0].msTotal, 100);
            expectEquals ((int) summary.perStep[1].msTotal, 0);
        }

        beginTest ("tail-of-finished-run attribution: a late reading after the final onBeatElapsed() still lands in the last step, not dropped");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1); // 16 steps, last real step index 15 = Sa (0 cents)
            uint64_t t = 1000;
            for (int i = 0; i < 16; ++i)
            {
                t += 200;
                engine.onBeatElapsed (t);
            }
            expect (engine.isFinished());

            // Both readings are timestamped before the finish boundary (t) -
            // stragglers from step 15's tail.
            engine.onPitchReading (0.0f, t - 100); // first reading ever -> weight 0, target = step 15 ((t-100) < t)
            engine.onPitchReading (0.0f, t - 50);  // weight 50, target = step 15 still, in tune

            const auto summary = engine.getSummary();
            expectEquals ((int) summary.perStep[15].msTotal, 50);
            expectEquals ((int) summary.perStep[15].msInTune, 50);
        }

        beginTest ("a step that never receives a pitch reading reports 0% (msTotal == 0), not a crash");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1);
            engine.onBeatElapsed (1000); // advance past step 0 without ever calling onPitchReading() for it

            const auto summary = engine.getSummary();
            expectEquals ((int) summary.perStep[0].msTotal, 0);
            expectEquals ((int) summary.perStep[0].msInTune, 0);
        }

        beginTest ("overallTimeInTunePercent aggregates correctly across steps, and is 0 with no time attributed at all");
        {
            AlankarPracticeEngine freshEngine (AlankarPatternId::Alankar1);
            expectWithinAbsoluteError (freshEngine.getSummary().overallTimeInTunePercent, 0.0f, 0.0001f);

            AlankarPracticeEngine engine (AlankarPatternId::Alankar1);
            engine.onPitchReading (0.0f, 1000);  // first reading ever -> weight 0, step 0 (Sa, 0 cents), in tune
            engine.onPitchReading (0.0f, 1100);  // weight 100, step 0, in tune
            engine.onBeatElapsed (1100);          // advance to step 1 (Re, 200 cents)
            engine.onPitchReading (500.0f, 1150); // weight 50, step 1, NOT in tune (way off)

            const auto summary = engine.getSummary();
            // 100 in-tune ms out of 150 total ms = 66.67%
            expectWithinAbsoluteError (summary.overallTimeInTunePercent, 66.6667f, 0.01f);
        }

        beginTest ("perSwarTimeInTunePercent aggregates across steps that repeat the same swar (different positions)");
        {
            // Alankar 3's full sequence starts: Sa, Re, Ga, Re, Ga, Ma, ... -
            // "Re" appears at both step index 1 and step index 3. Feed
            // different accuracy to each occurrence and confirm the per-swar
            // total combines both.
            AlankarPracticeEngine engine (AlankarPatternId::Alankar3);

            engine.onPitchReading (0.0f, 1000);    // first reading ever -> weight 0, step 0 = Sa (0c), in tune
            engine.onBeatElapsed (1100);            // -> step 1 = Re (200c)
            engine.onPitchReading (200.0f, 1200);  // weight 200, step 1 = Re, in tune
            engine.onBeatElapsed (1300);            // -> step 2 = Ga (400c)
            engine.onPitchReading (400.0f, 1400);  // weight 200, step 2 = Ga, in tune
            engine.onBeatElapsed (1500);            // -> step 3 = Re (200c)
            engine.onPitchReading (900.0f, 1600);  // weight 200, step 3 = Re, NOT in tune (way off)

            const auto summary = engine.getSummary();
            const auto reEntry = std::find_if (summary.perSwarTimeInTunePercent.begin(),
                                               summary.perSwarTimeInTunePercent.end(),
                                               [] (const auto& p) { return p.first == Swar::Re; });
            expect (reEntry != summary.perSwarTimeInTunePercent.end());
            // Re: 200ms in-tune out of 400ms total (steps 1 and 3 combined) = 50%
            expectWithinAbsoluteError (reEntry->second, 50.0f, 0.01f);
        }

        beginTest ("currentStepTargetCents() matches centsFromSaForSwar() for the current step");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1); // step 0 = Sa, 0 cents
            expectWithinAbsoluteError (engine.currentStepTargetCents(), 0.0f, 0.0001f);

            uint64_t t = 1000;
            for (int i = 0; i < 6; ++i)
            {
                t += 500;
                engine.onBeatElapsed (t); // Alankar 1: Sa Re Ga Ma Pa Dha Ni Sa'... - advance to step 6 = Ni
            }

            expectEquals (engine.currentStepIndex(), 6);
            expectWithinAbsoluteError (engine.currentStepTargetCents(), 1100.0f, 0.0001f);
        }
    }
};

static AlankarPracticeEngineTests alankarPracticeEngineTestsInstance;
