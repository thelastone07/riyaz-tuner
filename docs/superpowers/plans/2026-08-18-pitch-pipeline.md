# Pitch Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the real-time-readiness gaps the PitchEngine plan's final review flagged for CrepePitchEngine (unbounded buffer growth at large audio blocks, non-atomic status, timestamp lag), extract a clean CMake library structure so an app target can reuse everything without duplication, and build `PitchPipeline` — a single orchestrator that owns the Calibrating→Live state machine (TonicCalibrator → SwarMapper) so a future worker thread only has to call one method per audio block instead of juggling three components' state by hand.

**Architecture:** `CrepePitchEngine::processFrame()` currently consumes at most one 1024-sample window per call, so a call carrying more resampled audio than that (a normal 4096-sample JUCE buffer, for instance) leaves the surplus in `resampledBuffer` forever — it never drains. This plan makes it loop internally until the buffer is below one window, keeping only the most recent inference result (correct for a live display: an old, superseded pitch reading is not useful once a newer one exists) and deriving timestamps from a running count of *resampled* samples consumed, not wall-clock call time — so timestamps advance in fixed 64ms steps regardless of how many windows one call happens to drain. `PitchPipeline` then sits on top of `PitchEngine` + `TonicCalibrator` + `SwarMapper`, dispatching each incoming audio block to calibration until Sa is found, then to live tracking — and resolves a question left open since `SwarMapper` first shipped: unvoiced frames are never fed into `SwarMapper::update()` at all, so its hysteresis lock survives a brief pause in singing untouched.

**Tech Stack:** C++17, JUCE 8.0.7, ONNX Runtime 1.23.2 (vendored), `juce::UnitTestRunner`.

## Global Constraints

- C++17. Test framework is `juce::UnitTestRunner`.
- No behavior change to `SwarMapper`, `TonicCalibrator`, `CrepeDecode`, or `PitchContinuityFilter` in this plan beyond what's explicitly specified — this plan hardens `CrepePitchEngine` and adds a new orchestrator on top of the existing, already-reviewed pieces.
- `CrepePitchEngine::processFrame()` must return the LATEST inference result when a single call's audio spans multiple 1024-sample windows, not the first or an average — a live display only cares about the current pitch, and returning a stale first-of-several result would introduce exactly the lag the timestamp fix is meant to remove.
- Window timestamps must be derived from a running count of *resampled* (16kHz-domain) samples consumed by inference, not from wall-clock call timing or raw input sample count — this makes timestamps advance in fixed, predictable steps (`1024 / 16000 * 1000 = 64ms` per window) independent of caller block size or resample ratio.
- `PitchPipeline` must take `PitchEngine&` by reference (dependency injection, matching `TonicCalibrator`'s existing pattern) — never own or construct an engine.
- `PitchPipeline` must never call `SwarMapper::update()` for an unvoiced frame (`frequencyHz == nullopt`) — this is the resolved answer to the SwarMapper NaN/silence contract question noted in `TODOS.md`.
- Must not regress the 36 existing tests.

---

## File Structure

- `CMakeLists.txt` — restructured: new `riyaaz_pitchengine` and `riyaaz_calibration` static library targets, `riyaaz_tests` slimmed to test-only sources linking against them, a reusable DLL-copy CMake function
- `src/audio/pitchengine/CrepePitchEngine.h` / `.cpp` — modified (multi-window draining, timestamp derivation, atomic status)
- `src/audio/pitchengine/CrepePitchEngineTests.cpp` — modified (new tests for the above)
- `src/audio/pipeline/PitchPipeline.h` / `.cpp` — new orchestrator
- `src/audio/pipeline/PitchPipelineTests.cpp` — new

---

### Task 1: CMake library extraction

**Files:**
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing new
- Produces: `riyaaz_pitchengine` (STATIC, PUBLIC-propagates ONNX Runtime include/link so any consumer gets it automatically), `riyaaz_calibration` (STATIC, no ONNX dependency), a `riyaaz_copy_onnxruntime_dll(<target>)` CMake function

- [ ] **Step 1: Rewrite `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.24)
project(riyaaz VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(JUCE CONFIG REQUIRED)

set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-win-x64-1.23.2")

# Copies onnxruntime.dll next to a target's built executable. Call once per
# executable target that transitively links riyaaz_pitchengine (it will not
# launch without this - onnxruntime.dll must be discoverable at runtime).
function(riyaaz_copy_onnxruntime_dll target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
            "$<TARGET_FILE_DIR:${target}>"
    )
endfunction()

add_library(riyaaz_swarmap STATIC
    src/audio/swarmap/SwarMapper.cpp
)
target_include_directories(riyaaz_swarmap PUBLIC src)
target_link_libraries(riyaaz_swarmap PUBLIC juce::juce_core)
target_compile_definitions(riyaaz_swarmap PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)

add_library(riyaaz_pitchengine STATIC
    src/audio/pitchengine/CrepeDecode.cpp
    src/audio/pitchengine/PitchContinuityFilter.cpp
    src/audio/pitchengine/CrepePitchEngine.cpp
)
target_include_directories(riyaaz_pitchengine PUBLIC src)
target_include_directories(riyaaz_pitchengine PUBLIC "${ONNXRUNTIME_ROOT}/include")
target_link_libraries(riyaaz_pitchengine PUBLIC juce::juce_core juce::juce_dsp)
target_link_libraries(riyaaz_pitchengine PUBLIC "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
target_compile_definitions(riyaaz_pitchengine PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)

add_library(riyaaz_calibration STATIC
    src/audio/calibration/TonicCalibrator.cpp
)
target_include_directories(riyaaz_calibration PUBLIC src)
target_link_libraries(riyaaz_calibration PUBLIC juce::juce_core)
target_compile_definitions(riyaaz_calibration PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)

juce_add_console_app(riyaaz_tests PRODUCT_NAME "Riyaaz Tests")
target_sources(riyaaz_tests PRIVATE
    src/audio/swarmap/SwarMapperTests.cpp
    src/audio/pitchengine/CrepeDecodeTests.cpp
    src/audio/pitchengine/PitchContinuityFilterTests.cpp
    src/audio/pitchengine/CrepePitchEngineTests.cpp
    src/audio/calibration/TonicCalibratorTests.cpp
    src/TestMain.cpp
)
target_link_libraries(riyaaz_tests PRIVATE
    riyaaz_swarmap
    riyaaz_pitchengine
    riyaaz_calibration
    juce::juce_core
)
riyaaz_copy_onnxruntime_dll(riyaaz_tests)
```

Note what changed from the previous file: `riyaaz_pitchengine` and `riyaaz_calibration` are new; `riyaaz_tests` no longer lists `CrepeDecode.cpp`/`PitchContinuityFilter.cpp`/`CrepePitchEngine.cpp`/`TonicCalibrator.cpp` directly (those now live in the new libraries) and no longer has its own `target_include_directories`/`target_link_libraries` calls for ONNX Runtime or its own `add_custom_command` DLL copy (both now come from `riyaaz_pitchengine PUBLIC` and the shared `riyaaz_copy_onnxruntime_dll` function respectively). `PitchPipeline` (Task 4) is not yet added here — its own task adds `riyaaz_pipeline` and wires it into `riyaaz_tests`.

- [ ] **Step 2: Configure and build clean, verify the test executable still launches and all tests pass**

Run:
```
cmake -B build -S . -G Ninja -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --target riyaaz_tests
./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe
```
Expected: exit code 0, all 36 existing tests still passing. This is a pure build-structure refactor — no test file content changes, no production logic changes. If any test fails, the refactor introduced a regression; do not proceed to Task 2 until this is clean.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "refactor: extract riyaaz_pitchengine and riyaaz_calibration static libraries"
```

---

### Task 2: CrepePitchEngine — multi-window draining and window-relative timestamps

**Files:**
- Modify: `src/audio/pitchengine/CrepePitchEngine.h`
- Modify: `src/audio/pitchengine/CrepePitchEngine.cpp`
- Test: `src/audio/pitchengine/CrepePitchEngineTests.cpp`

**Interfaces:**
- Consumes: nothing new
- Produces: no public signature change — `processFrame()`'s behavior changes (drains all complete windows in one call, returns the latest result), and `PitchFrame::timestampMs`'s meaning changes (window-relative, not call-relative)

- [ ] **Step 1: Write the failing tests**

```cpp
// Append to src/audio/pitchengine/CrepePitchEngineTests.cpp, inside runTest():

beginTest ("A single large block spanning multiple windows drains all of them and returns only the latest result");
{
    CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
    auto prepareStatus = engine.prepare (16000.0);
    expect (prepareStatus == PitchEngineStatus::Ok);

    // 4096 samples = exactly 4 windows at 16kHz native rate (no resampling).
    // First 3072 samples (3 windows) are silence; last 1024 (the 4th window)
    // is a clean 220Hz tone. The OLD one-window-per-call code would only
    // consume the first 1024 samples on this call (silence -> unvoiced) and
    // leave the rest buffered for a future call it might never receive. The
    // fixed code must drain all 4 windows in this one call and return the
    // 4th window's result.
    constexpr double sr = 16000.0;
    constexpr float freq = 220.0f;
    std::vector<float> block (4096, 0.0f);
    for (size_t i = 3072; i < block.size(); ++i)
        block[i] = std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) (i - 3072) / sr) * 0.5f;

    auto frame = engine.processFrame (block.data(), block.size());

    expect (frame.frequencyHz.has_value());
    if (frame.frequencyHz.has_value())
        expectWithinAbsoluteError (*frame.frequencyHz, 220.0f, 5.0f);
}

beginTest ("Timestamps advance in fixed 64ms increments per window, independent of call granularity");
{
    CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
    engine.prepare (16000.0);

    std::vector<float> silence (1024, 0.0f);
    auto first = engine.processFrame (silence.data(), silence.size());
    auto second = engine.processFrame (silence.data(), silence.size());

    expectEquals ((long long) first.timestampMs, (long long) 0);
    expectEquals ((long long) second.timestampMs, (long long) 64); // 1024/16000*1000
}
```

- [ ] **Step 2: Run to verify these fail**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: FAIL — the first test fails because the current code only consumes 1024 of the 4096 samples per call and the remaining silence-then-tone stays buffered, so this call returns unvoiced. The second test likely already passes by coincidence at this exact configuration (1024-sample calls at 16kHz native rate happen to make the old `samplesProcessed`-based timestamp agree with the new scheme) — if so, that's fine, it's still worth locking down explicitly since the *reason* it's correct is about to change.

- [ ] **Step 3: Rewrite the affected parts of `CrepePitchEngine.h` and `.cpp`**

In `CrepePitchEngine.h`, replace the `samplesProcessed` member:

```cpp
// Replace this line:
    uint64_t samplesProcessed = 0;
// With:
    uint64_t resampledSamplesConsumed = 0; // running count of 16kHz-domain samples consumed by inference so far
```

In `CrepePitchEngine.cpp`:

Replace every occurrence of `samplesProcessed = 0;` (in `prepare()` and `reset()`) with `resampledSamplesConsumed = 0;`.

Replace the start of `runInference()`:

```cpp
PitchFrame CrepePitchEngine::runInference (const float* window)
{
    // Timestamp reflects the START of the window being analyzed, in terms of
    // resampled (16kHz-domain) audio time - not wall-clock call time. This
    // advances in fixed kCrepeWindowSize/kCrepeSampleRate steps regardless of
    // caller block size or how many windows one processFrame() call drains.
    const uint64_t frameTimestamp = (uint64_t) ((double) resampledSamplesConsumed / kCrepeSampleRate * 1000.0);
    resampledSamplesConsumed += (uint64_t) kCrepeWindowSize;

    // The try block wraps only the ONNX-specific work: ...
```

(keep the rest of `runInference()`'s body unchanged below this point — the try/catch, decode, continuityFilter.process call, etc. — only the timestamp computation at the top changes)

Replace `processFrame()` entirely:

```cpp
PitchFrame CrepePitchEngine::processFrame (const float* audioFrame, size_t numSamples)
{
    const uint64_t fallbackTimestamp = (uint64_t) ((double) resampledSamplesConsumed / kCrepeSampleRate * 1000.0);

    if (status == PitchEngineStatus::NotPrepared || status == PitchEngineStatus::LoadError)
        return PitchFrame { fallbackTimestamp, std::nullopt, 0.0f };

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
```

- [ ] **Step 4: Build and run, verify all tests pass**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: exit code 0. Test count should be 38 (36 existing + 2 new). If the multi-window test's detected frequency is off by more than the 5Hz tolerance, check whether `resampledBuffer.data()` correctly points at the CURRENT front of the buffer on each loop iteration after `erase()` shifts it (it should — `erase` on a `std::vector` moves subsequent elements down, so `.data()` always points at the new front) rather than a stale pointer captured once outside the loop.

- [ ] **Step 5: Commit**

```bash
git add src/audio/pitchengine/CrepePitchEngine.h src/audio/pitchengine/CrepePitchEngine.cpp src/audio/pitchengine/CrepePitchEngineTests.cpp
git commit -m "fix: drain all complete windows per processFrame() call, derive timestamps from resampled sample count"
```

---

### Task 3: CrepePitchEngine — atomic status

**Files:**
- Modify: `src/audio/pitchengine/CrepePitchEngine.h`
- Modify: `src/audio/pitchengine/CrepePitchEngine.cpp`

**Interfaces:**
- Consumes: nothing new
- Produces: no public signature change — `getStatus()` still returns `PitchEngineStatus` by value

- [ ] **Step 1: Change the `status` member to atomic**

In `CrepePitchEngine.h`, add `#include <atomic>` near the top, and replace:

```cpp
    PitchEngineStatus status = PitchEngineStatus::NotPrepared;
```

with:

```cpp
    // Written from whichever thread calls processFrame()/prepare(), read from
    // getStatus() which is intended to be polled from a UI thread once this
    // engine runs behind a worker thread - must be atomic to avoid a data
    // race the moment that wiring exists.
    std::atomic<PitchEngineStatus> status { PitchEngineStatus::NotPrepared };
```

- [ ] **Step 2: Update `getStatus()` in `CrepePitchEngine.cpp`**

```cpp
PitchEngineStatus CrepePitchEngine::getStatus() const
{
    return status.load();
}
```

Every other site that reads or writes `status` (comparisons like `status == PitchEngineStatus::NotPrepared`, assignments like `status = PitchEngineStatus::LoadError;`) needs no changes — `std::atomic<T>` supports `operator=` and implicit conversion to `T` for comparisons directly.

- [ ] **Step 3: Build and run, verify all tests pass with no behavior change**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: exit code 0, same 38 tests as Task 2 (this task adds no new test — atomicity itself isn't observable from a single-threaded test; the existing suite passing unchanged is the regression proof that the atomic wrapper didn't alter behavior).

- [ ] **Step 4: Commit**

```bash
git add src/audio/pitchengine/CrepePitchEngine.h src/audio/pitchengine/CrepePitchEngine.cpp
git commit -m "fix: make CrepePitchEngine::status atomic ahead of worker-thread wiring"
```

---

### Task 4: PitchPipeline orchestrator

**Files:**
- Create: `src/audio/pipeline/PitchPipeline.h`
- Create: `src/audio/pipeline/PitchPipeline.cpp`
- Test: `src/audio/pipeline/PitchPipelineTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `PitchEngine`, `PitchFrame`, `CrepePitchEngine` (pitchengine — the last only by the test file's end-to-end test, not by production code), `TonicCalibrator`, `CalibrationResult`, `CalibrationStatus` (calibration), `SwarMapper`, `SwarLabel`, `Swar`, `OctaveRegister` (swarmap) — all already built, unchanged
- Produces:
  ```cpp
  enum class PitchPipelinePhase { Calibrating, Live };

  struct PitchPipelineUpdate
  {
      PitchPipelinePhase phase;
      CalibrationStatus calibrationStatus = CalibrationStatus::InProgress; // valid when phase == Calibrating
      std::optional<SwarLabel> swarLabel;   // valid when phase == Live and a voiced frame was seen
      std::optional<float> centsFromSa;     // valid alongside swarLabel
      float saHz = 0.0f;                    // valid once phase == Live
  };

  class PitchPipeline
  {
  public:
      // calibrationWindowMsIn is passed straight through to TonicCalibrator's
      // own windowMs constructor argument (default 3000, matching
      // TonicCalibrator's own default) - exposed here so tests (and,
      // eventually, a "quick recalibrate" UI mode) can use a shorter window
      // without needing a separate code path.
      PitchPipeline (PitchEngine& engineIn, double sampleRateIn, uint64_t calibrationWindowMsIn = 3000);
      PitchPipelineUpdate process (const float* audioFrame, size_t numSamples);
      void restartCalibration();
  };
  ```

- [ ] **Step 1: Write the failing tests**

```cpp
// src/audio/pipeline/PitchPipelineTests.cpp
#include "PitchPipeline.h"
#include "../calibration/FakePitchEngine.h"
#include <juce_core/juce_core.h>
#include <vector>

class PitchPipelineTests : public juce::UnitTest
{
public:
    PitchPipelineTests() : juce::UnitTest ("PitchPipeline", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("Starts in Calibrating phase and stays there until enough confident readings arrive");
        {
            // Exactly 10 explicit script entries for exactly 10 calls (9
            // confident + 1 unvoiced) - NOT 9 confident entries relying on
            // FakePitchEngine's repeat-last-frame behavior to pad out to 10
            // calls, since the repeated 10th call would also be confident
            // (the last entry, 220Hz) and yield 10 confident readings, not
            // 9 - silently turning this into a Success test instead of a
            // Timeout test. Scripting all 10 calls explicitly avoids that.
            std::vector<PitchFrame> script;
            for (int i = 0; i < 9; ++i)
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            PitchPipeline pipeline (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f); // 300ms, matches TonicCalibrator's own test convention

            PitchPipelineUpdate update { PitchPipelinePhase::Calibrating };
            for (int i = 0; i < 10; ++i) // 10 calls * 300ms = 3000ms = default calibration window
                update = pipeline.process (block.data(), block.size());

            expect (update.phase == PitchPipelinePhase::Calibrating);
            expect (update.calibrationStatus == CalibrationStatus::Timeout); // 9 confident < 10 minConfidentReadings
        }

        beginTest ("Transitions to Live phase once calibration succeeds, and live frames map to swar labels");
        {
            std::vector<PitchFrame> script;
            for (int i = 0; i < 10; ++i) // exactly 10 confident frames closes calibration successfully
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });
            // After calibration's window closes on the 10th call, the engine
            // (FakePitchEngine) repeats its last scripted frame (220Hz) for
            // all subsequent calls - so the first Live-phase call also sees
            // 220Hz, exactly at Sa (0 cents).

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            PitchPipeline pipeline (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f);

            PitchPipelineUpdate update { PitchPipelinePhase::Calibrating };
            for (int i = 0; i < 10; ++i)
                update = pipeline.process (block.data(), block.size());
            expect (update.phase == PitchPipelinePhase::Calibrating);
            expect (update.calibrationStatus == CalibrationStatus::Success);

            // Next call: pipeline should now be in Live phase, processing
            // through the engine + SwarMapper.
            auto liveUpdate = pipeline.process (block.data(), block.size());
            expect (liveUpdate.phase == PitchPipelinePhase::Live);
            expectWithinAbsoluteError (liveUpdate.saHz, 220.0f, 0.1f);
            expect (liveUpdate.swarLabel.has_value());
            if (liveUpdate.swarLabel.has_value())
                expectEquals ((int) liveUpdate.swarLabel->swar, (int) Swar::Sa);
        }

        beginTest ("An unvoiced frame during Live phase produces no swar label, and the following confident frame still resolves correctly");
        {
            // Script one entry per expected processFrame() call, explicitly:
            // 10 calibration frames, then a confident Live-phase frame (locks
            // SwarMapper to Sa), then an explicit unvoiced frame, then a
            // confident frame close to Sa again. PitchFrame's frequencyHz is
            // already std::optional<float>, so scripting nullopt directly
            // needs no changes to FakePitchEngine.
            std::vector<PitchFrame> script;
            for (int i = 0; i < 10; ++i) // calibration window
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });
            script.push_back (PitchFrame { 0, 220.0f, 0.9f });          // 11th call: first Live-phase frame, locks SwarMapper to Sa
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });    // 12th call: unvoiced
            script.push_back (PitchFrame { 0, 220.5f, 0.9f });          // 13th call: confident again, close to Sa

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            PitchPipeline pipeline (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f);

            for (int i = 0; i < 10; ++i)
                pipeline.process (block.data(), block.size()); // calibration

            auto firstLive = pipeline.process (block.data(), block.size()); // 11th call
            expect (firstLive.phase == PitchPipelinePhase::Live);
            expect (firstLive.swarLabel.has_value());
            if (firstLive.swarLabel.has_value())
                expectEquals ((int) firstLive.swarLabel->swar, (int) Swar::Sa);

            auto unvoicedUpdate = pipeline.process (block.data(), block.size()); // 12th call
            expect (! unvoicedUpdate.swarLabel.has_value());
            expect (! unvoicedUpdate.centsFromSa.has_value());

            auto afterUnvoiced = pipeline.process (block.data(), block.size()); // 13th call
            expect (afterUnvoiced.swarLabel.has_value());
            if (afterUnvoiced.swarLabel.has_value())
                expectEquals ((int) afterUnvoiced.swarLabel->swar, (int) Swar::Sa); // still locked to Sa via hysteresis, unaffected by the unvoiced gap
        }

        beginTest ("End-to-end: PitchPipeline calibrates and tracks live pitch using the real CrepePitchEngine");
        {
            CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
            auto prepareStatus = engine.prepare (16000.0);
            expect (prepareStatus == PitchEngineStatus::Ok);

            // windowMs=1000 with a 16384-sample (1024ms) buffer matches the
            // exact configuration TonicCalibrator's own end-to-end test
            // already proved sufficient for real ONNX inference to gather
            // >=10 confident readings (its default minConfidentReadings).
            PitchPipeline pipeline (engine, 16000.0, 1000);

            constexpr double sr = 16000.0;
            constexpr float freq = 220.0f;
            std::vector<float> sine (16384);
            for (size_t i = 0; i < sine.size(); ++i)
                sine[i] = std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / sr) * 0.5f;

            PitchPipelineUpdate update { PitchPipelinePhase::Calibrating };
            for (size_t offset = 0; offset + 1024 <= sine.size(); offset += 1024)
                update = pipeline.process (sine.data() + offset, 1024);

            expect (update.phase == PitchPipelinePhase::Calibrating);
            expect (update.calibrationStatus == CalibrationStatus::Success);

            // One more block of the same tone should now be Live-phase and resolve to Sa.
            auto liveUpdate = pipeline.process (sine.data(), 1024);
            expect (liveUpdate.phase == PitchPipelinePhase::Live);
            expect (liveUpdate.swarLabel.has_value());
            if (liveUpdate.swarLabel.has_value())
                expectEquals ((int) liveUpdate.swarLabel->swar, (int) Swar::Sa);
        }
    }
};

static PitchPipelineTests pitchPipelineTestsInstance;
```

This last test needs `CrepePitchEngine.h` included too:

```cpp
// Add near the top of src/audio/pipeline/PitchPipelineTests.cpp, alongside
// the existing #include "../calibration/FakePitchEngine.h":
#include "../pitchengine/CrepePitchEngine.h"
```

- [ ] **Step 2: Run to verify the build fails (`PitchPipeline.h` doesn't exist yet)**

Run: `cmake --build build --target riyaaz_tests` — expect a compile failure referencing the missing header. All three new tests fail together at this stage since none of them can compile without it.

- [ ] **Step 3: Implement `PitchPipeline`**

```cpp
// src/audio/pipeline/PitchPipeline.h
#pragma once
#include "../pitchengine/PitchEngine.h"
#include "../calibration/TonicCalibrator.h"
#include "../swarmap/SwarMapper.h"
#include <cmath>
#include <optional>

enum class PitchPipelinePhase { Calibrating, Live };

struct PitchPipelineUpdate
{
    PitchPipelinePhase phase;
    CalibrationStatus calibrationStatus = CalibrationStatus::InProgress;
    std::optional<SwarLabel> swarLabel;
    std::optional<float> centsFromSa;
    float saHz = 0.0f;
};

class PitchPipeline
{
public:
    // calibrationWindowMsIn is passed straight through to TonicCalibrator's
    // own windowMs constructor argument (default 3000, matching
    // TonicCalibrator's own default) - exposed here so tests (and,
    // eventually, a "quick recalibrate" UI mode) can use a shorter window
    // without needing a separate code path.
    PitchPipeline (PitchEngine& engineIn, double sampleRateIn, uint64_t calibrationWindowMsIn = 3000);

    PitchPipelineUpdate process (const float* audioFrame, size_t numSamples);
    void restartCalibration();

private:
    PitchEngine& engine;
    double sampleRate;
    TonicCalibrator calibrator; // constructed with calibrationWindowMs; not itself stored elsewhere, since nothing after construction needs to re-read it
    SwarMapper swarMapper;
    PitchPipelinePhase phase = PitchPipelinePhase::Calibrating;
    float saHz = 0.0f;

    PitchPipelineUpdate handleCalibrating (const float* audioFrame, size_t numSamples);
    PitchPipelineUpdate handleLive (const float* audioFrame, size_t numSamples);
};
```

```cpp
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
```

- [ ] **Step 4: Add the new library and test sources to `CMakeLists.txt`**

```cmake
# Add after the riyaaz_calibration target block:
add_library(riyaaz_pipeline STATIC
    src/audio/pipeline/PitchPipeline.cpp
)
target_include_directories(riyaaz_pipeline PUBLIC src)
target_link_libraries(riyaaz_pipeline PUBLIC riyaaz_calibration riyaaz_swarmap juce::juce_core)
target_compile_definitions(riyaaz_pipeline PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)
```

```cmake
# Add src/audio/pipeline/PitchPipelineTests.cpp to riyaaz_tests' target_sources,
# and riyaaz_pipeline to its target_link_libraries:
target_sources(riyaaz_tests PRIVATE
    src/audio/swarmap/SwarMapperTests.cpp
    src/audio/pitchengine/CrepeDecodeTests.cpp
    src/audio/pitchengine/PitchContinuityFilterTests.cpp
    src/audio/pitchengine/CrepePitchEngineTests.cpp
    src/audio/calibration/TonicCalibratorTests.cpp
    src/audio/pipeline/PitchPipelineTests.cpp
    src/TestMain.cpp
)
target_link_libraries(riyaaz_tests PRIVATE
    riyaaz_swarmap
    riyaaz_pitchengine
    riyaaz_calibration
    riyaaz_pipeline
    juce::juce_core
)
```

- [ ] **Step 5: Build and run, verify all tests pass**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: exit code 0, 42 tests total (38 from Tasks 1-3 + 4 new `PitchPipeline` tests, one of which runs real ONNX inference and is slower than the others — that's expected, not a bug).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/audio/pipeline/PitchPipeline.h src/audio/pipeline/PitchPipeline.cpp src/audio/pipeline/PitchPipelineTests.cpp
git commit -m "feat: add PitchPipeline orchestrator (Calibrating->Live phase state machine)"
```

---

## Not covered by this plan (separate plan)

- `juce::AbstractFifo` + a dedicated worker `juce::Thread` moving `PitchPipeline::process()` off the real-time audio thread.
- `juce::AsyncUpdater` (or equivalent) posting `PitchPipelineUpdate` results back to a UI thread.
- An actual `juce_add_gui_app` target: a `MainComponent`/`AudioAppComponent` capturing real microphone input, a calibration-progress display, and a live swar/cents display (a simple scrolling pitch graph, per the original plan's Milestone 2 scope — no scoring yet).
- Tanpura, metronome/taal engine, packaging — unchanged from the milestone order.
