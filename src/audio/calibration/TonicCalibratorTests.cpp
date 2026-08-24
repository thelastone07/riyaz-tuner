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
