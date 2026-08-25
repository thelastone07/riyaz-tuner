// src/audio/worker/PitchWorkerTests.cpp
#include "PitchWorker.h"
#include "../calibration/FakePitchEngine.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <vector>

namespace
{
    class CapturingListener : public PitchWorker::Listener
    {
    public:
        void pitchWorkerUpdate (const PitchPipelineUpdate& update) override
        {
            lastUpdate = update;
            gotUpdate.signal();
        }

        juce::WaitableEvent gotUpdate;
        PitchPipelineUpdate lastUpdate { PitchPipelinePhase::Calibrating };
    };
}

class PitchWorkerTests : public juce::UnitTest
{
public:
    PitchWorkerTests() : juce::UnitTest ("PitchWorker", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("Audio pushed on the calling thread reaches the listener on the message thread");
        {
            // A message manager is required for AsyncUpdater's callback to be
            // delivered - juce::UnitTestRunner runs on a thread that already
            // has one when run via the console app's TestMain, but construct
            // one defensively so this test is self-contained if ever run
            // standalone.
            juce::ScopedJuceInitialiser_GUI juceInit;

            std::vector<PitchFrame> script;
            for (int i = 0; i < 9; ++i)
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });
            // 9 confident + 1 unvoiced = Timeout after the calibration window
            // closes - this test only needs to prove audio reaches the
            // pipeline and a result comes back, not a specific outcome.

            FakePitchEngine engine (script);
            engine.prepare (44100.0);
            PitchPipeline pipeline (engine, 44100.0);

            CapturingListener listener;
            // Four seconds of FIFO headroom - comfortably more than the
            // ~3 seconds of audio this test pushes below - so a worker-thread
            // scheduling hiccup between two pushes can never cause the FIFO
            // to drop samples. That drop behaviour is correct under real
            // backpressure, but here it would corrupt this test's timing
            // assumptions (all 132300 samples across 10 pushes must actually
            // reach the calibrator to close the 3000ms window as expected).
            PitchWorker worker (pipeline, listener, 44100 * 4);
            worker.start();

            std::vector<float> block (44100 * 300 / 1000, 0.0f); // 300ms, matches the project's established test convention
            for (int i = 0; i < 10; ++i)
            {
                worker.pushAudio (block.data(), (int) block.size());
                // Give the worker thread and message loop a chance to run
                // between pushes. juce::AsyncUpdater::triggerAsyncUpdate()
                // posts a message that is only delivered when the message
                // thread's dispatch loop actually runs - a plain sleep()
                // here (as opposed to pumping the loop) would never let
                // handleAsyncUpdate() fire, since this test IS the message
                // thread (it constructed ScopedJuceInitialiser_GUI above,
                // and TestMain.cpp's console main() never pumps one either).
                // runDispatchLoopUntil() both waits and pumps.
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            }

            const bool gotIt = listener.gotUpdate.wait (2000.0);
            worker.stop();

            expect (gotIt);
            expect (listener.lastUpdate.calibrationStatus == CalibrationStatus::Timeout);
        }

        beginTest ("A failed calibration restarts automatically on the worker thread, and the retry can still succeed");
        {
            juce::ScopedJuceInitialiser_GUI juceInit;

            // FakePitchEngine::reset() rewinds nextIndex to 0 (see
            // FakePitchEngine.h) and PitchPipeline::restartCalibration() calls
            // engine.reset() - so the SECOND calibration attempt replays this
            // SAME script from script[0]. The two attempts are therefore told
            // apart by how many process() calls each one takes, not by
            // different script content:
            //
            //   Attempt 1: 3 pushes of 1000ms -> 3 engine frames -> only 3
            //              confident readings when the 3000ms window closes.
            //              3 < 10 (TonicCalibrator's minConfidentReadings)
            //              => Timeout => PitchWorker auto-restarts.
            //   Attempt 2: 10 pushes of 300ms -> 10 engine frames -> replays
            //              script[0..9], all confident and all exactly 220Hz,
            //              when the 3000ms window closes. 10 >= 10 and zero
            //              spread => Success.
            std::vector<PitchFrame> script;
            for (int i = 0; i < 10; ++i)
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });

            FakePitchEngine engine (script);
            engine.prepare (44100.0);
            PitchPipeline pipeline (engine, 44100.0);

            CapturingListener listener;
            PitchWorker worker (pipeline, listener, 44100 * 4); // headroom for a 44100-sample (1000ms) push
            worker.start();

            // Push exactly one block, then wait for the resulting update
            // before pushing the next. PitchWorker::run() coalesces ALL ready
            // FIFO samples into ONE process() call, so overlapping pushes
            // would merge into fewer engine frames and break attempt 2's
            // exact 10-frame arithmetic above. Keeping one push outstanding
            // at a time makes push -> process() call a 1:1 mapping:
            // AbstractFifo publishes a write atomically when pushAudio()'s
            // scoped write handle goes out of scope, so the worker either
            // sees the whole block or none of it.
            auto pushAndAwait = [&listener, &worker] (const std::vector<float>& block)
            {
                listener.gotUpdate.reset();
                worker.pushAudio (block.data(), (int) block.size());

                const auto deadline = juce::Time::getMillisecondCounter() + 4000;
                while (juce::Time::getMillisecondCounter() < deadline)
                {
                    // This test thread IS the message thread, so the update is
                    // only delivered while the dispatch loop is pumped - a
                    // plain blocking wait() here would deadlock the delivery.
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
                    if (listener.gotUpdate.wait (0.0))
                        return true;
                }
                return false;
            };

            std::vector<float> oneSecondBlock (44100, 0.0f);
            std::vector<float> threeHundredMsBlock (44100 * 300 / 1000, 0.0f);

            bool allDelivered = true;
            for (int i = 0; i < 3; ++i) // attempt 1: 3 * 1000ms = the full 3000ms window
                allDelivered = pushAndAwait (oneSecondBlock) && allDelivered;

            expect (allDelivered);
            expect (listener.lastUpdate.phase == PitchPipelinePhase::Calibrating);
            expect (listener.lastUpdate.calibrationStatus == CalibrationStatus::Timeout);

            // Nothing in this test ever calls restartCalibration(). If
            // PitchWorker::run() did not restart it on its own, TonicCalibrator
            // stays latched on Timeout forever (processFrame() is idempotent
            // once its window has closed) and every assertion below fails.
            allDelivered = pushAndAwait (threeHundredMsBlock) && allDelivered;
            expect (allDelivered);
            expect (listener.lastUpdate.calibrationStatus == CalibrationStatus::InProgress); // a fresh window is open, not the latched Timeout

            for (int i = 1; i < 10; ++i) // remaining 9 of attempt 2's 10 * 300ms
                allDelivered = pushAndAwait (threeHundredMsBlock) && allDelivered;

            expect (allDelivered);
            expect (listener.lastUpdate.calibrationStatus == CalibrationStatus::Success);
            expectWithinAbsoluteError (listener.lastUpdate.saHz, 220.0f, 0.1f);

            worker.stop();
        }
    }
};

static PitchWorkerTests pitchWorkerTestsInstance;
