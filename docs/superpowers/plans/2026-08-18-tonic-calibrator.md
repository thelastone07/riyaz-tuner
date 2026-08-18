# Tonic Calibrator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `TonicCalibrator` — feeds a few seconds of audio through an already-prepared `PitchEngine`, collects confident pitch readings, and produces a calibrated Sa (tonic) frequency, with explicit handling for the two failure modes the original eng-review flagged as critical gaps: the user doesn't sing during the window (timeout), and the pitch is present but too unstable to trust (instability).

**Architecture:** `TonicCalibrator` takes a `PitchEngine&` by reference (dependency injection, not ownership — the same engine instance keeps running afterward for live tracking) and a target window duration. Each `processFrame()` call forwards audio to the engine, accumulates confident frequency readings, and tracks elapsed time. Once the window closes, it classifies the result: zero confident readings → `Timeout`; readings present but too spread out (in cents, relative to the median) → `Unstable`; otherwise → `Success` with the median frequency as Sa. Median (not mean) is deliberate — it's robust to a handful of outlier frames without needing a separate outlier-rejection pass.

**Tech Stack:** C++17, JUCE 8.0.7, `juce::UnitTestRunner`. No ONNX Runtime dependency in the core logic — tested against a fake `PitchEngine` implementation, with one integration test against the real `CrepePitchEngine` to prove the whole stack works together.

## Global Constraints

- C++17. Test framework is `juce::UnitTestRunner`.
- `TonicCalibrator` must NOT own or construct a `PitchEngine` — it's injected by reference, since the same engine instance continues live tracking after calibration succeeds.
- Both failure modes (timeout, instability) must be distinct, reportable statuses — not silently treated the same, per the original eng-review's critical-gap finding.
- Median-of-confident-readings is the aggregation method (not mean), and instability is measured as spread-from-median in cents, not raw Hz (cents are the domain-correct unit for pitch spread — a 30Hz spread means something very different at 100Hz vs. 1000Hz, cents normalize that).
- Must not regress the 26 existing tests.

---

## File Structure

- `src/audio/calibration/TonicCalibrator.h` / `.cpp` — the calibrator
- `src/audio/calibration/FakePitchEngine.h` — a test-only `PitchEngine` implementation that returns a scripted sequence of `PitchFrame`s, letting calibrator tests run deterministically without real audio or ONNX Runtime
- `src/audio/calibration/TonicCalibratorTests.cpp`
- `CMakeLists.txt` — extended with the new sources

---

### Task 1: TonicCalibrator skeleton + success case, using a fake PitchEngine

**Files:**
- Create: `src/audio/calibration/TonicCalibrator.h`
- Create: `src/audio/calibration/TonicCalibrator.cpp`
- Create: `src/audio/calibration/FakePitchEngine.h`
- Test: `src/audio/calibration/TonicCalibratorTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `PitchEngine`, `PitchFrame`, `PitchEngineStatus` from `src/audio/pitchengine/PitchEngine.h` (already built)
- Produces:
  - `enum class CalibrationStatus { InProgress, Success, Timeout, Unstable };`
  - `struct CalibrationResult { CalibrationStatus status; std::optional<float> saHz; };`
  - `class TonicCalibrator { public: TonicCalibrator (PitchEngine& engine, double sampleRate, uint64_t windowMs = 3000); CalibrationResult processFrame (const float* audioFrame, size_t numSamples); void reset(); };`
  - `class FakePitchEngine : public PitchEngine` — test double, constructed with a `std::vector<PitchFrame>` script; each `processFrame()` call returns the next scripted frame (cycling or clamping at the end — implementer's choice, document which).

- [ ] **Step 1: Write `FakePitchEngine.h` (test double, not itself tested — it's test infrastructure)**

```cpp
// src/audio/calibration/FakePitchEngine.h
#pragma once
#include "../pitchengine/PitchEngine.h"
#include <vector>

// Test double: returns a pre-scripted sequence of PitchFrames, one per
// processFrame() call, regardless of the actual audio content passed in.
// Once the script is exhausted, repeats the last frame. Lets calibrator
// tests run deterministically without real audio or ONNX Runtime.
class FakePitchEngine : public PitchEngine
{
public:
    explicit FakePitchEngine (std::vector<PitchFrame> scriptedFrames)
        : script (std::move (scriptedFrames))
    {
    }

    PitchEngineStatus prepare (double) override
    {
        status = PitchEngineStatus::Ok;
        return status;
    }

    void reset() override
    {
        nextIndex = 0;
    }

    PitchFrame processFrame (const float*, size_t) override
    {
        if (script.empty())
            return PitchFrame { 0, std::nullopt, 0.0f };

        const auto& frame = script[std::min (nextIndex, script.size() - 1)];
        if (nextIndex < script.size() - 1)
            ++nextIndex;
        return frame;
    }

    PitchEngineStatus getStatus() const override { return status; }

private:
    std::vector<PitchFrame> script;
    size_t nextIndex = 0;
    PitchEngineStatus status = PitchEngineStatus::NotPrepared;
};
```

- [ ] **Step 2: Write the failing test — a run of stable confident frames succeeds with the correct median Sa**

```cpp
// src/audio/calibration/TonicCalibratorTests.cpp
#include "TonicCalibrator.h"
#include "FakePitchEngine.h"
#include <juce_core/juce_core.h>
#include <vector>

class TonicCalibratorTests : public juce::UnitTest
{
public:
    TonicCalibratorTests() : juce::UnitTest ("TonicCalibrator", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("A window of stable confident frames succeeds with the median frequency as Sa");
        {
            // 10 frames, all close to 220Hz with minor jitter, all confident.
            std::vector<PitchFrame> script;
            const float freqs[] = { 219.5f, 220.1f, 219.8f, 220.3f, 220.0f,
                                     219.9f, 220.2f, 220.0f, 219.7f, 220.4f };
            for (float f : freqs)
                script.push_back (PitchFrame { 0, f, 0.9f });

            FakePitchEngine engine (script);
            engine.prepare (44100.0);

            // Feed frames in 300ms chunks (10 * 300ms = 3000ms = default window)
            TonicCalibrator calibrator (engine, 44100.0);
            std::vector<float> block (44100 * 300 / 1000, 0.0f); // 300ms of silence (content ignored by the fake)

            CalibrationResult result { CalibrationStatus::InProgress, std::nullopt };
            for (int i = 0; i < 10; ++i)
                result = calibrator.processFrame (block.data(), block.size());

            expect (result.status == CalibrationStatus::Success);
            expect (result.saHz.has_value());
            if (result.saHz.has_value())
                expectWithinAbsoluteError (*result.saHz, 220.0f, 1.0f); // median of the jittery values, well within 1Hz
        }
    }
};

static TonicCalibratorTests tonicCalibratorTestsInstance;
```

- [ ] **Step 3: Run to verify it fails (`TonicCalibrator.h` doesn't exist)**

Run:
```
cmake -B build -S . -G Ninja -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --target riyaaz_tests
```

- [ ] **Step 4: Implement `TonicCalibrator`**

```cpp
// src/audio/calibration/TonicCalibrator.h
#pragma once
#include "../pitchengine/PitchEngine.h"
#include <cstdint>
#include <optional>
#include <vector>

enum class CalibrationStatus
{
    InProgress,
    Success,
    Timeout,
    Unstable
};

struct CalibrationResult
{
    CalibrationStatus status;
    std::optional<float> saHz;
};

class TonicCalibrator
{
public:
    TonicCalibrator (PitchEngine& engineIn, double sampleRateIn, uint64_t windowMsIn = 3000);

    CalibrationResult processFrame (const float* audioFrame, size_t numSamples);
    void reset();

private:
    PitchEngine& engine;
    double sampleRate;
    uint64_t windowMs;

    uint64_t elapsedMs = 0;
    std::vector<float> confidentFrequencies;
    CalibrationResult finalResult { CalibrationStatus::InProgress, std::nullopt };
};
```

```cpp
// src/audio/calibration/TonicCalibrator.cpp
#include "TonicCalibrator.h"

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

    // Window closed - classify. (Timeout/Unstable logic lands in Tasks 2-3;
    // for this task, treat a non-empty result as always Success.)
    if (confidentFrequencies.empty())
    {
        finalResult = { CalibrationStatus::Timeout, std::nullopt };
        return finalResult;
    }

    std::vector<float> sorted = confidentFrequencies;
    std::sort (sorted.begin(), sorted.end());
    const float median = sorted[sorted.size() / 2];

    finalResult = { CalibrationStatus::Success, median };
    return finalResult;
}

void TonicCalibrator::reset()
{
    elapsedMs = 0;
    confidentFrequencies.clear();
    finalResult = { CalibrationStatus::InProgress, std::nullopt };
}
```

Add `#include <algorithm>` to `TonicCalibrator.cpp` for `std::sort`.

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
    src/audio/calibration/TonicCalibrator.cpp
    src/audio/calibration/TonicCalibratorTests.cpp
    src/TestMain.cpp
)
```

(Keep every existing entry — this is additive.)

- [ ] **Step 6: Build and run, verify all tests pass**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: exit code 0, 27 tests total (26 existing + 1 new).

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/audio/calibration/TonicCalibrator.h src/audio/calibration/TonicCalibrator.cpp src/audio/calibration/FakePitchEngine.h src/audio/calibration/TonicCalibratorTests.cpp
git commit -m "feat: add TonicCalibrator with FakePitchEngine test double"
```

---

### Task 2: Timeout handling — zero confident frames during the window

**Files:**
- Test: `src/audio/calibration/TonicCalibratorTests.cpp`

**Interfaces:**
- Consumes: `TonicCalibrator`, `FakePitchEngine`, `CalibrationStatus::Timeout` (already defined in Task 1)
- Produces: nothing new — Task 1's implementation already handles this branch; this task locks it down with an explicit test

- [ ] **Step 1: Write the test**

```cpp
beginTest ("A window with zero confident frames (user never sings) reports Timeout");
{
    // Script of all-unvoiced frames - simulates silence for the whole window.
    std::vector<PitchFrame> script;
    for (int i = 0; i < 10; ++i)
        script.push_back (PitchFrame { 0, std::nullopt, 0.0f });

    FakePitchEngine engine (script);
    engine.prepare (44100.0);

    TonicCalibrator calibrator (engine, 44100.0);
    std::vector<float> block (44100 * 300 / 1000, 0.0f);

    CalibrationResult result { CalibrationStatus::InProgress, std::nullopt };
    for (int i = 0; i < 10; ++i)
        result = calibrator.processFrame (block.data(), block.size());

    expect (result.status == CalibrationStatus::Timeout);
    expect (! result.saHz.has_value());
}
```

- [ ] **Step 2: Build and run, verify it passes (Task 1's implementation already handles this)**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add src/audio/calibration/TonicCalibratorTests.cpp
git commit -m "test: verify TonicCalibrator reports Timeout when no confident pitch is detected"
```

---

### Task 3: Instability handling — confident frames present but too spread out

**Files:**
- Modify: `src/audio/calibration/TonicCalibrator.cpp`
- Test: `src/audio/calibration/TonicCalibratorTests.cpp`

**Interfaces:**
- Consumes: `TonicCalibrator` (Task 1)
- Produces: `CalibrationStatus::Unstable` becomes reachable (currently defined but unused)

- [ ] **Step 1: Write the failing test**

```cpp
beginTest ("Confident frames spread across a wide pitch range (unstable singing) report Unstable, not Success");
{
    // Frames confidently detected but spanning ~400 cents (a wide, unstable
    // range - not the tight jitter of a held note).
    std::vector<PitchFrame> script;
    const float freqs[] = { 200.0f, 220.0f, 240.0f, 195.0f, 250.0f,
                             205.0f, 235.0f, 210.0f, 245.0f, 200.0f };
    for (float f : freqs)
        script.push_back (PitchFrame { 0, f, 0.9f });

    FakePitchEngine engine (script);
    engine.prepare (44100.0);

    TonicCalibrator calibrator (engine, 44100.0);
    std::vector<float> block (44100 * 300 / 1000, 0.0f);

    CalibrationResult result { CalibrationStatus::InProgress, std::nullopt };
    for (int i = 0; i < 10; ++i)
        result = calibrator.processFrame (block.data(), block.size());

    expect (result.status == CalibrationStatus::Unstable);
    expect (! result.saHz.has_value());
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: FAIL — Task 1's implementation always returns `Success` for any non-empty confident-frequency list, so this currently reports `Success` with a median in the 200-250Hz range instead of `Unstable`.

- [ ] **Step 3: Add instability detection to `TonicCalibrator::processFrame()`**

Replace the "Window closed - classify" block in `TonicCalibrator.cpp`:

```cpp
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
```

Add `#include <cmath>` to `TonicCalibrator.cpp` for `std::log2`.

- [ ] **Step 4: Build and run, verify all tests pass, including Task 1's original success-case test (which must NOT have become Unstable now that the check exists)**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: exit code 0, all tests passing. Task 1's success-case test uses freqs within ~0.9Hz of 220Hz (a few cents of spread) — well under the 80-cent threshold, so it must still report `Success`. If it doesn't, the threshold or the test data needs adjustment — report the actual computed spread in cents if this happens, don't just widen the threshold blindly.

- [ ] **Step 5: Commit**

```bash
git add src/audio/calibration/TonicCalibrator.cpp src/audio/calibration/TonicCalibratorTests.cpp
git commit -m "feat: add instability detection to TonicCalibrator (cents-spread threshold)"
```

---

### Task 4: reset() behavior and idempotent post-completion calls

**Files:**
- Test: `src/audio/calibration/TonicCalibratorTests.cpp`

**Interfaces:**
- Consumes: `TonicCalibrator::reset()` (already defined in Task 1, exercised for the first time here)

- [ ] **Step 1: Write the failing tests**

```cpp
beginTest ("Calling processFrame() after the window has closed returns the same final result without re-classifying");
{
    std::vector<PitchFrame> script;
    for (int i = 0; i < 10; ++i)
        script.push_back (PitchFrame { 0, 220.0f, 0.9f });

    FakePitchEngine engine (script);
    engine.prepare (44100.0);

    TonicCalibrator calibrator (engine, 44100.0);
    std::vector<float> block (44100 * 300 / 1000, 0.0f);

    CalibrationResult result { CalibrationStatus::InProgress, std::nullopt };
    for (int i = 0; i < 10; ++i)
        result = calibrator.processFrame (block.data(), block.size());
    expect (result.status == CalibrationStatus::Success);

    // One more call past the window - must return the SAME result, not
    // re-run classification on a now-stale confidentFrequencies list.
    auto again = calibrator.processFrame (block.data(), block.size());
    expect (again.status == CalibrationStatus::Success);
    expectWithinAbsoluteError (*again.saHz, *result.saHz, 0.001f);
}

beginTest ("reset() clears state so a fresh calibration window can start");
{
    std::vector<PitchFrame> script;
    for (int i = 0; i < 10; ++i)
        script.push_back (PitchFrame { 0, 220.0f, 0.9f });

    FakePitchEngine engine (script);
    engine.prepare (44100.0);

    TonicCalibrator calibrator (engine, 44100.0);
    std::vector<float> block (44100 * 300 / 1000, 0.0f);

    for (int i = 0; i < 10; ++i)
        calibrator.processFrame (block.data(), block.size());

    calibrator.reset();
    engine.reset();

    // Immediately after reset, a single short frame should NOT have closed
    // the window yet (elapsedMs should be back to 0).
    auto result = calibrator.processFrame (block.data(), block.size());
    expect (result.status == CalibrationStatus::InProgress);
}
```

- [ ] **Step 2: Run to verify these pass (Task 1's implementation already supports both)**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: PASS. If the idempotent-post-completion test fails, check that `processFrame()`'s early return (`if (finalResult.status != InProgress) return finalResult;`) is actually the first statement in the function body.

- [ ] **Step 3: Commit**

```bash
git add src/audio/calibration/TonicCalibratorTests.cpp
git commit -m "test: verify TonicCalibrator idempotence after window close and reset() behavior"
```

---

### Task 5: Integration test against the real CrepePitchEngine

**Files:**
- Test: `src/audio/calibration/TonicCalibratorTests.cpp`

**Interfaces:**
- Consumes: `CrepePitchEngine` (real, from the prior plan) instead of `FakePitchEngine`, proving the whole stack (resample → window → infer → decode → continuity-filter → calibrate) works together, not just each piece in isolation

- [ ] **Step 1: Write the test**

```cpp
#include "../pitchengine/CrepePitchEngine.h"

// ... inside runTest():

beginTest ("End-to-end: a real 220Hz sine through CrepePitchEngine calibrates to ~220Hz Sa");
{
    CrepePitchEngine engine (juce::String ("models/crepe/small.onnx"));
    auto prepareStatus = engine.prepare (16000.0); // native rate, same reasoning as the PitchEngine plan's sine test
    expect (prepareStatus == PitchEngineStatus::Ok);

    TonicCalibrator calibrator (engine, 16000.0, 1000); // shorter window for test speed - 1s instead of 3s

    constexpr double sr = 16000.0;
    constexpr float freq = 220.0f;
    std::vector<float> sine (16000); // 1 second
    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] = std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / sr) * 0.5f;

    CalibrationResult result { CalibrationStatus::InProgress, std::nullopt };
    for (size_t offset = 0; offset + 1024 <= sine.size(); offset += 1024)
        result = calibrator.processFrame (sine.data() + offset, 1024);

    expect (result.status == CalibrationStatus::Success);
    if (result.status == CalibrationStatus::Success && result.saHz.has_value())
        expectWithinAbsoluteError (*result.saHz, 220.0f, 5.0f);
}
```

- [ ] **Step 2: Add `src/audio/pitchengine/CrepePitchEngine.cpp` to this test's compilation if not already linked, build and run**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: exit code 0, all tests passing (30 total: 27 from Tasks 1-4 + 3 new... recount based on actual tests added; report the true final count). This test is slower than the others (real ONNX inference) — that's expected, not a bug.

If the window doesn't reach `Success` within 1 second at a 16kHz native rate feeding 1024-sample chunks (16 inferences in 1 second, well above what's needed for the confidence check), investigate rather than lengthening the window blindly — this exact configuration was already proven to work in the PitchEngine plan's own sine-wave test.

- [ ] **Step 3: Commit**

```bash
git add src/audio/calibration/TonicCalibratorTests.cpp
git commit -m "test: add end-to-end TonicCalibrator + CrepePitchEngine integration test"
```

---

## Not covered by this plan (separate plans)

- Real-time audio pipeline: `juce::AbstractFifo`, worker thread, `AsyncUpdater`, wiring calibration + live tracking into an actual JUCE `AudioAppComponent`/GUI window. This plan's `TonicCalibrator` is synchronous/blocking like `CrepePitchEngine` — correct for now, not yet real-time-thread-safe.
- UI for the calibration flow itself ("sing your Sa, hold for 3 seconds" prompt, visual feedback, retry-on-Timeout/Unstable messaging to the user).
- Tanpura, metronome/taal engine, packaging — unchanged from the milestone order.
