// src/audio/pipeline/PitchPipeline.cpp
#include "PitchPipeline.h"

PitchPipeline::PitchPipeline (PitchEngine& engineIn, double sampleRateIn, uint64_t calibrationWindowMsIn)
    : engine (engineIn), sampleRate (sampleRateIn),
      calibrator (engineIn, sampleRateIn, calibrationWindowMsIn)
{
}

PitchPipelineUpdate PitchPipeline::handleCalibrating (const float* audioFrame, size_t numSamples)
{
    auto result = calibrator.processFrame (audioFrame, numSamples);

    PitchPipelineUpdate update { PitchPipelinePhase::Calibrating };
    update.calibrationStatus = result.status;

    if (result.status == CalibrationStatus::Success && result.saHz.has_value())
    {
        saHz = *result.saHz;
        update.saHz = saHz;
        phase = PitchPipelinePhase::Live;
        // Report this call as still Calibrating/Success - the FIRST Live
        // frame is produced on the NEXT process() call, so this call's audio
        // is never processed through two different logical paths at once.
    }

    return update;
}

PitchPipelineUpdate PitchPipeline::handleLive (const float* audioFrame, size_t numSamples)
{
    PitchPipelineUpdate update { PitchPipelinePhase::Live };
    update.saHz = saHz;

    PitchFrame frame = engine.processFrame (audioFrame, numSamples);
    update.timestampMs = frame.timestampMs;

    if (! frame.frequencyHz.has_value())
        return update; // unvoiced - do NOT call swarMapper.update(), preserving its hysteresis lock

    const float cents = 1200.0f * std::log2 (*frame.frequencyHz / saHz);
    update.centsFromSa = cents;
    update.swarLabel = swarMapper.update (cents);

    return update;
}

PitchPipelineUpdate PitchPipeline::process (const float* audioFrame, size_t numSamples)
{
    return phase == PitchPipelinePhase::Calibrating
        ? handleCalibrating (audioFrame, numSamples)
        : handleLive (audioFrame, numSamples);
}

void PitchPipeline::restartCalibration()
{
    engine.reset();
    calibrator.reset();
    swarMapper.reset();
    phase = PitchPipelinePhase::Calibrating;
    saHz = 0.0f;
}

void PitchPipeline::startLiveWithKnownSa (float knownSaHz)
{
    saHz = knownSaHz;
    phase = PitchPipelinePhase::Live;
    // Redundant given a fresh PitchPipeline already constructs a fresh
    // SwarMapper, but cheap and makes the intent explicit rather than
    // relying on that invariant silently - this is a genuine "start fresh"
    // entry point, not just a phase flag flip.
    swarMapper.reset();
}
