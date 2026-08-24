#pragma once
#include "../pitchengine/PitchEngine.h"
#include <cstdint>
#include <optional>
#include <vector>

enum class CalibrationStatus
{
    InProgress,
    Success,
    Timeout,
    Unstable
};

struct CalibrationResult
{
    CalibrationStatus status;
    std::optional<float> saHz;
};

class TonicCalibrator
{
public:
    TonicCalibrator (PitchEngine& engineIn, double sampleRateIn, uint64_t windowMsIn = 3000);

    CalibrationResult processFrame (const float* audioFrame, size_t numSamples);
    void reset();

private:
    PitchEngine& engine;
    double sampleRate;
    uint64_t windowMs;

    uint64_t elapsedMs = 0;
    uint64_t samplesSeen = 0;
    int totalFramesInWindow = 0;
    std::vector<float> confidentFrequencies;
    CalibrationResult finalResult { CalibrationStatus::InProgress, std::nullopt };
};
