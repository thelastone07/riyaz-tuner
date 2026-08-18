#pragma once
#include "PitchEngine.h"
#include <juce_core/juce_core.h>
#include <onnxruntime_cxx_api.h>
#include <memory>
#include <vector>

class CrepePitchEngine : public PitchEngine
{
public:
    explicit CrepePitchEngine (juce::String modelPath);

    PitchEngineStatus prepare (double inputSampleRate) override;
    void reset() override;
    PitchFrame processFrame (const float* audioFrame, size_t numSamples) override;
    PitchEngineStatus getStatus() const override;

private:
    juce::String modelPath;
    PitchEngineStatus status = PitchEngineStatus::NotPrepared;
    double sampleRate = 0.0;
    uint64_t samplesProcessed = 0;

    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> session;
};
