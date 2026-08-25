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

            // The success path in runInference() explicitly sets status = Ok on
            // every successful inference. This is what allows recovery from a
            // prior transient InferenceError: as long as inference keeps
            // succeeding, status stays (or becomes) Ok rather than getting stuck.
            // We can't deterministically force a real Ort::Exception here (no
            // ONNX-session mocking seam), so this only confirms the success path
            // itself lands on Ok — the guard-logic half of the recovery fix
            // (processFrame() no longer permanently blocking on InferenceError)
            // is verified by code inspection, not by this test.
            expect (engine.getStatus() == PitchEngineStatus::Ok);
        }

        beginTest ("prepare() called twice in a row still works correctly");
        {
            CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
            auto firstPrepare = engine.prepare (16000.0);
            expect (firstPrepare == PitchEngineStatus::Ok);

            // Calling prepare() again must not use-after-free the ONNX Runtime
            // Env/Session (the first prepare()'s Session must be torn down
            // before its Env is), and the engine must remain fully usable.
            auto secondPrepare = engine.prepare (16000.0);
            expect (secondPrepare == PitchEngineStatus::Ok);
            expect (engine.getStatus() == PitchEngineStatus::Ok);

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
                expectWithinAbsoluteError (*lastDetected, 220.0f, 5.0f);
        }

        beginTest ("prepare() with sampleRate <= 0 returns LoadError, not UB");
        {
            CrepePitchEngine engineZero (juce::String ("models/crepe/small.onnx"));
            auto zeroStatus = engineZero.prepare (0.0);
            expect (zeroStatus == PitchEngineStatus::LoadError);
            expect (engineZero.getStatus() == PitchEngineStatus::LoadError);

            CrepePitchEngine engineNegative (juce::String ("models/crepe/small.onnx"));
            auto negativeStatus = engineNegative.prepare (-44100.0);
            expect (negativeStatus == PitchEngineStatus::LoadError);
            expect (engineNegative.getStatus() == PitchEngineStatus::LoadError);
        }

        beginTest ("processFrame() before prepare() returns timestampMs == 0, not garbage from a NaN cast");
        {
            CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
            // prepare() deliberately NOT called, so sampleRate is still 0.0.
            std::vector<float> silence (1024, 0.0f);
            auto frame = engine.processFrame (silence.data(), silence.size());
            expect (frame.timestampMs == 0);
        }

        beginTest ("PitchEngineStatus::InferenceError is a distinct status value");
        {
            // Triggering a real Ort::Exception mid-inference isn't deterministic
            // to reproduce in a unit test, so this simply proves the enumerator
            // exists and is distinguishable from the other statuses it must not
            // be confused with: LoadError (permanent, model failed to load) and
            // Ok (nothing wrong).
            expect (PitchEngineStatus::InferenceError != PitchEngineStatus::LoadError);
            expect (PitchEngineStatus::InferenceError != PitchEngineStatus::Ok);
        }

        beginTest ("A single large block spanning multiple windows drains all of them and returns only the latest result");
        {
            CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
            auto prepareStatus = engine.prepare (16000.0);
            expect (prepareStatus == PitchEngineStatus::Ok);

            // 4096 samples = exactly 4 windows at 16kHz native rate (no resampling).
            // First 3072 samples (3 windows) are silence; last 1024 (the 4th window)
            // is a clean 220Hz tone. The OLD one-window-per-call code would only
            // consume the first 1024 samples on this call (silence -> unvoiced) and
            // leave the rest buffered for a future call it might never receive. The
            // fixed code must drain all 4 windows in this one call and return the
            // 4th window's result.
            constexpr double sr = 16000.0;
            constexpr float freq = 220.0f;
            std::vector<float> block (4096, 0.0f);
            for (size_t i = 3072; i < block.size(); ++i)
                block[i] = std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) (i - 3072) / sr) * 0.5f;

            auto frame = engine.processFrame (block.data(), block.size());

            expect (frame.frequencyHz.has_value());
            if (frame.frequencyHz.has_value())
                expectWithinAbsoluteError (*frame.frequencyHz, 220.0f, 5.0f);
            expectEquals ((long long) frame.timestampMs, (long long) 192);
        }

        beginTest ("Timestamps advance in fixed 64ms increments per window, independent of call granularity");
        {
            CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
            engine.prepare (16000.0);

            std::vector<float> silence (1024, 0.0f);
            auto first = engine.processFrame (silence.data(), silence.size());
            auto second = engine.processFrame (silence.data(), silence.size());

            expectEquals ((long long) first.timestampMs, (long long) 0);
            expectEquals ((long long) second.timestampMs, (long long) 64); // 1024/16000*1000
        }
    }
};

static CrepePitchEngineTests crepePitchEngineTestsInstance;
