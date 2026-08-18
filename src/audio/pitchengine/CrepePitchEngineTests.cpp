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
    }
};

static CrepePitchEngineTests crepePitchEngineTestsInstance;
