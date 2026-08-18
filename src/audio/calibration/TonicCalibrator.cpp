#include "TonicCalibrator.h"
#include <algorithm>

TonicCalibrator::TonicCalibrator (PitchEngine& engineIn, double sampleRateIn, uint64_t windowMsIn)
    : engine (engineIn), sampleRate (sampleRateIn), windowMs (windowMsIn)
{
}

CalibrationResult TonicCalibrator::processFrame (const float* audioFrame, size_t numSamples)
{
    if (finalResult.status != CalibrationStatus::InProgress)
        return finalResult; // idempotent once the window has closed

    PitchFrame frame = engine.processFrame (audioFrame, numSamples);
    if (frame.frequencyHz.has_value())
        confidentFrequencies.push_back (*frame.frequencyHz);

    elapsedMs += (uint64_t) ((double) numSamples / sampleRate * 1000.0);

    if (elapsedMs < windowMs)
        return { CalibrationStatus::InProgress, std::nullopt };

    // Window closed - classify. (Timeout/Unstable logic lands in Tasks 2-3;
    // for this task, treat a non-empty result as always Success.)
    if (confidentFrequencies.empty())
    {
        finalResult = { CalibrationStatus::Timeout, std::nullopt };
        return finalResult;
    }

    std::vector<float> sorted = confidentFrequencies;
    std::sort (sorted.begin(), sorted.end());
    const float median = sorted[sorted.size() / 2];

    finalResult = { CalibrationStatus::Success, median };
    return finalResult;
}

void TonicCalibrator::reset()
{
    elapsedMs = 0;
    confidentFrequencies.clear();
    finalResult = { CalibrationStatus::InProgress, std::nullopt };
}
