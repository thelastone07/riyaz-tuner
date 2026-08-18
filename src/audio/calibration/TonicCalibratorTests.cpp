#include "TonicCalibrator.h"
#include "FakePitchEngine.h"
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
    }
};

static TonicCalibratorTests tonicCalibratorTestsInstance;
