#include "TonicCalibrator.h"
#include "FakePitchEngine.h"
#include "../pitchengine/CrepePitchEngine.h"
#include <juce_core/juce_core.h>
#include <vector>

class TonicCalibratorTests : public juce::UnitTest
{
public:
    TonicCalibratorTests() : juce::UnitTest ("TonicCalibrator", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("A window of stable confident frames succeeds with the median frequency as Sa");
        {
            // 10 frames, all close to 220Hz with minor jitter, all confident.
            std::vector<PitchFrame> script;
            const float freqs[] = { 219.5f, 220.1f, 219.8f, 220.3f, 220.0f,
                                     219.9f, 220.2f, 220.0f, 219.7f, 220.4f };
            for (float f : freqs)
                script.push_back (PitchFrame { 0, f, 0.9f });

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            // Feed frames in 300ms chunks (10 * 300ms = 3000ms = default window)
            TonicCalibrator calibrator (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f); // 300ms of silence (content ignored by the fake)

            CalibrationResult result { CalibrationStatus::InProgress, std::nullopt };
            for (int i = 0; i < 10; ++i)
                result = calibrator.processFrame (block.data(), block.size());

            expect (result.status == CalibrationStatus::Success);
            expect (result.saHz.has_value());
            if (result.saHz.has_value())
                expectWithinAbsoluteError (*result.saHz, 220.0f, 1.0f); // median of the jittery values, well within 1Hz
        }

        beginTest ("A window with zero confident frames (user never sings) reports Timeout");
        {
            // Script of all-unvoiced frames - simulates silence for the whole window.
            std::vector<PitchFrame> script;
            for (int i = 0; i < 10; ++i)
                script.push_back (PitchFrame { 0, std::nullopt, 0.0f });

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            TonicCalibrator calibrator (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f);

            CalibrationResult result { CalibrationStatus::InProgress, std::nullopt };
            for (int i = 0; i < 10; ++i)
                result = calibrator.processFrame (block.data(), block.size());

            expect (result.status == CalibrationStatus::Timeout);
            expect (! result.saHz.has_value());
        }

        beginTest ("Confident frames spread across a wide pitch range (unstable singing) report Unstable, not Success");
        {
            // Frames confidently detected but spanning ~400 cents (a wide, unstable
            // range - not the tight jitter of a held note).
            std::vector<PitchFrame> script;
            const float freqs[] = { 200.0f, 220.0f, 240.0f, 195.0f, 250.0f,
                                     205.0f, 235.0f, 210.0f, 245.0f, 200.0f };
            for (float f : freqs)
                script.push_back (PitchFrame { 0, f, 0.9f });

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            TonicCalibrator calibrator (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f);

            CalibrationResult result { CalibrationStatus::InProgress, std::nullopt };
            for (int i = 0; i < 10; ++i)
                result = calibrator.processFrame (block.data(), block.size());

            expect (result.status == CalibrationStatus::Unstable);
            expect (! result.saHz.has_value());
        }

        beginTest ("Calling processFrame() after the window has closed returns the same final result without re-classifying");
        {
            std::vector<PitchFrame> script;
            for (int i = 0; i < 10; ++i)
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            TonicCalibrator calibrator (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f);

            CalibrationResult result { CalibrationStatus::InProgress, std::nullopt };
            for (int i = 0; i < 10; ++i)
                result = calibrator.processFrame (block.data(), block.size());
            expect (result.status == CalibrationStatus::Success);

            // One more call past the window - must return the SAME result, not
            // re-run classification on a now-stale confidentFrequencies list.
            auto again = calibrator.processFrame (block.data(), block.size());
            expect (again.status == CalibrationStatus::Success);
            expectWithinAbsoluteError (*again.saHz, *result.saHz, 0.001f);
        }

        beginTest ("reset() clears state so a fresh calibration window can start");
        {
            std::vector<PitchFrame> script;
            for (int i = 0; i < 10; ++i)
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            TonicCalibrator calibrator (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f);

            for (int i = 0; i < 10; ++i)
                calibrator.processFrame (block.data(), block.size());

            calibrator.reset();
            engine.reset();

            // Immediately after reset, a single short frame should NOT have closed
            // the window yet (elapsedMs should be back to 0).
            auto result = calibrator.processFrame (block.data(), block.size());
            expect (result.status == CalibrationStatus::InProgress);
        }

        beginTest ("A mixed window with confident frames below the absolute minimum reports Timeout, not Success");
        {
            // 2 confident frames out of 10 total calls - non-zero confident
            // frequencies exist, but there isn't enough signal to trust them as
            // a calibration. This is now gated on an absolute minimum count
            // (minConfidentReadings, default 10), not a ratio of confident to
            // total calls - see the new "total call count doesn't matter" test
            // below for why the absolute count is the right gate.
            std::vector<PitchFrame> script;
            script.push_back (PitchFrame { 0, 220.0f, 0.9f });
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });
            script.push_back (PitchFrame { 0, 221.0f, 0.9f });
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            TonicCalibrator calibrator (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f);

            CalibrationResult result { CalibrationStatus::InProgress, std::nullopt };
            for (int i = 0; i < 10; ++i)
                result = calibrator.processFrame (block.data(), block.size());

            expect (result.status == CalibrationStatus::Timeout);
            expect (! result.saHz.has_value());
        }

        beginTest ("8 confident readings report Timeout regardless of how many total calls it took to gather them (absolute minimum, not a ratio)");
        {
            // With the default minConfidentReadings=10, 8 confident readings is
            // below the floor and should report Timeout - and, crucially, it
            // should report Timeout the same way whether those 8 confident
            // readings arrived among 10 total calls or among 30 total calls.
            // Under the old ratio-based gate, 8/10 (80%) would have passed while
            // 8/30 (~27%) would have failed - the whole point of the new gate
            // is that neither total call count matters, only the absolute count
            // of confident readings.

            // Variant A: 8 confident out of 10 total calls (300ms blocks, the
            // default window-closing cadence used elsewhere in this file).
            {
                std::vector<PitchFrame> script;
                for (int i = 0; i < 8; ++i)
                    script.push_back (PitchFrame { 0, 220.0f, 0.9f });
                script.push_back (PitchFrame { 0, std::nullopt, 0.0f });
                script.push_back (PitchFrame { 0, std::nullopt, 0.0f });

                FakePitchEngine engine (script);
                engine.prepare (44100.0);

                TonicCalibrator calibrator (engine, 44100.0);
                std::vector<float> block (44100 * 300 / 1000, 0.0f); // 300ms * 10 = 3000ms window

                CalibrationResult result { CalibrationStatus::InProgress, std::nullopt };
                for (int i = 0; i < 10; ++i)
                    result = calibrator.processFrame (block.data(), block.size());

                expect (result.status == CalibrationStatus::Timeout);
                expect (! result.saHz.has_value());
            }

            // Variant B: the same 8 confident readings, but this time spread
            // among 30 total calls (100ms blocks, so the window still closes at
            // 3000ms) - simulating many extra calls that returned nullopt purely
            // because the underlying engine was still buffering samples toward
            // its analysis window, not because the singer stayed silent. The
            // extra 20 buffering-only calls must not change the outcome.
            {
                std::vector<PitchFrame> script;
                for (int i = 0; i < 8; ++i)
                    script.push_back (PitchFrame { 0, 220.0f, 0.9f });
                for (int i = 0; i < 22; ++i)
                    script.push_back (PitchFrame { 0, std::nullopt, 0.0f });

                FakePitchEngine engine (script);
                engine.prepare (44100.0);

                TonicCalibrator calibrator (engine, 44100.0);
                std::vector<float> block (44100 * 100 / 1000, 0.0f); // 100ms * 30 = 3000ms window

                CalibrationResult result { CalibrationStatus::InProgress, std::nullopt };
                for (int i = 0; i < 30; ++i)
                    result = calibrator.processFrame (block.data(), block.size());

                expect (result.status == CalibrationStatus::Timeout);
                expect (! result.saHz.has_value());
            }
        }

        beginTest ("reset() resets the injected engine, clearing any stale continuity-filter state before a retry");
        {
            std::vector<PitchFrame> script;
            for (int i = 0; i < 5; ++i)
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });

            FakePitchEngine engine (script);
            engine.prepare (44100.0);
            expect (! engine.wasReset);

            TonicCalibrator calibrator (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f);
            for (int i = 0; i < 3; ++i)
                calibrator.processFrame (block.data(), block.size());

            calibrator.reset();

            expect (engine.wasReset);
        }

        beginTest ("A single outlier frame among tightly-clustered frames reports Success (percentile-based instability gate), not Unstable");
        {
            // 9 frames at exactly 220Hz plus 1 outlier at 250Hz. Old min/max
            // based spread: 1200*log2(250/220) =~ 221 cents from the median -
            // would have failed as Unstable. New 5th/95th percentile spread
            // excludes that single outlier (with 10 sorted values, p5Index=0
            // and p95Index=8, both landing on 220Hz values), so the computed
            // spread is 0 cents and calibration succeeds (Fix 4).
            std::vector<PitchFrame> script;
            for (int i = 0; i < 9; ++i)
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });
            script.push_back (PitchFrame { 0, 250.0f, 0.9f });

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            TonicCalibrator calibrator (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f);

            CalibrationResult result { CalibrationStatus::InProgress, std::nullopt };
            for (int i = 0; i < 10; ++i)
                result = calibrator.processFrame (block.data(), block.size());

            expect (result.status == CalibrationStatus::Success);
            expect (result.saHz.has_value());
            if (result.saHz.has_value())
                expectWithinAbsoluteError (*result.saHz, 220.0f, 0.001f);
        }

        beginTest ("End-to-end: a real 220Hz sine through CrepePitchEngine calibrates to ~220Hz Sa");
        {
            CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
            auto prepareStatus = engine.prepare (16000.0); // native rate, same reasoning as the PitchEngine plan's sine test
            expect (prepareStatus == PitchEngineStatus::Ok);

            TonicCalibrator calibrator (engine, 16000.0, 1000); // shorter window for test speed - 1s instead of 3s

            constexpr double sr = 16000.0;
            constexpr float freq = 220.0f;
            // 16 full 1024-sample chunks (1024ms) are needed to close the 1000ms
            // window: each chunk is exactly 1024/16000*1000 = 64ms, so 15 chunks
            // (from an exact 16000-sample/1.0s buffer) only total 960ms and would
            // leave the calibrator stuck InProgress forever. 16384 samples guarantees
            // 16 full chunks feed through the offset+1024<=size loop below.
            std::vector<float> sine (16384);
            for (size_t i = 0; i < sine.size(); ++i)
                sine[i] = std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / sr) * 0.5f;

            CalibrationResult result { CalibrationStatus::InProgress, std::nullopt };
            for (size_t offset = 0; offset + 1024 <= sine.size(); offset += 1024)
                result = calibrator.processFrame (sine.data() + offset, 1024);

            expect (result.status == CalibrationStatus::Success);
            if (result.status == CalibrationStatus::Success && result.saHz.has_value())
                expectWithinAbsoluteError (*result.saHz, 220.0f, 5.0f);
        }
    }
};

static TonicCalibratorTests tonicCalibratorTestsInstance;
