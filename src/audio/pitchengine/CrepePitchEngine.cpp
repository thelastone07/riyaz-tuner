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
    resampledSamplesConsumed = 0;
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
    resampledSamplesConsumed = 0;
    resampledBuffer.clear();
    resampler.reset();
    continuityFilter.reset();
}

PitchFrame CrepePitchEngine::runInference (const float* window)
{
    // Timestamp reflects the START of the window being analyzed, in terms of
    // resampled (16kHz-domain) audio time - not wall-clock call time. This
    // advances in fixed kCrepeWindowSize/kCrepeSampleRate steps regardless of
    // caller block size or how many windows one processFrame() call drains.
    const uint64_t frameTimestamp = (uint64_t) ((double) resampledSamplesConsumed / kCrepeSampleRate * 1000.0);
    resampledSamplesConsumed += (uint64_t) kCrepeWindowSize;

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
    const uint64_t fallbackTimestamp = (uint64_t) ((double) resampledSamplesConsumed / kCrepeSampleRate * 1000.0);

    if (status == PitchEngineStatus::NotPrepared || status == PitchEngineStatus::LoadError)
        return PitchFrame { fallbackTimestamp, std::nullopt, 0.0f };

    const int maxOutputSamples = (int) ((double) numSamples / resampleSpeedRatio);
    if (maxOutputSamples > 0)
    {
        resampleScratch.resize ((size_t) maxOutputSamples);
        // LagrangeInterpolator::process() is output-driven: the caller specifies
        // how many OUTPUT samples are wanted, not how many input samples are fed.
        // We use the 6-argument overload (passing numInputSamplesAvailable) to
        // ensure it zero-pads safely instead of reading past the end of audioFrame
        // if the output request ever exceeds the input available (verified against
        // JUCE 8.0.7 in vcpkg_installed/x64-windows/include/JUCE-8.0.7/...).
        resampler.process (resampleSpeedRatio, audioFrame,
                            resampleScratch.data(), maxOutputSamples,
                            (int) numSamples, 0);
        resampledBuffer.insert (resampledBuffer.end(),
                                 resampleScratch.begin(), resampleScratch.begin() + maxOutputSamples);
    }

    // Drain every complete window this call's audio makes available, keeping
    // only the LATEST result. A live display only cares about the current
    // pitch - returning a stale first-of-several-windows result here would
    // reintroduce the lag this fix exists to remove, and never fully
    // draining would let resampledBuffer grow without bound on large blocks.
    PitchFrame result { fallbackTimestamp, std::nullopt, 0.0f };
    bool producedResult = false;

    while ((int) resampledBuffer.size() >= kCrepeWindowSize)
    {
        result = runInference (resampledBuffer.data());
        resampledBuffer.erase (resampledBuffer.begin(), resampledBuffer.begin() + kCrepeWindowSize);
        producedResult = true;
    }

    return producedResult ? result : PitchFrame { fallbackTimestamp, std::nullopt, 0.0f };
}

PitchEngineStatus CrepePitchEngine::getStatus() const
{
    return status.load();
}
