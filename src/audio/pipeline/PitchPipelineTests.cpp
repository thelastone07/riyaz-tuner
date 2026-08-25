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
            // confident frame at +55 cents from Sa. PitchFrame's frequencyHz
            // is already std::optional<float>, so scripting nullopt directly
            // needs no changes to FakePitchEngine.
            //
            // +55 cents is deliberately NOT close to Sa in absolute terms -
            // it's chosen to be discriminating between "the hysteresis lock
            // survived the unvoiced gap" and "SwarMapper started fresh":
            //   - SwarMapper's lock-retention threshold is
            //     50 + hysteresisMargin/2 = 50 + 15/2 = 57.5 cents from the
            //     locked center (see SwarMapper::update() in
            //     SwarMapper.cpp). +55 cents is inside that band, so if the
            //     lock on Sa (0 cents) survived the unvoiced gap, this frame
            //     still resolves to Sa.
            //   - A FRESH/unlocked SwarMapper instead nearest-quantizes to
            //     the closest 100-cent center: nearestCenterCents(55) rounds
            //     55/100 = 0.55 to 1, i.e. 100 cents = ReKomal, NOT Sa.
            // So this frame's expected label (Sa vs ReKomal) actually
            // depends on whether the unvoiced frame preserved the lock -
            // unlike a near-zero-cents frame, which would resolve to Sa
            // either way and so couldn't catch a regression here.
            std::vector<PitchFrame> script;
            for (int i = 0; i < 10; ++i) // calibration window
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });
            script.push_back (PitchFrame { 0, 220.0f, 0.9f });          // 11th call: first Live-phase frame, locks SwarMapper to Sa
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });    // 12th call: unvoiced
            // freq = 220.0 * 2^(55/1200) ~= 227.101Hz - computed with
            // std::pow for exactness rather than a hardcoded literal.
            const float freqPlus55Cents = 220.0f * std::pow (2.0f, 55.0f / 1200.0f);
            script.push_back (PitchFrame { 0, freqPlus55Cents, 0.9f }); // 13th call: confident again, +55 cents from Sa (see comment above)

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
                expectEquals ((int) afterUnvoiced.swarLabel->swar, (int) Swar::Sa); // still locked to Sa via hysteresis (55 cents < 57.5 threshold) - would read ReKomal instead if the lock had NOT survived the unvoiced gap
        }

        beginTest ("restartCalibration() clears calibrator, engine, and SwarMapper state, letting a full second calibration attempt run to completion");
        {
            // Same script-construction idiom as the very first test above:
            // exactly 10 explicit entries (9 confident 220Hz + 1 unvoiced) so
            // this deterministically produces Timeout after 10 calls (9
            // confident < 10 minConfidentReadings).
            //
            // FakePitchEngine::reset() rewinds nextIndex to 0 (see
            // FakePitchEngine.h), so after restartCalibration() calls
            // engine.reset(), the SECOND attempt below replays this SAME
            // script from script[0] again - it does NOT continue from where
            // the first attempt left off. The second attempt's call counts
            // are built around that real behavior.
            std::vector<PitchFrame> script;
            for (int i = 0; i < 9; ++i)
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            PitchPipeline pipeline (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f); // 300ms, matches this file's convention

            PitchPipelineUpdate update { PitchPipelinePhase::Calibrating };
            for (int i = 0; i < 10; ++i) // first attempt's full 3000ms window
                update = pipeline.process (block.data(), block.size());
            expect (update.phase == PitchPipelinePhase::Calibrating);
            expect (update.calibrationStatus == CalibrationStatus::Timeout);

            pipeline.restartCalibration();

            // Second attempt, call 1: replays script[0] (confident 220Hz),
            // 300ms elapsed - well under the 3000ms window. If
            // restartCalibration() had forgotten to reset TonicCalibrator's
            // internal state, TonicCalibrator::processFrame() is idempotent
            // once its window has closed (see TonicCalibrator.cpp) and this
            // would incorrectly still read the stale Timeout instead of a
            // fresh InProgress - this is the critical regression-catching
            // assertion.
            auto afterRestart = pipeline.process (block.data(), block.size());
            expect (afterRestart.phase == PitchPipelinePhase::Calibrating);
            expect (afterRestart.calibrationStatus == CalibrationStatus::InProgress);

            // Second attempt, calls 2-10: replay script[1..9] - the same 8
            // confident + 1 unvoiced tail as the first attempt (call 1 above
            // already consumed script[0]) - proving a full second
            // calibration attempt can run to completion after a restart, not
            // just that the state got cleared.
            PitchPipelineUpdate secondUpdate { PitchPipelinePhase::Calibrating };
            for (int i = 0; i < 9; ++i)
                secondUpdate = pipeline.process (block.data(), block.size());
            expect (secondUpdate.phase == PitchPipelinePhase::Calibrating);
            expect (secondUpdate.calibrationStatus == CalibrationStatus::Timeout);
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
            // Feed 2048 samples (two full CREPE windows) in this single call to exercise
            // PitchPipeline's Live-phase handling against CrepePitchEngine's
            // multi-window-draining behavior, which no prior test covered.
            auto liveUpdate = pipeline.process (sine.data(), 2048);
            expect (liveUpdate.phase == PitchPipelinePhase::Live);
            expect (liveUpdate.swarLabel.has_value());
            if (liveUpdate.swarLabel.has_value())
                expectEquals ((int) liveUpdate.swarLabel->swar, (int) Swar::Sa);
        }
    }
};

static PitchPipelineTests pitchPipelineTestsInstance;
