#include "CrepePitchEngine.h"

CrepePitchEngine::CrepePitchEngine (juce::String modelPathIn)
    : modelPath (std::move (modelPathIn))
{
}

PitchEngineStatus CrepePitchEngine::prepare (double inputSampleRate)
{
    sampleRate = inputSampleRate;
    samplesProcessed = 0;

    juce::File modelFile (modelPath);
    if (! modelFile.existsAsFile())
    {
        status = PitchEngineStatus::LoadError;
        return status;
    }

    try
    {
        env = std::make_unique<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "riyaaz");
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads (1);

        const auto pathString = modelFile.getFullPathName();
        session = std::make_unique<Ort::Session> (
            *env, pathString.toWideCharPointer(), sessionOptions);

        status = PitchEngineStatus::Ok;
    }
    catch (const Ort::Exception&)
    {
        session.reset();
        env.reset();
        status = PitchEngineStatus::LoadError;
    }

    return status;
}

void CrepePitchEngine::reset()
{
    samplesProcessed = 0;
}

PitchFrame CrepePitchEngine::processFrame (const float*, size_t)
{
    // Real inference lands in Task 4. For now, before that's wired up (or if
    // prepare() failed/was never called), always report unvoiced rather than
    // crash or fabricate a result.
    return PitchFrame { samplesProcessed, std::nullopt, 0.0f };
}

PitchEngineStatus CrepePitchEngine::getStatus() const
{
    return status;
}
