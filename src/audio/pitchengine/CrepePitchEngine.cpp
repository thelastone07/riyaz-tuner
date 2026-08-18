#include "CrepePitchEngine.h"
#include "CrepeDecode.h"

CrepePitchEngine::CrepePitchEngine (juce::String modelPathIn)
    : modelPath (std::move (modelPathIn))
{
}

PitchEngineStatus CrepePitchEngine::prepare (double inputSampleRate)
{
    sampleRate = inputSampleRate;
    samplesProcessed = 0;
    resampledBuffer.clear();
    resampler.reset();
    continuityFilter.reset();
    resampleSpeedRatio = sampleRate / kCrepeSampleRate; // input samples consumed per output sample

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
    resampledBuffer.clear();
    resampler.reset();
    continuityFilter.reset();
}

PitchFrame CrepePitchEngine::runInference (const float* window)
{
    try
    {
        Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);

        std::array<int64_t, 2> inputShape { 1, kCrepeWindowSize };
        Ort::Value inputTensor = Ort::Value::CreateTensor<float> (
            memoryInfo, const_cast<float*> (window), (size_t) kCrepeWindowSize,
            inputShape.data(), inputShape.size());

        const char* inputNames[] = { "frames" };
        const char* outputNames[] = { "probabilities" };

        auto outputTensors = session->Run (Ort::RunOptions { nullptr },
                                            inputNames, &inputTensor, 1,
                                            outputNames, 1);

        const float* probabilities = outputTensors[0].GetTensorMutableData<float>();
        auto decoded = decodeCrepeOutput (probabilities, 360);

        PitchFrame frame;
        frame.timestampMs = (uint64_t) ((double) samplesProcessed / sampleRate * 1000.0);
        frame.confidence = decoded.confidence;
        frame.frequencyHz = decoded.confidence >= kMinConfidence
            ? std::optional<float> (decoded.frequencyHz)
            : std::nullopt;

        status = PitchEngineStatus::Ok;
        return continuityFilter.process (frame);
    }
    catch (const Ort::Exception&)
    {
        status = PitchEngineStatus::InferenceError;
        return PitchFrame {
            (uint64_t) ((double) samplesProcessed / sampleRate * 1000.0),
            std::nullopt,
            0.0f
        };
    }
}

PitchFrame CrepePitchEngine::processFrame (const float* audioFrame, size_t numSamples)
{
    const uint64_t frameTimestamp = (uint64_t) ((double) samplesProcessed / sampleRate * 1000.0);
    samplesProcessed += numSamples;

    if (status == PitchEngineStatus::NotPrepared || status == PitchEngineStatus::LoadError)
        return PitchFrame { frameTimestamp, std::nullopt, 0.0f };

    // LagrangeInterpolator::process() is output-driven: you tell it how many
    // OUTPUT samples you want, not how many input samples you're feeding it.
    // Use the 6-argument overload with an explicit numInputSamplesAvailable
    // so it zero-pads instead of reading past the end of audioFrame if the
    // conservative floor() below ever asks for slightly more than is truly
    // available (verified against JUCE 8.0.7's juce_GenericInterpolator.h).
    const int maxOutputSamples = (int) ((double) numSamples / resampleSpeedRatio);
    if (maxOutputSamples > 0)
    {
        resampleScratch.resize ((size_t) maxOutputSamples);
        resampler.process (resampleSpeedRatio, audioFrame,
                            resampleScratch.data(), maxOutputSamples,
                            (int) numSamples, 0);
        resampledBuffer.insert (resampledBuffer.end(),
                                 resampleScratch.begin(), resampleScratch.begin() + maxOutputSamples);
    }

    if ((int) resampledBuffer.size() < kCrepeWindowSize)
        return PitchFrame { frameTimestamp, std::nullopt, 0.0f };

    PitchFrame result = runInference (resampledBuffer.data());
    resampledBuffer.erase (resampledBuffer.begin(), resampledBuffer.begin() + kCrepeWindowSize);
    return result;
}

PitchEngineStatus CrepePitchEngine::getStatus() const
{
    return status;
}
