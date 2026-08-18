#pragma once
#include "../pitchengine/PitchEngine.h"
#include <vector>

// Test double: returns a pre-scripted sequence of PitchFrames, one per
// processFrame() call, regardless of the actual audio content passed in.
// Once the script is exhausted, repeats the last frame. Lets calibrator
// tests run deterministically without real audio or ONNX Runtime.
class FakePitchEngine : public PitchEngine
{
public:
    explicit FakePitchEngine (std::vector<PitchFrame> scriptedFrames)
        : script (std::move (scriptedFrames))
    {
    }

    PitchEngineStatus prepare (double) override
    {
        status = PitchEngineStatus::Ok;
        return status;
    }

    void reset() override
    {
        nextIndex = 0;
    }

    PitchFrame processFrame (const float*, size_t) override
    {
        if (script.empty())
            return PitchFrame { 0, std::nullopt, 0.0f };

        const auto& frame = script[std::min (nextIndex, script.size() - 1)];
        if (nextIndex < script.size() - 1)
            ++nextIndex;
        return frame;
    }

    PitchEngineStatus getStatus() const override { return status; }

private:
    std::vector<PitchFrame> script;
    size_t nextIndex = 0;
    PitchEngineStatus status = PitchEngineStatus::NotPrepared;
};
