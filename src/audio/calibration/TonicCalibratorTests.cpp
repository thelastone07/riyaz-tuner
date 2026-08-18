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
    }
};

static TonicCalibratorTests tonicCalibratorTestsInstance;
