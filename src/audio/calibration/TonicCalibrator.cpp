#include "TonicCalibrator.h"
#include <algorithm>
#include <cmath>

TonicCalibrator::TonicCalibrator (PitchEngine& engineIn, double sampleRateIn, uint64_t windowMsIn, int minConfidentReadingsIn)
    : engine (engineIn), sampleRate (sampleRateIn), windowMs (windowMsIn), minConfidentReadings (minConfidentReadingsIn)
{
}

CalibrationResult TonicCalibrator::processFrame (const float* audioFrame, size_t numSamples)
{
    if (finalResult.status != CalibrationStatus::InProgress)
        return finalResult; // idempotent once the window has closed

    PitchFrame frame = engine.processFrame (audioFrame, numSamples);
    if (frame.frequencyHz.has_value())
        confidentFrequencies.push_back (*frame.frequencyHz);

    samplesSeen += numSamples;
    elapsedMs = (uint64_t) ((double) samplesSeen / sampleRate * 1000.0);

    if (elapsedMs < windowMs)
        return { CalibrationStatus::InProgress, std::nullopt };

    // Window closed - classify. Require at least minConfidentReadings confident
    // frames to have been gathered, regardless of how many total processFrame()
    // calls occurred (see the constructor doc comment for why an absolute
    // count, not a ratio, is the right gate here).
    if (confidentFrequencies.size() < (size_t) minConfidentReadings)
    {
        finalResult = { CalibrationStatus::Timeout, std::nullopt };
        return finalResult;
    }

    std::vector<float> sorted = confidentFrequencies;
    std::sort (sorted.begin(), sorted.end());
    const float median = sorted[sorted.size() / 2];

    // Instability check: how far (in cents) do the 5th/95th percentile bounds
    // sit from the median? A held note jitters by a few cents; unstable/
    // wandering pitch spans much more. Using percentiles instead of raw
    // min/max keeps a single outlier/glitch frame from failing an otherwise
    // stable take. 80 cents is roughly 4/5 of a semitone. This is the
    // threshold each of the 5th/95th percentile bounds may deviate from the
    // median (so the full permitted range spans up to ~160 cents) - wide
    // enough to tolerate normal vibrato and a few outlier/glitch frames
    // without discarding a genuinely stable take, narrow enough to reject an
    // obviously unstable or wandering pitch.
    constexpr float kMaxSpreadCents = 80.0f;
    const size_t lastIndex = sorted.size() - 1;
    const size_t p5Index = std::min (lastIndex, (size_t) (0.05 * (double) lastIndex));
    const size_t p95Index = std::min (lastIndex, (size_t) (0.95 * (double) lastIndex));
    const float minCents = 1200.0f * std::log2 (sorted[p5Index] / median);
    const float maxCents = 1200.0f * std::log2 (sorted[p95Index] / median);
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
    engine.reset();
    elapsedMs = 0;
    samplesSeen = 0;
    confidentFrequencies.clear();
    finalResult = { CalibrationStatus::InProgress, std::nullopt };
}
