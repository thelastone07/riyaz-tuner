#include "TonicCalibrator.h"
#include <algorithm>
#include <cmath>

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

    // Window closed - classify.
    if (confidentFrequencies.empty())
    {
        finalResult = { CalibrationStatus::Timeout, std::nullopt };
        return finalResult;
    }

    std::vector<float> sorted = confidentFrequencies;
    std::sort (sorted.begin(), sorted.end());
    const float median = sorted[sorted.size() / 2];

    // Instability check: how far (in cents) does the widest outlier sit from
    // the median? A held note jitters by a few cents; unstable/wandering
    // pitch spans much more. Threshold chosen generously above normal vibrato
    // range (typically under 100 cents) but well below a semitone (100
    // cents) to catch genuinely unstable input without flagging vibrato.
    constexpr float kMaxSpreadCents = 80.0f;
    const float minCents = 1200.0f * std::log2 (sorted.front() / median);
    const float maxCents = 1200.0f * std::log2 (sorted.back() / median);
    const float spread = std::max (std::abs (minCents), std::abs (maxCents));

    if (spread > kMaxSpreadCents)
    {
        finalResult = { CalibrationStatus::Unstable, std::nullopt };
        return finalResult;
    }

    finalResult = { CalibrationStatus::Success, median };
    return finalResult;
}

void TonicCalibrator::reset()
{
    elapsedMs = 0;
    confidentFrequencies.clear();
    finalResult = { CalibrationStatus::InProgress, std::nullopt };
}
