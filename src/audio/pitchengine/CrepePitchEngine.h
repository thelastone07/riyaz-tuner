#pragma once
#include "PitchEngine.h"
#include "PitchContinuityFilter.h"
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
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
    static constexpr double kCrepeSampleRate = 16000.0;
    static constexpr int kCrepeWindowSize = 1024;
    static constexpr float kMinConfidence = 0.5f;

    juce::String modelPath;
    PitchEngineStatus status = PitchEngineStatus::NotPrepared;
    double sampleRate = 0.0;
    uint64_t samplesProcessed = 0;

    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> session;

    juce::LagrangeInterpolator resampler;
    double resampleSpeedRatio = 1.0; // input samples per output sample (sampleRate / kCrepeSampleRate)
    std::vector<float> resampledBuffer; // accumulates resampled samples until a full window is available
    std::vector<float> resampleScratch;

    PitchContinuityFilter continuityFilter;

    PitchFrame runInference (const float* window);
};
