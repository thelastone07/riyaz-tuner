// src/audio/pipeline/PitchPipeline.h
#pragma once
#include "../pitchengine/PitchEngine.h"
#include "../calibration/TonicCalibrator.h"
#include "../swarmap/SwarMapper.h"
#include <cmath>
#include <optional>

enum class PitchPipelinePhase { Calibrating, Live };

struct PitchPipelineUpdate
{
    PitchPipelinePhase phase;
    CalibrationStatus calibrationStatus = CalibrationStatus::InProgress;
    std::optional<SwarLabel> swarLabel;
    std::optional<float> centsFromSa;
    float saHz = 0.0f;
};

class PitchPipeline
{
public:
    // calibrationWindowMsIn is passed straight through to TonicCalibrator's
    // own windowMs constructor argument (default 3000, matching
    // TonicCalibrator's own default) - exposed here so tests (and,
    // eventually, a "quick recalibrate" UI mode) can use a shorter window
    // without needing a separate code path.
    PitchPipeline (PitchEngine& engineIn, double sampleRateIn, uint64_t calibrationWindowMsIn = 3000);

    PitchPipelineUpdate process (const float* audioFrame, size_t numSamples);
    void restartCalibration();

private:
    PitchEngine& engine;
    double sampleRate;
    TonicCalibrator calibrator; // constructed with calibrationWindowMs; not itself stored elsewhere, since nothing after construction needs to re-read it
    SwarMapper swarMapper;
    PitchPipelinePhase phase = PitchPipelinePhase::Calibrating;
    float saHz = 0.0f;

    PitchPipelineUpdate handleCalibrating (const float* audioFrame, size_t numSamples);
    PitchPipelineUpdate handleLive (const float* audioFrame, size_t numSamples);
};
