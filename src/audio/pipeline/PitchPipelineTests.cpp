// src/audio/pipeline/PitchPipelineTests.cpp
#include "PitchPipeline.h"
#include "../calibration/FakePitchEngine.h"
#include "../pitchengine/CrepePitchEngine.h"
#include <juce_core/juce_core.h>
#include <vector>

class PitchPipelineTests : public juce::UnitTest
{
public:
    PitchPipelineTests() : juce::UnitTest ("PitchPipeline", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("Starts in Calibrating phase and stays there until enough confident readings arrive");
        {
            // Exactly 10 explicit script entries for exactly 10 calls (9
            // confident + 1 unvoiced) - NOT 9 confident entries relying on
            // FakePitchEngine's repeat-last-frame behavior to pad out to 10
            // calls, since the repeated 10th call would also be confident
            // (the last entry, 220Hz) and yield 10 confident readings, not
            // 9 - silently turning this into a Success test instead of a
            // Timeout test. Scripting all 10 calls explicitly avoids that.
            std::vector<PitchFrame> script;
            for (int i = 0; i < 9; ++i)
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            PitchPipeline pipeline (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f); // 300ms, matches TonicCalibrator's own test convention

            PitchPipelineUpdate update { PitchPipelinePhase::Calibrating };
            for (int i = 0; i < 10; ++i) // 10 calls * 300ms = 3000ms = default calibration window
                update = pipeline.process (block.data(), block.size());

            expect (update.phase == PitchPipelinePhase::Calibrating);
            expect (update.calibrationStatus == CalibrationStatus::Timeout); // 9 confident < 10 minConfidentReadings
        }

        beginTest ("Transitions to Live phase once calibration succeeds, and live frames map to swar labels");
        {
            std::vector<PitchFrame> script;
            for (int i = 0; i < 10; ++i) // exactly 10 confident frames closes calibration successfully
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });
            // After calibration's window closes on the 10th call, the engine
            // (FakePitchEngine) repeats its last scripted frame (220Hz) for
            // all subsequent calls - so the first Live-phase call also sees
            // 220Hz, exactly at Sa (0 cents).

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            PitchPipeline pipeline (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f);

            PitchPipelineUpdate update { PitchPipelinePhase::Calibrating };
            for (int i = 0; i < 10; ++i)
                update = pipeline.process (block.data(), block.size());
            expect (update.phase == PitchPipelinePhase::Calibrating);
            expect (update.calibrationStatus == CalibrationStatus::Success);

            // Next call: pipeline should now be in Live phase, processing
            // through the engine + SwarMapper.
            auto liveUpdate = pipeline.process (block.data(), block.size());
            expect (liveUpdate.phase == PitchPipelinePhase::Live);
            expectWithinAbsoluteError (liveUpdate.saHz, 220.0f, 0.1f);
            expect (liveUpdate.swarLabel.has_value());
            if (liveUpdate.swarLabel.has_value())
                expectEquals ((int) liveUpdate.swarLabel->swar, (int) Swar::Sa);
        }

        beginTest ("An unvoiced frame during Live phase produces no swar label, and the following confident frame still resolves correctly");
        {
            // Script one entry per expected processFrame() call, explicitly:
            // 10 calibration frames, then a confident Live-phase frame (locks
            // SwarMapper to Sa), then an explicit unvoiced frame, then a
            // confident frame close to Sa again. PitchFrame's frequencyHz is
            // already std::optional<float>, so scripting nullopt directly
            // needs no changes to FakePitchEngine.
            std::vector<PitchFrame> script;
            for (int i = 0; i < 10; ++i) // calibration window
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });
            script.push_back (PitchFrame { 0, 220.0f, 0.9f });          // 11th call: first Live-phase frame, locks SwarMapper to Sa
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });    // 12th call: unvoiced
            script.push_back (PitchFrame { 0, 220.5f, 0.9f });          // 13th call: confident again, close to Sa

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            PitchPipeline pipeline (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f);

            for (int i = 0; i < 10; ++i)
                pipeline.process (block.data(), block.size()); // calibration

            auto firstLive = pipeline.process (block.data(), block.size()); // 11th call
            expect (firstLive.phase == PitchPipelinePhase::Live);
            expect (firstLive.swarLabel.has_value());
            if (firstLive.swarLabel.has_value())
                expectEquals ((int) firstLive.swarLabel->swar, (int) Swar::Sa);

            auto unvoicedUpdate = pipeline.process (block.data(), block.size()); // 12th call
            expect (! unvoicedUpdate.swarLabel.has_value());
            expect (! unvoicedUpdate.centsFromSa.has_value());

            auto afterUnvoiced = pipeline.process (block.data(), block.size()); // 13th call
            expect (afterUnvoiced.swarLabel.has_value());
            if (afterUnvoiced.swarLabel.has_value())
                expectEquals ((int) afterUnvoiced.swarLabel->swar, (int) Swar::Sa); // still locked to Sa via hysteresis, unaffected by the unvoiced gap
        }

        beginTest ("End-to-end: PitchPipeline calibrates and tracks live pitch using the real CrepePitchEngine");
        {
            CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
            auto prepareStatus = engine.prepare (16000.0);
            expect (prepareStatus == PitchEngineStatus::Ok);

            // windowMs=1000 with a 16384-sample (1024ms) buffer matches the
            // exact configuration TonicCalibrator's own end-to-end test
            // already proved sufficient for real ONNX inference to gather
            // >=10 confident readings (its default minConfidentReadings).
            PitchPipeline pipeline (engine, 16000.0, 1000);

            constexpr double sr = 16000.0;
            constexpr float freq = 220.0f;
            std::vector<float> sine (16384);
            for (size_t i = 0; i < sine.size(); ++i)
                sine[i] = std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / sr) * 0.5f;

            PitchPipelineUpdate update { PitchPipelinePhase::Calibrating };
            for (size_t offset = 0; offset + 1024 <= sine.size(); offset += 1024)
                update = pipeline.process (sine.data() + offset, 1024);

            expect (update.phase == PitchPipelinePhase::Calibrating);
            expect (update.calibrationStatus == CalibrationStatus::Success);

            // One more block of the same tone should now be Live-phase and resolve to Sa.
            auto liveUpdate = pipeline.process (sine.data(), 1024);
            expect (liveUpdate.phase == PitchPipelinePhase::Live);
            expect (liveUpdate.swarLabel.has_value());
            if (liveUpdate.swarLabel.has_value())
                expectEquals ((int) liveUpdate.swarLabel->swar, (int) Swar::Sa);
        }
    }
};

static PitchPipelineTests pitchPipelineTestsInstance;
