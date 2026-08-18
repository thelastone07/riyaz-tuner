#include "CrepePitchEngine.h"
#include <juce_core/juce_core.h>

class CrepePitchEngineTests : public juce::UnitTest
{
public:
    CrepePitchEngineTests() : juce::UnitTest ("CrepePitchEngine", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("prepare() with a valid model path returns Ok and getStatus() agrees");
        {
            CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
            auto status = engine.prepare (44100.0);
            expect (status == PitchEngineStatus::Ok);
            expect (engine.getStatus() == PitchEngineStatus::Ok);
        }

        beginTest ("prepare() with a missing model path returns LoadError, not a crash");
        {
            CrepePitchEngine engine (juce::String ("models/crepe/does_not_exist.onnx"));
            auto status = engine.prepare (44100.0);
            expect (status == PitchEngineStatus::LoadError);
            expect (engine.getStatus() == PitchEngineStatus::LoadError);
        }

        beginTest ("processFrame() before prepare() returns an unvoiced frame, not a crash");
        {
            CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
            // prepare() deliberately NOT called
            std::vector<float> silence (1024, 0.0f);
            auto frame = engine.processFrame (silence.data(), silence.size());
            expect (! frame.frequencyHz.has_value());
        }

        beginTest ("A synthetic 220Hz sine wave is detected at approximately 220Hz");
        {
            CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
            auto prepareStatus = engine.prepare (16000.0); // feed at CREPE's native rate to avoid resampler-accuracy as a variable in this test
            expect (prepareStatus == PitchEngineStatus::Ok);

            // Generate 1 second of 220Hz sine at 16kHz, fed in 1024-sample chunks
            // (matching CREPE's window size) so the engine has enough for several
            // inferences.
            constexpr double sr = 16000.0;
            constexpr float freq = 220.0f;
            std::vector<float> sine (16000);
            for (size_t i = 0; i < sine.size(); ++i)
                sine[i] = std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / sr) * 0.5f;

            std::optional<float> lastDetected;
            for (size_t offset = 0; offset + 1024 <= sine.size(); offset += 1024)
            {
                auto frame = engine.processFrame (sine.data() + offset, 1024);
                if (frame.frequencyHz.has_value() && frame.confidence > 0.5f)
                    lastDetected = frame.frequencyHz;
            }

            expect (lastDetected.has_value());
            if (lastDetected.has_value())
                expectWithinAbsoluteError (*lastDetected, 220.0f, 5.0f); // CREPE bin quantization is ~20 cents (~2.5Hz at 220Hz), 5Hz tolerance is safe
        }
    }
};

static CrepePitchEngineTests crepePitchEngineTestsInstance;
