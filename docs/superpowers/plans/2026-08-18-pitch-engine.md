# Pitch Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the `PitchEngine` abstract interface, a `CrepePitchEngine` implementation wrapping the CREPE ONNX model via ONNX Runtime's C++ API, and a `PitchContinuityFilter` that rejects octave-jump artifacts — the pitch-detection core that `SwarMapper` (already built) will consume.

**Architecture:** `PitchEngine` is a pure-virtual interface (`prepare`/`reset`/`processFrame`/`getStatus`). `CrepePitchEngine` resamples incoming audio to CREPE's required 16kHz, buffers it into non-overlapping 1024-sample windows, runs ONNX Runtime inference, and decodes the 360-bin sigmoid output into (frequency, confidence) using CREPE's own "weighted local average around the argmax bin" method — no cross-frame Viterbi, so it works one frame at a time in a streaming context. `PitchContinuityFilter` sits between the engine and `SwarMapper`, comparing each new frequency to the last confident one and correcting jumps that land within 50 cents of an exact octave multiple (a genuine artifact) while leaving real melodic leaps (which don't land on octave multiples) untouched.

**Tech Stack:** C++17, JUCE 8.0.7 (`juce_dsp` for resampling), ONNX Runtime 1.23.2 (vendored prebuilt binary — vcpkg's source build fails on this machine, see `TODOS.md`), `juce::UnitTestRunner`.

## Global Constraints

- C++17. Test framework is `juce::UnitTestRunner`.
- ONNX Runtime is vendored at `third_party/onnxruntime-win-x64-1.23.2/` (NOT via vcpkg — `find_package(onnxruntime CONFIG)` will fail, use manual `target_include_directories`/`target_link_libraries` pointing at that directory). `onnxruntime.dll` must be copied next to the test executable post-build or the test binary won't launch.
- CREPE model is vendored at `models/crepe/small.onnx` (6.5MB, "small" capacity — chosen for CPU real-time inference; can be swapped for `large`/`full` later without code changes since the ONNX I/O contract is identical across sizes).
- **Verified ONNX model I/O contract** (inspected directly from the `.onnx` file, not assumed): input tensor name `"frames"`, shape `[n_frames, 1024]`, float32, raw audio samples at 16000 Hz with **no manual normalization** (z-score standardization is baked into the graph). Output tensor name `"probabilities"`, shape `[n_frames, 360]`, float32, **already sigmoid-activated** (values in [0,1] per bin, not raw logits).
- CREPE bin→cents formula: `cents = 20.0 * bin + 1997.3794084376191`. Cents→frequency: `freq = 10.0 * 2^(cents/1200.0)`. These are exact constants from the reference `onnxcrepe` implementation — use them verbatim, do not approximate.
- Decode method: **weighted local average around the argmax bin** (not full Viterbi — Viterbi needs the whole sequence and doesn't fit a streaming one-frame-at-a-time architecture). Given 360 sigmoid outputs: find `argmaxBin`; take the window `[argmaxBin-4, argmaxBin+4]` (clamped to `[0,360)`); weight each bin's ReLU'd probability by its cents value; `cents = sum(weight*prob) / sum(prob)` over the window; confidence = `probabilities[argmaxBin]` directly (already in [0,1], no further transform).
- `PitchEngine` extends the original interface with `prepare(double inputSampleRate) -> PitchEngineStatus`, `reset()`, and `getStatus()` per the eng-review's Code Quality decision — a model load failure must be distinguishable from a silent/unvoiced frame, not conflated.
- Octave-jump correction (`PitchContinuityFilter`) per the eng-review's Code Quality decision: correct only jumps that land within 50 cents of an exact multiple of 1200 cents from the last confident frequency — this is what distinguishes an engine octave-glitch from a real large melodic interval (which won't land near an exact octave multiple).

---

## File Structure

- `third_party/onnxruntime-win-x64-1.23.2/` — vendored prebuilt ONNX Runtime (already downloaded, gitignored)
- `models/crepe/small.onnx` — vendored CREPE weights (already downloaded, gitignored — add `models/` to `.gitignore` in Task 1)
- `src/audio/pitchengine/PitchEngine.h` — `PitchFrame`, `PitchEngineStatus`, `PitchEngine` abstract interface
- `src/audio/pitchengine/CrepeDecode.h` / `.cpp` — pure decode math (no ONNX Runtime dependency, fully unit-testable with synthetic probability arrays)
- `src/audio/pitchengine/CrepeDecodeTests.cpp`
- `src/audio/pitchengine/PitchContinuityFilter.h` / `.cpp` — pure logic (no ONNX Runtime dependency)
- `src/audio/pitchengine/PitchContinuityFilterTests.cpp`
- `src/audio/pitchengine/CrepePitchEngine.h` / `.cpp` — ONNX Runtime integration, resampling, buffering
- `src/audio/pitchengine/CrepePitchEngineTests.cpp`
- `CMakeLists.txt` — extended with the vendored ONNX Runtime paths and new source files

---

### Task 1: PitchEngine interface + CrepeDecode (pure math, no ONNX Runtime dependency)

**Files:**
- Create: `src/audio/pitchengine/PitchEngine.h`
- Create: `src/audio/pitchengine/CrepeDecode.h`
- Create: `src/audio/pitchengine/CrepeDecode.cpp`
- Test: `src/audio/pitchengine/CrepeDecodeTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: nothing
- Produces:
  - `struct PitchFrame { uint64_t timestampMs; std::optional<float> frequencyHz; float confidence; };`
  - `enum class PitchEngineStatus { Ok, NotPrepared, LoadError };`
  - `class PitchEngine { public: virtual ~PitchEngine() = default; virtual PitchEngineStatus prepare (double inputSampleRate) = 0; virtual void reset() = 0; virtual PitchFrame processFrame (const float* audioFrame, size_t numSamples) = 0; virtual PitchEngineStatus getStatus() const = 0; };`
  - `struct CrepeDecodeResult { float frequencyHz; float confidence; };`
  - `CrepeDecodeResult decodeCrepeOutput (const float* probabilities, int numBins);`

- [ ] **Step 1: Add `models/` to `.gitignore` (the CREPE weights are a 6.5MB binary, gitignored like `third_party/`)**

```
# .gitignore — add this line alongside the existing third_party/ entry
models/
```

- [ ] **Step 2: Write `PitchEngine.h`**

```cpp
// src/audio/pitchengine/PitchEngine.h
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
    LoadError
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
    virtual PitchFrame processFrame (const float* audioFrame, size_t numSamples) = 0;

    virtual PitchEngineStatus getStatus() const = 0;
};
```

- [ ] **Step 3: Write the failing test for `decodeCrepeOutput`**

```cpp
// src/audio/pitchengine/CrepeDecodeTests.cpp
#include "CrepeDecode.h"
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

class CrepeDecodeTests : public juce::UnitTest
{
public:
    CrepeDecodeTests() : juce::UnitTest ("CrepeDecode", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("Single confident bin decodes to that bin's exact cents/frequency");
        {
            std::vector<float> probs (360, 0.0f);
            probs[0] = 1.0f; // isolated spike at bin 0, all others exactly 0

            auto result = decodeCrepeOutput (probs.data(), 360);

            // Bin 0's cents value directly from the documented formula.
            const float expectedCents = 20.0f * 0.0f + 1997.3794084376191f;
            const float expectedFreq = 10.0f * std::pow (2.0f, expectedCents / 1200.0f);

            expectWithinAbsoluteError (result.frequencyHz, expectedFreq, 0.01f);
            expectWithinAbsoluteError (result.confidence, 1.0f, 0.0001f);
        }

        beginTest ("Weighted average pulls frequency toward a strong neighboring bin");
        {
            std::vector<float> probs (360, 0.0f);
            probs[200] = 0.6f;
            probs[201] = 0.6f; // equal-strength neighbor should pull the average up

            auto result = decodeCrepeOutput (probs.data(), 360);

            const float cents200 = 20.0f * 200.0f + 1997.3794084376191f;
            const float cents201 = 20.0f * 201.0f + 1997.3794084376191f;
            const float expectedCents = (cents200 * 0.6f + cents201 * 0.6f) / (0.6f + 0.6f);
            const float expectedFreq = 10.0f * std::pow (2.0f, expectedCents / 1200.0f);

            expectWithinAbsoluteError (result.frequencyHz, expectedFreq, 0.01f);
            expectWithinAbsoluteError (result.confidence, 0.6f, 0.0001f);
        }

        beginTest ("Window clamps at array bounds near bin 0 and bin 359");
        {
            std::vector<float> probs (360, 0.0f);
            probs[0] = 1.0f;
            probs[1] = 0.3f; // only 1 neighbor exists on the low side (no bin -1..-4)

            auto result = decodeCrepeOutput (probs.data(), 360);
            // Just confirm it doesn't crash/read out of bounds and produces a
            // frequency between bin 0's and bin 1's pure values.
            const float freq0 = 10.0f * std::pow (2.0f, (1997.3794084376191f) / 1200.0f);
            const float freq1 = 10.0f * std::pow (2.0f, (20.0f + 1997.3794084376191f) / 1200.0f);
            expect (result.frequencyHz >= freq0 && result.frequencyHz <= freq1);
        }
    }
};

static CrepeDecodeTests crepeDecodeTestsInstance;
```

- [ ] **Step 4: Extend `CMakeLists.txt` to build these new sources into `riyaaz_tests`**

```cmake
# Add to the existing riyaaz_tests target_sources call in CMakeLists.txt:
target_sources(riyaaz_tests PRIVATE
    src/audio/swarmap/SwarMapperTests.cpp
    src/audio/pitchengine/CrepeDecode.cpp
    src/audio/pitchengine/CrepeDecodeTests.cpp
    src/TestMain.cpp
)
```

(Keep the existing `SwarMapperTests.cpp` and `TestMain.cpp` entries — this is an addition, not a replacement.)

- [ ] **Step 5: Configure and build, verify the test fails (`CrepeDecode.h` doesn't exist yet)**

Run:
```
cmake -B build -S . -G Ninja -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --target riyaaz_tests
```
Expected: build fails with "CrepeDecode.h: No such file or directory".

- [ ] **Step 6: Write the minimal `CrepeDecode` implementation**

```cpp
// src/audio/pitchengine/CrepeDecode.h
#pragma once

struct CrepeDecodeResult
{
    float frequencyHz;
    float confidence;
};

// Decodes one frame's 360 sigmoid-activated pitch-bin probabilities into a
// (frequency, confidence) pair using CREPE's weighted-local-average method.
CrepeDecodeResult decodeCrepeOutput (const float* probabilities, int numBins);
```

```cpp
// src/audio/pitchengine/CrepeDecode.cpp
#include "CrepeDecode.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kCentsPerBin = 20.0f;
    constexpr float kCentsOffset = 1997.3794084376191f;

    float binToCents (int bin)
    {
        return kCentsPerBin * (float) bin + kCentsOffset;
    }
}

CrepeDecodeResult decodeCrepeOutput (const float* probabilities, int numBins)
{
    int argmaxBin = 0;
    float maxVal = probabilities[0];
    for (int i = 1; i < numBins; ++i)
    {
        if (probabilities[i] > maxVal)
        {
            maxVal = probabilities[i];
            argmaxBin = i;
        }
    }

    const int start = std::max (0, argmaxBin - 4);
    const int end = std::min (numBins, argmaxBin + 5); // exclusive

    float weightedCentsSum = 0.0f;
    float probSum = 0.0f;
    for (int i = start; i < end; ++i)
    {
        const float p = std::max (0.0f, probabilities[i]); // ReLU
        weightedCentsSum += binToCents (i) * p;
        probSum += p;
    }

    const float cents = probSum > 0.0f ? weightedCentsSum / probSum : binToCents (argmaxBin);
    const float frequencyHz = 10.0f * std::pow (2.0f, cents / 1200.0f);

    return { frequencyHz, probabilities[argmaxBin] };
}
```

- [ ] **Step 7: Build and run, verify all tests pass**

Run:
```
cmake --build build --target riyaaz_tests
./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe
```
Expected: exit code 0, all 3 new `CrepeDecode` tests passing alongside the 8 existing `SwarMapper` tests (11 total).

- [ ] **Step 8: Commit**

```bash
git add .gitignore CMakeLists.txt src/audio/pitchengine/PitchEngine.h src/audio/pitchengine/CrepeDecode.h src/audio/pitchengine/CrepeDecode.cpp src/audio/pitchengine/CrepeDecodeTests.cpp
git commit -m "feat: add PitchEngine interface and CrepeDecode weighted-average decoding"
```

---

### Task 2: PitchContinuityFilter (pure logic, no ONNX Runtime dependency)

**Files:**
- Create: `src/audio/pitchengine/PitchContinuityFilter.h`
- Create: `src/audio/pitchengine/PitchContinuityFilter.cpp`
- Test: `src/audio/pitchengine/PitchContinuityFilterTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `PitchFrame` from Task 1 (`PitchEngine.h`)
- Produces:
  - `class PitchContinuityFilter { public: PitchFrame process (PitchFrame frame); void reset(); };`

- [ ] **Step 1: Write the failing tests**

```cpp
// src/audio/pitchengine/PitchContinuityFilterTests.cpp
#include "PitchContinuityFilter.h"
#include <juce_core/juce_core.h>

class PitchContinuityFilterTests : public juce::UnitTest
{
public:
    PitchContinuityFilterTests() : juce::UnitTest ("PitchContinuityFilter", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("First confident frame passes through unchanged and becomes the reference");
        {
            PitchContinuityFilter filter;
            PitchFrame frame { 0, 220.0f, 0.9f };
            auto result = filter.process (frame);
            expectWithinAbsoluteError (*result.frequencyHz, 220.0f, 0.01f);
        }

        beginTest ("A normal melodic interval (not near an octave multiple) passes through unchanged");
        {
            PitchContinuityFilter filter;
            filter.process (PitchFrame { 0, 220.0f, 0.9f }); // Sa
            auto result = filter.process (PitchFrame { 10, 329.63f, 0.9f }); // Pa, a fifth up (700 cents) - not near an octave
            expectWithinAbsoluteError (*result.frequencyHz, 329.63f, 0.5f);
        }

        beginTest ("A frame landing within 50 cents of exactly double the reference is corrected down an octave");
        {
            PitchContinuityFilter filter;
            filter.process (PitchFrame { 0, 220.0f, 0.9f });
            auto result = filter.process (PitchFrame { 10, 440.0f, 0.9f }); // exactly 1200 cents up - classic octave-error
            expectWithinAbsoluteError (*result.frequencyHz, 220.0f, 1.0f);
        }

        beginTest ("A frame landing within 50 cents of exactly half the reference is corrected up an octave");
        {
            PitchContinuityFilter filter;
            filter.process (PitchFrame { 0, 220.0f, 0.9f });
            auto result = filter.process (PitchFrame { 10, 110.0f, 0.9f }); // exactly -1200 cents
            expectWithinAbsoluteError (*result.frequencyHz, 220.0f, 1.0f);
        }

        beginTest ("Unvoiced frames pass through unchanged and do not reset the reference");
        {
            PitchContinuityFilter filter;
            filter.process (PitchFrame { 0, 220.0f, 0.9f });
            PitchFrame unvoiced { 10, std::nullopt, 0.0f };
            auto result = filter.process (unvoiced);
            expect (! result.frequencyHz.has_value());

            // Reference should still be 220Hz from before the unvoiced gap -
            // an octave-jump frame right after should still get corrected.
            auto corrected = filter.process (PitchFrame { 20, 440.0f, 0.9f });
            expectWithinAbsoluteError (*corrected.frequencyHz, 220.0f, 1.0f);
        }

        beginTest ("reset() clears the reference so the next frame passes through unchanged");
        {
            PitchContinuityFilter filter;
            filter.process (PitchFrame { 0, 220.0f, 0.9f });
            filter.reset();
            auto result = filter.process (PitchFrame { 10, 440.0f, 0.9f }); // would have been corrected pre-reset
            expectWithinAbsoluteError (*result.frequencyHz, 440.0f, 0.01f);
        }
    }
};

static PitchContinuityFilterTests pitchContinuityFilterTestsInstance;
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target riyaaz_tests` — expect a build failure (`PitchContinuityFilter.h` doesn't exist).

- [ ] **Step 3: Write the implementation**

```cpp
// src/audio/pitchengine/PitchContinuityFilter.h
#pragma once
#include "PitchEngine.h"
#include <optional>

class PitchContinuityFilter
{
public:
    PitchFrame process (PitchFrame frame);
    void reset();

private:
    std::optional<float> lastConfidentFrequencyHz;
};
```

```cpp
// src/audio/pitchengine/PitchContinuityFilter.cpp
#include "PitchContinuityFilter.h"
#include <cmath>

namespace
{
    constexpr float kOctaveToleranceCents = 50.0f;

    float centsBetween (float a, float b)
    {
        return 1200.0f * std::log2 (a / b);
    }
}

PitchFrame PitchContinuityFilter::process (PitchFrame frame)
{
    if (! frame.frequencyHz.has_value())
        return frame;

    if (! lastConfidentFrequencyHz.has_value())
    {
        lastConfidentFrequencyHz = *frame.frequencyHz;
        return frame;
    }

    const float cents = centsBetween (*frame.frequencyHz, *lastConfidentFrequencyHz);
    const float nearestOctaveMultiple = std::round (cents / 1200.0f);

    if (nearestOctaveMultiple != 0.0f)
    {
        const float distanceFromMultiple = std::abs (cents - nearestOctaveMultiple * 1200.0f);
        if (distanceFromMultiple < kOctaveToleranceCents)
        {
            const float corrected = *frame.frequencyHz / std::pow (2.0f, nearestOctaveMultiple);
            frame.frequencyHz = corrected;
            lastConfidentFrequencyHz = corrected;
            return frame;
        }
    }

    lastConfidentFrequencyHz = *frame.frequencyHz;
    return frame;
}

void PitchContinuityFilter::reset()
{
    lastConfidentFrequencyHz.reset();
}
```

- [ ] **Step 4: Add to CMakeLists.txt**

```cmake
target_sources(riyaaz_tests PRIVATE
    src/audio/swarmap/SwarMapperTests.cpp
    src/audio/pitchengine/CrepeDecode.cpp
    src/audio/pitchengine/CrepeDecodeTests.cpp
    src/audio/pitchengine/PitchContinuityFilter.cpp
    src/audio/pitchengine/PitchContinuityFilterTests.cpp
    src/TestMain.cpp
)
```

- [ ] **Step 5: Build and run, verify all tests pass**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: exit code 0, 17 tests total (11 from Task 1 + 6 new).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/audio/pitchengine/PitchContinuityFilter.h src/audio/pitchengine/PitchContinuityFilter.cpp src/audio/pitchengine/PitchContinuityFilterTests.cpp
git commit -m "feat: add PitchContinuityFilter for octave-jump correction"
```

---

### Task 3: Vendored ONNX Runtime CMake integration + CrepePitchEngine skeleton (model loading, prepare/reset, load-error signaling)

**Files:**
- Modify: `CMakeLists.txt`
- Create: `src/audio/pitchengine/CrepePitchEngine.h`
- Create: `src/audio/pitchengine/CrepePitchEngine.cpp`
- Test: `src/audio/pitchengine/CrepePitchEngineTests.cpp`

**Interfaces:**
- Consumes: `PitchEngine`, `PitchFrame`, `PitchEngineStatus` from Task 1
- Produces: `class CrepePitchEngine : public PitchEngine { public: explicit CrepePitchEngine (juce::String modelPath); PitchEngineStatus prepare (double inputSampleRate) override; void reset() override; PitchFrame processFrame (const float* audioFrame, size_t numSamples) override; PitchEngineStatus getStatus() const override; };` — `processFrame`'s real inference body lands in Task 4; this task only needs `prepare()` to load the model and report status correctly.

- [ ] **Step 1: Wire the vendored ONNX Runtime into CMake**

```cmake
# Add near the top of CMakeLists.txt, after find_package(JUCE CONFIG REQUIRED):
set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-win-x64-1.23.2")

# Add to the riyaaz_tests target (after target_link_libraries(riyaaz_tests ...)):
target_include_directories(riyaaz_tests PRIVATE "${ONNXRUNTIME_ROOT}/include")
target_link_libraries(riyaaz_tests PRIVATE "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")

# Copy the runtime DLL next to the built test executable so it can actually launch:
add_custom_command(TARGET riyaaz_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
        "$<TARGET_FILE_DIR:riyaaz_tests>"
)
```

- [ ] **Step 2: Write the failing test — model loads successfully, and a missing model reports LoadError distinctly**

```cpp
// src/audio/pitchengine/CrepePitchEngineTests.cpp
#include "CrepePitchEngine.h"
#include <juce_core/juce_core.h>

class CrepePitchEngineTests : public juce::UnitTest
{
public:
    CrepePitchEngineTests() : juce::UnitTest ("CrepePitchEngine", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("prepare() with a valid model path returns Ok and getStatus() agrees");
        {
            CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
            auto status = engine.prepare (44100.0);
            expect (status == PitchEngineStatus::Ok);
            expect (engine.getStatus() == PitchEngineStatus::Ok);
        }

        beginTest ("prepare() with a missing model path returns LoadError, not a crash");
        {
            CrepePitchEngine engine (juce::String ("models/crepe/does_not_exist.onnx"));
            auto status = engine.prepare (44100.0);
            expect (status == PitchEngineStatus::LoadError);
            expect (engine.getStatus() == PitchEngineStatus::LoadError);
        }

        beginTest ("processFrame() before prepare() returns an unvoiced frame, not a crash");
        {
            CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
            // prepare() deliberately NOT called
            std::vector<float> silence (1024, 0.0f);
            auto frame = engine.processFrame (silence.data(), silence.size());
            expect (! frame.frequencyHz.has_value());
        }
    }
};

static CrepePitchEngineTests crepePitchEngineTestsInstance;
```

Note: the test runs with working directory at the build/run location — if `models/crepe/small.onnx` (relative path) isn't found, adjust the test to use an absolute path via `juce::File::getCurrentWorkingDirectory()` diagnostics; report this in your task report rather than silently hardcoding a machine-specific absolute path.

- [ ] **Step 3: Run to verify failure**

Run: `cmake --build build --target riyaaz_tests` — expect a build failure (`CrepePitchEngine.h` doesn't exist).

- [ ] **Step 4: Write the implementation (model loading + prepare/reset/status only — processFrame returns an empty/unvoiced frame for now, real inference is Task 4)**

```cpp
// src/audio/pitchengine/CrepePitchEngine.h
#pragma once
#include "PitchEngine.h"
#include <juce_core/juce_core.h>
#include <onnxruntime_cxx_api.h>
#include <memory>
#include <vector>

class CrepePitchEngine : public PitchEngine
{
public:
    explicit CrepePitchEngine (juce::String modelPath);

    PitchEngineStatus prepare (double inputSampleRate) override;
    void reset() override;
    PitchFrame processFrame (const float* audioFrame, size_t numSamples) override;
    PitchEngineStatus getStatus() const override;

private:
    juce::String modelPath;
    PitchEngineStatus status = PitchEngineStatus::NotPrepared;
    double sampleRate = 0.0;
    uint64_t samplesProcessed = 0;

    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> session;
};
```

```cpp
// src/audio/pitchengine/CrepePitchEngine.cpp
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
```

- [ ] **Step 5: Add to CMakeLists.txt**

```cmake
target_sources(riyaaz_tests PRIVATE
    src/audio/swarmap/SwarMapperTests.cpp
    src/audio/pitchengine/CrepeDecode.cpp
    src/audio/pitchengine/CrepeDecodeTests.cpp
    src/audio/pitchengine/PitchContinuityFilter.cpp
    src/audio/pitchengine/PitchContinuityFilterTests.cpp
    src/audio/pitchengine/CrepePitchEngine.cpp
    src/audio/pitchengine/CrepePitchEngineTests.cpp
    src/TestMain.cpp
)
```

- [ ] **Step 6: Build and run, verify all tests pass**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: exit code 0, 20 tests total (17 from Tasks 1-2 + 3 new). Confirm the `onnxruntime.dll` copy step worked — if the test exe fails to launch with a missing-DLL error, that's the post-build copy command, fix it before proceeding.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/audio/pitchengine/CrepePitchEngine.h src/audio/pitchengine/CrepePitchEngine.cpp src/audio/pitchengine/CrepePitchEngineTests.cpp
git commit -m "feat: add CrepePitchEngine model loading with distinct load-error status"
```

---

### Task 4: CrepePitchEngine real inference — resampling, windowing, ONNX inference, decode, continuity filtering

**Files:**
- Modify: `src/audio/pitchengine/CrepePitchEngine.h`
- Modify: `src/audio/pitchengine/CrepePitchEngine.cpp`
- Modify: `src/audio/pitchengine/CrepePitchEngineTests.cpp`
- Modify: `CMakeLists.txt` (add `juce_dsp` for resampling if not already linked)

**Interfaces:**
- Consumes: `decodeCrepeOutput` (Task 1), `PitchContinuityFilter` (Task 2), `Ort::Session` (Task 3, already built in `prepare()`)
- Produces: a working `processFrame()` — no new public symbols, `PitchEngine`'s interface is unchanged

- [ ] **Step 1: Write the failing test — a synthetic sine wave is detected at approximately its true frequency**

```cpp
// Append to src/audio/pitchengine/CrepePitchEngineTests.cpp, inside runTest():

beginTest ("A synthetic 220Hz sine wave is detected at approximately 220Hz");
{
    CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
    auto prepareStatus = engine.prepare (16000.0); // feed at CREPE's native rate to avoid resampler-accuracy as a variable in this test
    expect (prepareStatus == PitchEngineStatus::Ok);

    // Generate 1 second of 220Hz sine at 16kHz, fed in 1024-sample chunks
    // (matching CREPE's window size) so the engine has enough for several
    // inferences.
    constexpr double sr = 16000.0;
    constexpr float freq = 220.0f;
    std::vector<float> sine (16000);
    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] = std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / sr) * 0.5f;

    std::optional<float> lastDetected;
    for (size_t offset = 0; offset + 1024 <= sine.size(); offset += 1024)
    {
        auto frame = engine.processFrame (sine.data() + offset, 1024);
        if (frame.frequencyHz.has_value() && frame.confidence > 0.5f)
            lastDetected = frame.frequencyHz;
    }

    expect (lastDetected.has_value());
    if (lastDetected.has_value())
        expectWithinAbsoluteError (*lastDetected, 220.0f, 5.0f); // CREPE bin quantization is ~20 cents (~2.5Hz at 220Hz), 5Hz tolerance is safe
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: FAIL — `processFrame()` currently always returns unvoiced (Task 3's stub).

- [ ] **Step 3: Implement resampling + windowing + inference + decode + continuity filtering**

```cpp
// src/audio/pitchengine/CrepePitchEngine.h — extend the private section:
#pragma once
#include "PitchEngine.h"
#include "PitchContinuityFilter.h"
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <onnxruntime_cxx_api.h>
#include <memory>
#include <vector>

class CrepePitchEngine : public PitchEngine
{
public:
    explicit CrepePitchEngine (juce::String modelPath);

    PitchEngineStatus prepare (double inputSampleRate) override;
    void reset() override;
    PitchFrame processFrame (const float* audioFrame, size_t numSamples) override;
    PitchEngineStatus getStatus() const override;

private:
    static constexpr double kCrepeSampleRate = 16000.0;
    static constexpr int kCrepeWindowSize = 1024;
    static constexpr float kMinConfidence = 0.5f;

    juce::String modelPath;
    PitchEngineStatus status = PitchEngineStatus::NotPrepared;
    double sampleRate = 0.0;
    uint64_t samplesProcessed = 0;

    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> session;

    juce::LagrangeInterpolator resampler;
    double resampleSpeedRatio = 1.0; // input samples per output sample (sampleRate / kCrepeSampleRate)
    std::vector<float> resampledBuffer; // accumulates resampled samples until a full window is available
    std::vector<float> resampleScratch;

    PitchContinuityFilter continuityFilter;

    PitchFrame runInference (const float* window);
};
```

```cpp
// src/audio/pitchengine/CrepePitchEngine.cpp — extend prepare(), reset(), processFrame():
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

    return continuityFilter.process (frame);
}

PitchFrame CrepePitchEngine::processFrame (const float* audioFrame, size_t numSamples)
{
    const uint64_t frameTimestamp = (uint64_t) ((double) samplesProcessed / sampleRate * 1000.0);
    samplesProcessed += numSamples;

    if (status != PitchEngineStatus::Ok)
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
```

The resampler call above was verified against the actual installed header
(`vcpkg_installed/x64-windows/include/JUCE-8.0.7/modules/juce_audio_basics/utilities/juce_GenericInterpolator.h`,
which `LagrangeInterpolator` derives from): `process()` is output-driven —
`speedRatio` is input-samples-per-output-sample, and the 6-argument overload
takes an explicit `numInputSamplesAvailable` so it zero-pads rather than
reading out of bounds. Use the code as written; no further verification of
this specific call is needed.

- [ ] **Step 4: Add `juce_dsp` to CMakeLists.txt if not already linked**

```cmake
# In the riyaaz_swarmap / riyaaz_tests target_link_libraries calls, ensure
# juce::juce_dsp is present alongside juce::juce_core:
target_link_libraries(riyaaz_tests PRIVATE riyaaz_swarmap juce::juce_core juce::juce_dsp)
```

- [ ] **Step 5: Build and run, verify all tests pass**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: exit code 0, 21 tests total (20 from Tasks 1-3 + 1 new). If the sine-wave test fails on frequency accuracy, check: (a) the resampler ratio direction, (b) whether `kMinConfidence` is too strict for a clean synthetic tone (it shouldn't be — a pure sine should produce high confidence), (c) windowing alignment. Report the actual detected frequency in your task report even if the test fails, so the reviewer can see how far off it was.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/audio/pitchengine/CrepePitchEngine.h src/audio/pitchengine/CrepePitchEngine.cpp src/audio/pitchengine/CrepePitchEngineTests.cpp
git commit -m "feat: implement CrepePitchEngine inference pipeline (resample, window, infer, decode, continuity-filter)"
```

---

## Not covered by this plan (separate plans)

- Tonic calibration (`TonicCalibrator`) — now unblocked by this plan (consumes `PitchEngine`), was previously blocked on it.
- Real-time audio pipeline: `juce::AbstractFifo`, worker thread, `AsyncUpdater`, live JUCE GUI window showing the pitch graph — this plan's `CrepePitchEngine` is synchronous/blocking (fine for unit tests and offline processing), NOT yet wired to run off the audio thread.
- Tanpura (pre-recorded per-semitone samples), metronome/taal engine, packaging — unchanged from the milestone order.
