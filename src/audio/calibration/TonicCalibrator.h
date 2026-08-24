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
    // minConfidentReadingsIn is a floor on how much confident evidence must be
    // gathered before a Success/Unstable classification is trusted, independent
    // of how many total processFrame() calls it took to gather it. This matters
    // because PitchEngine::processFrame() legitimately returns an unvoiced
    // frame (frequencyHz == nullopt) for two different reasons: the audio is
    // genuinely unvoiced/silent, or the engine is still internally buffering
    // samples toward a full analysis window and hasn't produced an inference
    // result at all yet. TonicCalibrator cannot distinguish these two cases,
    // so the ratio of confident-to-total calls reflects the engine's internal
    // buffering behavior (e.g. callback block size vs. analysis window size)
    // as much as it reflects singer behavior, and is not a reliable gate. An
    // absolute minimum count of confident readings sidesteps that: it only
    // asks "did we accumulate enough good readings overall," which holds
    // regardless of how many buffering-only calls happened in between.
    TonicCalibrator (PitchEngine& engineIn, double sampleRateIn, uint64_t windowMsIn = 3000, int minConfidentReadingsIn = 10);

    CalibrationResult processFrame (const float* audioFrame, size_t numSamples);
    void reset();

private:
    PitchEngine& engine;
    double sampleRate;
    uint64_t windowMs;
    int minConfidentReadings;

    uint64_t elapsedMs = 0;
    uint64_t samplesSeen = 0;
    std::vector<float> confidentFrequencies;
    CalibrationResult finalResult { CalibrationStatus::InProgress, std::nullopt };
};
