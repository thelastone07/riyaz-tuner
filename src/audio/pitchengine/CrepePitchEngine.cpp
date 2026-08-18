#include "CrepePitchEngine.h"
#include "CrepeDecode.h"

CrepePitchEngine::CrepePitchEngine (juce::String modelPathIn)
    : modelPath (std::move (modelPathIn))
{
}

PitchEngineStatus CrepePitchEngine::prepare (double inputSampleRate)
{
    // ONNX Runtime requires the Env to outlive every Session created from it.
    // Reset in this order (Session first, then Env) unconditionally, every
    // time prepare() is called (including the first, where both are already
    // null and this is a no-op), so a second prepare() call never destroys
    // the Env while the old Session is still alive.
    session.reset();
    env.reset();

    sampleRate = inputSampleRate;
    samplesProcessed = 0;
    resampledBuffer.clear();
    resampler.reset();
    continuityFilter.reset();

    if (sampleRate <= 0.0)
    {
        status = PitchEngineStatus::LoadError;
        return status;
    }

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
    catch (const std::exception&)
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
    const uint64_t frameTimestamp = sampleRate > 0.0
        ? (uint64_t) ((double) samplesProcessed / sampleRate * 1000.0)
        : 0;

    // The try block wraps only the ONNX-specific work: tensor construction,
    // session->Run, reading/validating the output tensor, and decoding it.
    // Nothing outside this block can throw an Ort::Exception, so status/
    // continuityFilter handling lives outside it (see below).
    CrepeDecodeResult decoded {};
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

        const size_t elementCount = outputTensors[0].GetTensorTypeAndShapeInfo().GetElementCount();
        if (elementCount != 360)
        {
            status = PitchEngineStatus::InferenceError;
            return PitchFrame { frameTimestamp, std::nullopt, 0.0f };
        }

        const float* probabilities = outputTensors[0].GetTensorMutableData<float>();
        decoded = decodeCrepeOutput (probabilities, 360);
    }
    catch (const Ort::Exception&)
    {
        status = PitchEngineStatus::InferenceError;
        return PitchFrame { frameTimestamp, std::nullopt, 0.0f };
    }
    catch (const std::exception&)
    {
        status = PitchEngineStatus::InferenceError;
        return PitchFrame { frameTimestamp, std::nullopt, 0.0f };
    }

    PitchFrame frame;
    frame.timestampMs = frameTimestamp;
    frame.confidence = decoded.confidence;
    frame.frequencyHz = decoded.confidence >= kMinConfidence
        ? std::optional<float> (decoded.frequencyHz)
        : std::nullopt;

    status = PitchEngineStatus::Ok;
    return continuityFilter.process (frame);
}

PitchFrame CrepePitchEngine::processFrame (const float* audioFrame, size_t numSamples)
{
    const uint64_t frameTimestamp = sampleRate > 0.0
        ? (uint64_t) ((double) samplesProcessed / sampleRate * 1000.0)
        : 0;
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
