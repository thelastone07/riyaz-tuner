#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>

struct PitchFrame
{
    uint64_t timestampMs;
    std::optional<float> frequencyHz;
    float confidence; // 0.0-1.0
};

enum class PitchEngineStatus
{
    Ok,
    NotPrepared,
    LoadError,
    InferenceError
};

class PitchEngine
{
public:
    virtual ~PitchEngine() = default;

    // Loads model/resources and prepares for the given input sample rate.
    // Returns LoadError if the model/resources could not be loaded — the
    // caller must check this before calling processFrame().
    virtual PitchEngineStatus prepare (double inputSampleRate) = 0;

    // Clears internal buffering/history state without reloading the model.
    virtual void reset() = 0;

    // Processes one block of raw input-rate audio samples. May internally
    // buffer partial windows and return frequencyHz = nullopt if not enough
    // samples have accumulated yet for an inference.
    //
    // Confidence contract: implementations are expected to populate
    // frequencyHz (i.e. return non-nullopt) only when they consider the
    // reading confident/voiced. Callers may therefore treat any non-nullopt
    // frequencyHz as usable on its own, without a separate confidence check.
    // The confidence field remains available on PitchFrame for
    // implementations/callers that want finer-grained information beyond
    // this pass/fail signal.
    virtual PitchFrame processFrame (const float* audioFrame, size_t numSamples) = 0;

    virtual PitchEngineStatus getStatus() const = 0;
};
