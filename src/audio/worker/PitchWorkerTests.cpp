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
            PitchWorker worker (pipeline, listener);
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
    }
};

static PitchWorkerTests pitchWorkerTestsInstance;
