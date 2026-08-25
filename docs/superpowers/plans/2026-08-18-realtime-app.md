# Real-Time App Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire `PitchPipeline` behind a real-time-safe audio thread → worker thread → UI thread pipeline, and ship a minimal JUCE GUI app that captures real microphone input, shows calibration progress, then a live swar label and a scrolling pitch graph — the first milestone that's an actual runnable application, not a test suite.

**Architecture:** `PitchWorker` owns a `juce::AbstractFifo`-backed ring buffer and a `juce::Thread`. The real-time audio callback calls `pushAudio()`, which copies samples into the FIFO and returns immediately — it never blocks, never allocates, and silently drops samples if the FIFO is full rather than risk stalling the audio thread. The worker thread wakes on `notify()`, drains everything currently in the FIFO in one call to `PitchPipeline::process()`, and hands the resulting `PitchPipelineUpdate` to the UI thread via `juce::AsyncUpdater` (a small mutex-protected "latest value" handoff, not a queue — only the newest result matters for a live display). `MainComponent` (a `juce::AudioAppComponent`) owns the real `CrepePitchEngine` + `PitchPipeline` + `PitchWorker`, constructing the latter two in `prepareToPlay()` once the real device sample rate is known, and repaints a status label + `PitchGraphComponent` each time a `PitchWorkerUpdate` arrives.

**Tech Stack:** C++17, JUCE 8.0.7 (`juce_audio_utils`, `juce_gui_extra`, and their transitive dependencies — `juce_audio_devices`, `juce_audio_basics`, `juce_gui_basics`, `juce_graphics`, `juce_events`, `juce_data_structures`), everything already built (`riyaaz_pipeline`, `riyaaz_pitchengine`).

## Global Constraints

- C++17. `juce::UnitTestRunner` for everything that can be tested without a real audio device or a visible window.
- `PitchWorker::pushAudio()` must be real-time-safe: no heap allocation, no locks that could be held by a lower-priority thread for long, no blocking. If the FIFO doesn't have room for all of `numSamples`, the excess is silently dropped — never grow the buffer, never block waiting for the worker thread to catch up.
- `PitchWorker` hands off only the LATEST `PitchPipelineUpdate` to the UI thread, not a queue of all of them — a live display only needs the current state, matching the same "latest wins" principle `CrepePitchEngine`'s multi-window draining already uses.
- `CrepePitchEngine`, `PitchPipeline`, and `PitchWorker` must all be constructed AFTER the real device sample rate is known (inside `prepareToPlay()`), never in `MainComponent`'s constructor.
- **What this plan cannot verify by itself:** whether the app actually captures your voice correctly, whether the calibration flow feels right, and whether the live graph is readable are all things that need you to actually run the app and try it — the same way the M0 spike needed your ears, not just passing tests. Each task's automated verification is scoped to what's actually testable (unit tests for `PitchWorker`'s threading logic, a data-model test for the graph's point buffer); the final task's "it launches and doesn't crash" check is not the same as "it works," and this plan says so explicitly rather than overclaiming.

---

## File Structure

- `src/audio/worker/PitchWorker.h` / `.cpp` — the FIFO + thread + AsyncUpdater bridge
- `src/audio/worker/PitchWorkerTests.cpp`
- `src/ui/PitchGraphComponent.h` / `.cpp` — the scrolling pitch graph
- `src/ui/PitchGraphComponent.cpp`'s point-buffer logic gets its own test file: `src/ui/PitchGraphPointBufferTests.cpp` (tests the data model, not `paint()`)
- `src/app/MainComponent.h` / `.cpp`
- `src/app/Main.cpp` — `JUCEApplication` + `START_JUCE_APPLICATION`
- `CMakeLists.txt` — new `riyaaz_worker` library, new `riyaaz_app` GUI executable target

---

### Task 1: PitchWorker (AbstractFifo + Thread + AsyncUpdater bridge)

**Files:**
- Create: `src/audio/worker/PitchWorker.h`
- Create: `src/audio/worker/PitchWorker.cpp`
- Test: `src/audio/worker/PitchWorkerTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `PitchPipeline`, `PitchPipelineUpdate` (pipeline library)
- Produces:
  ```cpp
  class PitchWorker : private juce::Thread, private juce::AsyncUpdater
  {
  public:
      struct Listener
      {
          virtual ~Listener() = default;
          virtual void pitchWorkerUpdate (const PitchPipelineUpdate& update) = 0;
      };

      PitchWorker (PitchPipeline& pipelineIn, Listener& listenerIn, int fifoCapacitySamples = 16384);
      ~PitchWorker() override;

      void pushAudio (const float* samples, int numSamples); // real-time-safe
      void start();
      void stop();
  };
  ```

- [ ] **Step 1: Write the failing test — pushed audio reaches the listener via the worker thread**

```cpp
// src/audio/worker/PitchWorkerTests.cpp
#include "PitchWorker.h"
#include "../calibration/FakePitchEngine.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <vector>

namespace
{
    class CapturingListener : public PitchWorker::Listener
    {
    public:
        void pitchWorkerUpdate (const PitchPipelineUpdate& update) override
        {
            lastUpdate = update;
            gotUpdate.signal();
        }

        juce::WaitableEvent gotUpdate;
        PitchPipelineUpdate lastUpdate { PitchPipelinePhase::Calibrating };
    };
}

class PitchWorkerTests : public juce::UnitTest
{
public:
    PitchWorkerTests() : juce::UnitTest ("PitchWorker", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("Audio pushed on the calling thread reaches the listener on the message thread");
        {
            // A message manager is required for AsyncUpdater's callback to be
            // delivered - juce::UnitTestRunner runs on a thread that already
            // has one when run via the console app's TestMain, but construct
            // one defensively so this test is self-contained if ever run
            // standalone.
            juce::ScopedJuceInitialiser_GUI juceInit;

            std::vector<PitchFrame> script;
            for (int i = 0; i < 9; ++i)
                script.push_back (PitchFrame { 0, 220.0f, 0.9f });
            script.push_back (PitchFrame { 0, std::nullopt, 0.0f });
            // 9 confident + 1 unvoiced = Timeout after the calibration window
            // closes - this test only needs to prove audio reaches the
            // pipeline and a result comes back, not a specific outcome.

            FakePitchEngine engine (script);
            engine.prepare (44100.0);
            PitchPipeline pipeline (engine, 44100.0);

            CapturingListener listener;
            PitchWorker worker (pipeline, listener);
            worker.start();

            std::vector<float> block (44100 * 300 / 1000, 0.0f); // 300ms, matches the project's established test convention
            for (int i = 0; i < 10; ++i)
            {
                worker.pushAudio (block.data(), (int) block.size());
                // Give the worker thread and message loop a chance to run
                // between pushes - dispatchNextMessageOnSystemQueue-style
                // pumping isn't available off the message thread, so just
                // wait briefly; the real assertion is the WaitableEvent below.
                juce::Thread::sleep (5);
            }

            const bool gotIt = listener.gotUpdate.wait (2000.0);
            worker.stop();

            expect (gotIt);
            expect (listener.lastUpdate.calibrationStatus == CalibrationStatus::Timeout);
        }
    }
};

static PitchWorkerTests pitchWorkerTestsInstance;
```

Note on message-thread delivery in a console test runner: `juce::AsyncUpdater::triggerAsyncUpdate()` posts a message that is delivered the next time the message loop runs. `TestMain.cpp` (the console app's `main()`) does not run a JUCE message loop today. If this test hangs (the `WaitableEvent` never signals) rather than failing cleanly, that is the cause — see Step 4 below, which addresses it directly rather than leaving it as a mystery for the next task.

- [ ] **Step 2: Run to verify it fails (`PitchWorker.h` doesn't exist)**

Run:
```
cmake -B build -S . -G Ninja -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --target riyaaz_tests
```
Expected: build failure, missing header.

- [ ] **Step 3: Implement `PitchWorker`**

```cpp
// src/audio/worker/PitchWorker.h
#pragma once
#include "../pipeline/PitchPipeline.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <vector>

class PitchWorker : private juce::Thread, private juce::AsyncUpdater
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void pitchWorkerUpdate (const PitchPipelineUpdate& update) = 0;
    };

    PitchWorker (PitchPipeline& pipelineIn, Listener& listenerIn, int fifoCapacitySamples = 16384);
    ~PitchWorker() override;

    // Real-time-safe: no allocation, no blocking. Drops the excess silently
    // if the FIFO doesn't have room for all of numSamples.
    void pushAudio (const float* samples, int numSamples);

    void start();
    void stop();

private:
    void run() override;
    void handleAsyncUpdate() override;

    PitchPipeline& pipeline;
    Listener& listener;

    juce::AbstractFifo fifo;
    std::vector<float> fifoBuffer;
    std::vector<float> drainScratch;

    juce::CriticalSection latestUpdateLock;
    PitchPipelineUpdate latestUpdate { PitchPipelinePhase::Calibrating };
};
```

```cpp
// src/audio/worker/PitchWorker.cpp
#include "PitchWorker.h"
#include <algorithm>

PitchWorker::PitchWorker (PitchPipeline& pipelineIn, Listener& listenerIn, int fifoCapacitySamples)
    : Thread ("PitchWorker"),
      pipeline (pipelineIn), listener (listenerIn),
      fifo (fifoCapacitySamples), fifoBuffer ((size_t) fifoCapacitySamples),
      drainScratch ((size_t) fifoCapacitySamples)
{
}

PitchWorker::~PitchWorker()
{
    stop();
}

void PitchWorker::pushAudio (const float* samples, int numSamples)
{
    auto writeHandle = fifo.write (numSamples);
    int written = 0;

    if (writeHandle.blockSize1 > 0)
    {
        std::copy (samples, samples + writeHandle.blockSize1,
                   fifoBuffer.begin() + writeHandle.startIndex1);
        written += writeHandle.blockSize1;
    }
    if (writeHandle.blockSize2 > 0)
    {
        std::copy (samples + written, samples + written + writeHandle.blockSize2,
                   fifoBuffer.begin() + writeHandle.startIndex2);
    }
    // If blockSize1 + blockSize2 < numSamples, the FIFO was full and the
    // remainder is dropped - by design, never blocks or grows here.

    notify();
}

void PitchWorker::start()
{
    startThread (Priority::normal);
}

void PitchWorker::stop()
{
    stopThread (1000);
}

void PitchWorker::run()
{
    while (! threadShouldExit())
    {
        wait (100.0); // wake on notify(), or periodically as a safety net

        while (fifo.getNumReady() > 0 && ! threadShouldExit())
        {
            auto readHandle = fifo.read (fifo.getNumReady());
            int n = 0;

            if (readHandle.blockSize1 > 0)
            {
                std::copy (fifoBuffer.begin() + readHandle.startIndex1,
                           fifoBuffer.begin() + readHandle.startIndex1 + readHandle.blockSize1,
                           drainScratch.begin());
                n += readHandle.blockSize1;
            }
            if (readHandle.blockSize2 > 0)
            {
                std::copy (fifoBuffer.begin() + readHandle.startIndex2,
                           fifoBuffer.begin() + readHandle.startIndex2 + readHandle.blockSize2,
                           drainScratch.begin() + n);
                n += readHandle.blockSize2;
            }

            auto update = pipeline.process (drainScratch.data(), (size_t) n);

            {
                const juce::ScopedLock sl (latestUpdateLock);
                latestUpdate = update;
            }
            triggerAsyncUpdate();
        }
    }
}

void PitchWorker::handleAsyncUpdate()
{
    PitchPipelineUpdate update { PitchPipelinePhase::Calibrating };
    {
        const juce::ScopedLock sl (latestUpdateLock);
        update = latestUpdate;
    }
    listener.pitchWorkerUpdate (update);
}
```

- [ ] **Step 4: Verify (and if needed, fix) message-thread delivery in the console test runner**

Before running the test, check whether `src/TestMain.cpp` pumps a JUCE message loop. Read the file. `juce::AsyncUpdater` callbacks are delivered by the message thread's event loop; a plain `main()` that just calls `runner.runAllTests()` and returns does not run one, so `triggerAsyncUpdate()` would never actually invoke `handleAsyncUpdate()` and this test would hang until its own `wait(2000.0)` times out and fails.

If `TestMain.cpp` has no message loop, this task must add one WITHOUT breaking the existing test run (which must still exit promptly with a proper pass/fail code, not hang forever as a GUI app would). The standard approach: wrap the existing `runner.runAllTests()` call so it runs on a background thread while the main thread pumps `juce::MessageManager::getInstance()->runDispatchLoopUntil(...)` in short slices, or more simply, since this project has no other async-updater-dependent tests today, add a small message-pump helper that `PitchWorkerTests` itself calls between `pushAudio()` calls (e.g. `juce::MessageManager::getInstance()->runDispatchLoopUntil (5);` instead of `juce::Thread::sleep (5);` in the test's loop) rather than restructuring `TestMain.cpp`'s overall flow. Prefer the smaller, test-local fix over restructuring shared test infrastructure — but if you find the message-pump call requires `TestMain.cpp` to have initialized a `MessageManager` first (via `juce::ScopedJuceInitialiser_GUI` or `juce::MessageManager::getInstance()`), that initialization can live in `TestMain.cpp` itself (it's cheap and harmless for the other tests, which don't use it). Investigate and choose the smaller fix; do not guess blindly — report what you found and what you changed.

- [ ] **Step 5: Add to CMakeLists.txt**

```cmake
# New library, after the riyaaz_pipeline block:
add_library(riyaaz_worker STATIC
    src/audio/worker/PitchWorker.cpp
)
target_include_directories(riyaaz_worker PUBLIC src)
target_link_libraries(riyaaz_worker PUBLIC riyaaz_pipeline juce::juce_core juce::juce_events)
target_compile_definitions(riyaaz_worker PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)
```

```cmake
# Add to riyaaz_tests' target_sources and target_link_libraries:
target_sources(riyaaz_tests PRIVATE
    ...(existing entries)...
    src/audio/worker/PitchWorkerTests.cpp
)
target_link_libraries(riyaaz_tests PRIVATE
    ...(existing entries)...
    riyaaz_worker
)
```

- [ ] **Step 6: Build and run, verify all tests pass**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: exit code 0, 44 tests total (43 existing + 1 new). If the test hangs, that confirms Step 4's diagnosis was needed but not fully resolved — do not increase the timeout as a workaround; fix the message-pump issue.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/audio/worker/PitchWorker.h src/audio/worker/PitchWorker.cpp src/audio/worker/PitchWorkerTests.cpp src/TestMain.cpp
git commit -m "feat: add PitchWorker (AbstractFifo + Thread + AsyncUpdater real-time bridge)"
```
(Include `src/TestMain.cpp` in the `git add` only if Step 4 required changing it.)

---

### Task 2: PitchGraphComponent (point-buffer data model + minimal rendering)

**Files:**
- Create: `src/ui/PitchGraphComponent.h`
- Create: `src/ui/PitchGraphComponent.cpp`
- Test: `src/ui/PitchGraphPointBufferTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from other project modules (pure JUCE + standard library)
- Produces:
  ```cpp
  struct PitchGraphPoint { uint64_t timestampMs; float centsFromSa; };

  // Pure data model: a rolling window of points, oldest evicted once the
  // window exceeds maxAgeMs. Separated from PitchGraphComponent so the
  // eviction/ordering logic is unit-testable without a Component or a
  // rendering context.
  class PitchGraphPointBuffer
  {
  public:
      explicit PitchGraphPointBuffer (uint64_t maxAgeMsIn = 8000);
      void addPoint (uint64_t timestampMs, float centsFromSa);
      void clear();
      const std::deque<PitchGraphPoint>& getPoints() const;
  private:
      uint64_t maxAgeMs;
      std::deque<PitchGraphPoint> points;
  };

  class PitchGraphComponent : public juce::Component
  {
  public:
      PitchGraphComponent();
      void addPoint (uint64_t timestampMs, float centsFromSa); // forwards to an owned PitchGraphPointBuffer, then repaint()
      void clear();
      void paint (juce::Graphics&) override;
  private:
      PitchGraphPointBuffer buffer;
  };
  ```

- [ ] **Step 1: Write the failing tests for `PitchGraphPointBuffer`**

```cpp
// src/ui/PitchGraphPointBufferTests.cpp
#include "PitchGraphComponent.h"
#include <juce_core/juce_core.h>

class PitchGraphPointBufferTests : public juce::UnitTest
{
public:
    PitchGraphPointBufferTests() : juce::UnitTest ("PitchGraphPointBuffer", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("Points are stored in insertion order and retrievable");
        {
            PitchGraphPointBuffer buffer (8000);
            buffer.addPoint (0, 0.0f);
            buffer.addPoint (100, 5.0f);
            buffer.addPoint (200, -3.0f);

            auto& points = buffer.getPoints();
            expectEquals ((int) points.size(), 3);
            expectEquals ((long long) points[0].timestampMs, (long long) 0);
            expectWithinAbsoluteError (points[2].centsFromSa, -3.0f, 0.01f);
        }

        beginTest ("Points older than maxAgeMs relative to the newest point are evicted");
        {
            PitchGraphPointBuffer buffer (1000); // 1 second window
            buffer.addPoint (0, 0.0f);
            buffer.addPoint (500, 1.0f);
            buffer.addPoint (2500, 2.0f); // newest - window is now [1500, 2500]

            auto& points = buffer.getPoints();
            // Points at 0 and 500 are both older than 2500 - 1000 = 1500, so both evicted.
            expectEquals ((int) points.size(), 1);
            expectEquals ((long long) points[0].timestampMs, (long long) 2500);
        }

        beginTest ("clear() removes all points");
        {
            PitchGraphPointBuffer buffer (8000);
            buffer.addPoint (0, 0.0f);
            buffer.addPoint (100, 1.0f);
            buffer.clear();

            expect (buffer.getPoints().empty());
        }
    }
};

static PitchGraphPointBufferTests pitchGraphPointBufferTestsInstance;
```

- [ ] **Step 2: Run to verify these fail (`PitchGraphComponent.h` doesn't exist)**

Run: `cmake --build build --target riyaaz_tests` — expect a build failure.

- [ ] **Step 3: Implement `PitchGraphPointBuffer` and `PitchGraphComponent`**

```cpp
// src/ui/PitchGraphComponent.h
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <deque>

struct PitchGraphPoint
{
    uint64_t timestampMs;
    float centsFromSa;
};

class PitchGraphPointBuffer
{
public:
    explicit PitchGraphPointBuffer (uint64_t maxAgeMsIn = 8000);

    void addPoint (uint64_t timestampMs, float centsFromSa);
    void clear();
    const std::deque<PitchGraphPoint>& getPoints() const { return points; }

private:
    uint64_t maxAgeMs;
    std::deque<PitchGraphPoint> points;
};

class PitchGraphComponent : public juce::Component
{
public:
    PitchGraphComponent();

    void addPoint (uint64_t timestampMs, float centsFromSa);
    void clear();

    void paint (juce::Graphics& g) override;

private:
    PitchGraphPointBuffer buffer;
};
```

```cpp
// src/ui/PitchGraphComponent.cpp
#include "PitchGraphComponent.h"

PitchGraphPointBuffer::PitchGraphPointBuffer (uint64_t maxAgeMsIn)
    : maxAgeMs (maxAgeMsIn)
{
}

void PitchGraphPointBuffer::addPoint (uint64_t timestampMs, float centsFromSa)
{
    points.push_back ({ timestampMs, centsFromSa });

    while (! points.empty() && points.front().timestampMs + maxAgeMs < timestampMs)
        points.pop_front();
}

void PitchGraphPointBuffer::clear()
{
    points.clear();
}

PitchGraphComponent::PitchGraphComponent()
{
}

void PitchGraphComponent::addPoint (uint64_t timestampMs, float centsFromSa)
{
    buffer.addPoint (timestampMs, centsFromSa);
    repaint();
}

void PitchGraphComponent::clear()
{
    buffer.clear();
    repaint();
}

void PitchGraphComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    const auto& points = buffer.getPoints();
    if (points.size() < 2)
        return;

    const auto bounds = getLocalBounds().toFloat();
    const uint64_t newestMs = points.back().timestampMs;
    const uint64_t oldestMs = points.front().timestampMs;
    const float timeSpanMs = (float) juce::jmax<uint64_t> (1, newestMs - oldestMs);

    // Cents axis: fixed +/-100 cent range (one swar-width either side of
    // center), clamped - enough to see meend/gamak motion within a swar
    // without the graph rescaling constantly, which is not what
    // "no scoring yet, just a scrolling graph" calls for.
    constexpr float kCentsRange = 100.0f;

    juce::Path path;
    bool started = false;

    for (const auto& p : points)
    {
        const float x = bounds.getX() + bounds.getWidth() * (float) (p.timestampMs - oldestMs) / timeSpanMs;
        const float clampedCents = juce::jlimit (-kCentsRange, kCentsRange, p.centsFromSa);
        const float y = bounds.getCentreY() - (clampedCents / kCentsRange) * (bounds.getHeight() * 0.5f);

        if (! started)
        {
            path.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            path.lineTo (x, y);
        }
    }

    g.setColour (juce::Colours::limegreen);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    g.setColour (juce::Colours::darkgrey);
    g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX(), bounds.getRight());
}
```

- [ ] **Step 4: Add to CMakeLists.txt**

```cmake
# Add src/ui/PitchGraphComponent.cpp and src/ui/PitchGraphPointBufferTests.cpp
# to riyaaz_tests' target_sources. This introduces riyaaz_tests' first
# dependency on juce_gui_basics - add it to the link line too:
target_sources(riyaaz_tests PRIVATE
    ...(existing entries)...
    src/ui/PitchGraphComponent.cpp
    src/ui/PitchGraphPointBufferTests.cpp
)
target_link_libraries(riyaaz_tests PRIVATE
    ...(existing entries)...
    juce::juce_gui_basics
)
```

- [ ] **Step 5: Build and run, verify all tests pass**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: exit code 0, 47 tests total (44 from Task 1 + 3 new). Linking `juce_gui_basics` into a console app may require no display/window server to actually be present at RUN time (component construction without `setVisible`/adding to desktop shouldn't need one) — if the test binary fails to launch or crashes only after this change, investigate whether `PitchGraphComponent`'s construction itself is the cause (it shouldn't need a real display just to exist and hold data) before assuming it's unfixable in a headless CI-style environment.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/ui/PitchGraphComponent.h src/ui/PitchGraphComponent.cpp src/ui/PitchGraphPointBufferTests.cpp
git commit -m "feat: add PitchGraphComponent with unit-tested point-buffer eviction logic"
```

---

### Task 3: MainComponent + JUCE app target

**Files:**
- Create: `src/app/MainComponent.h`
- Create: `src/app/MainComponent.cpp`
- Create: `src/app/Main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `CrepePitchEngine`, `PitchPipeline`, `PitchWorker`, `PitchGraphComponent` — all already built
- Produces: a runnable GUI executable (`riyaaz_app`). No new C++ types other than `MainComponent` and the `JUCEApplication` subclass in `Main.cpp` — nothing else in the codebase consumes these.

- [ ] **Step 1: Write `MainComponent.h`**

```cpp
// src/app/MainComponent.h
#pragma once
#include "../audio/pitchengine/CrepePitchEngine.h"
#include "../audio/pipeline/PitchPipeline.h"
#include "../audio/worker/PitchWorker.h"
#include "../ui/PitchGraphComponent.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

class MainComponent : public juce::AudioAppComponent, private PitchWorker::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void pitchWorkerUpdate (const PitchPipelineUpdate& update) override;

    CrepePitchEngine engine { juce::String ("models/crepe/small.onnx") };
    std::unique_ptr<PitchPipeline> pipeline;   // constructed in prepareToPlay(), once the real sample rate is known
    std::unique_ptr<PitchWorker> worker;       // ditto

    juce::Label statusLabel;
    PitchGraphComponent pitchGraph;

    PitchPipelineUpdate lastUpdate { PitchPipelinePhase::Calibrating };
};
```

- [ ] **Step 2: Write `MainComponent.cpp`**

```cpp
// src/app/MainComponent.cpp
#include "MainComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible (statusLabel);
    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setText ("Starting...", juce::dontSendNotification);

    addAndMakeVisible (pitchGraph);

    setSize (600, 400);

    // Mono input (mic), no audio output needed - the app only analyzes,
    // it doesn't play anything back yet (tanpura/metronome are later work).
    setAudioChannels (1, 0);
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

void MainComponent::prepareToPlay (int /*samplesPerBlockExpected*/, double sampleRate)
{
    auto prepareStatus = engine.prepare (sampleRate);
    if (prepareStatus != PitchEngineStatus::Ok)
    {
        juce::MessageManager::callAsync ([this]
        {
            statusLabel.setText ("Could not load the pitch model (models/crepe/small.onnx) - check it exists relative to the app's working directory.",
                                  juce::dontSendNotification);
        });
        return;
    }

    pipeline = std::make_unique<PitchPipeline> (engine, sampleRate);
    worker = std::make_unique<PitchWorker> (*pipeline, *this);
    worker->start();

    juce::MessageManager::callAsync ([this]
    {
        statusLabel.setText ("Calibrating - sing a steady, comfortable note for a few seconds...",
                              juce::dontSendNotification);
    });
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (worker == nullptr || bufferToFill.buffer == nullptr || bufferToFill.buffer->getNumChannels() == 0)
        return;

    const float* channelData = bufferToFill.buffer->getReadPointer (0, bufferToFill.startSample);
    worker->pushAudio (channelData, bufferToFill.numSamples);
}

void MainComponent::releaseResources()
{
    if (worker != nullptr)
        worker->stop();
    worker.reset();
    pipeline.reset();
}

void MainComponent::pitchWorkerUpdate (const PitchPipelineUpdate& update)
{
    // Called via PitchWorker's AsyncUpdater, so this already runs on the
    // message thread - safe to touch Components directly here.
    lastUpdate = update;

    if (update.phase == PitchPipelinePhase::Calibrating)
    {
        juce::String text;
        switch (update.calibrationStatus)
        {
            case CalibrationStatus::InProgress: text = "Calibrating..."; break;
            case CalibrationStatus::Success:    text = "Calibrated!"; break;
            case CalibrationStatus::Timeout:    text = "Didn't hear a steady note - try again."; break;
            case CalibrationStatus::Unstable:   text = "Pitch was too unstable - try holding a steadier note."; break;
        }
        statusLabel.setText (text, juce::dontSendNotification);
    }
    else if (update.swarLabel.has_value() && update.centsFromSa.has_value())
    {
        juce::String text = "Sa = " + juce::String (update.saHz, 1) + "Hz   "
                           + swarToString (update.swarLabel->swar) + " ("
                           + registerToString (update.swarLabel->octaveRegister) + ")   "
                           + juce::String (update.swarLabel->centsFromCenter, 1) + "c";
        statusLabel.setText (text, juce::dontSendNotification);

        pitchGraph.addPoint (update.timestampMs, *update.centsFromSa);
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    statusLabel.setBounds (area.removeFromTop (60));
    pitchGraph.setBounds (area);
}
```

Note: `PitchPipelineUpdate` does not yet have a `timestampMs` field as of this plan's starting point — `TODOS.md` already flags this as a next-plan design input ("PitchPipelineUpdate is under-specified... it drops PitchFrame::timestampMs"). This task's `MainComponent.cpp` above references `update.timestampMs`, which means this field must be added to `PitchPipelineUpdate` and populated in `PitchPipeline::handleLive()` as a prerequisite. **Do this as Step 2a below, before writing `MainComponent.cpp` as shown above** — do not work around its absence by inventing a different timestamp source in `MainComponent` itself, since `PitchPipeline` (not its callers) is the correct place to own this per the existing architecture.

- [ ] **Step 2a: Add `timestampMs` to `PitchPipelineUpdate` (prerequisite for Step 2)**

In `src/audio/pipeline/PitchPipeline.h`, add a field to `PitchPipelineUpdate`:

```cpp
struct PitchPipelineUpdate
{
    PitchPipelinePhase phase = PitchPipelinePhase::Calibrating;
    CalibrationStatus calibrationStatus = CalibrationStatus::InProgress;
    std::optional<SwarLabel> swarLabel;
    std::optional<float> centsFromSa;
    float saHz = 0.0f;
    uint64_t timestampMs = 0; // from PitchFrame::timestampMs during Live phase; 0 during Calibrating
};
```

In `src/audio/pipeline/PitchPipeline.cpp`'s `handleLive()`, populate it from the engine's frame:

```cpp
// After: PitchFrame frame = engine.processFrame (audioFrame, numSamples);
update.timestampMs = frame.timestampMs;
```

(add this line regardless of whether the frame is voiced or unvoiced - an unvoiced frame's timestamp is still meaningful for a caller tracking elapsed time, and costs nothing to propagate)

Add ONE test to `src/audio/pipeline/PitchPipelineTests.cpp` confirming a Live-phase update's `timestampMs` is populated and non-decreasing across consecutive confident frames (reuse the existing "Transitions to Live phase" test's setup, or write a small new one — your judgment on which is less duplicative, but the assertion must be concrete: e.g. `expect (update.timestampMs >= 0);` alone is too weak since `uint64_t` can't be negative — assert it matches the expected value given `CrepePitchEngine`'s known 64ms-per-window timestamp behavior, or at minimum assert two consecutive Live-phase updates have strictly increasing `timestampMs`).

Build and run the full suite before proceeding to Step 2 — this is its own small, independently-verifiable change. Expected: same test count as before this step (47, if a new test wasn't added; 48 if one was) with no regressions, then continue to Step 2's `MainComponent.cpp`.

- [ ] **Step 3: Write `Main.cpp`**

```cpp
// src/app/Main.cpp
#include "MainComponent.h"
#include <juce_gui_extra/juce_gui_extra.h>

class RiyaazApplication : public juce::JUCEApplication
{
public:
    RiyaazApplication() = default;

    const juce::String getApplicationName() override { return "Riyaaz"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                               juce::Desktop::getInstance().getDefaultLookAndFeel()
                                   .findColour (juce::ResizableWindow::backgroundColourId),
                               DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (RiyaazApplication)
```

- [ ] **Step 4: Add the `riyaaz_app` target to `CMakeLists.txt`**

```cmake
juce_add_gui_app(riyaaz_app PRODUCT_NAME "Riyaaz")
target_sources(riyaaz_app PRIVATE
    src/app/MainComponent.cpp
    src/app/Main.cpp
)
target_link_libraries(riyaaz_app PRIVATE
    riyaaz_pipeline
    riyaaz_pitchengine
    riyaaz_worker
    riyaaz_swarmap
    juce::juce_audio_utils
    juce::juce_gui_extra
)
riyaaz_copy_onnxruntime_dll(riyaaz_app)
```

- [ ] **Step 5: Build and confirm it compiles**

Run:
```
cmake --build build --target riyaaz_app
```
Expected: successful build producing `riyaaz_app_artefacts/Debug/riyaaz_app.exe` (or similar path — check the actual output path `juce_add_gui_app` produces, matching the pattern already established for `riyaaz_tests`). Fix any compile errors before proceeding — do not skip straight to the smoke-launch step on a broken build.

- [ ] **Step 6: Smoke-test that it launches without crashing**

This does NOT verify the app works correctly (calibration flow, mic capture, graph rendering) — only that it starts and stays running instead of crashing immediately. Run it in the background for a few seconds, then check it's still alive, then terminate it:

```
# Adjust the exact executable path to whatever Step 5 actually produced.
./build/riyaaz_app_artefacts/Debug/riyaaz_app.exe &
APP_PID=$!
sleep 4
if kill -0 $APP_PID 2>/dev/null; then
    echo "SMOKE TEST PASSED: app still running after 4s"
    kill $APP_PID
else
    echo "SMOKE TEST FAILED: app exited or crashed within 4s"
fi
```

If it crashes immediately, investigate before reporting DONE — common causes at this stage: the model file path (`models/crepe/small.onnx`) being relative to a working directory the launched process doesn't have (check what CWD the smoke-test command uses vs. what `MainComponent`'s `engine` constructor assumes), or a missing runtime DLL (onnxruntime.dll) not actually landing next to `riyaaz_app.exe` despite the `riyaaz_copy_onnxruntime_dll` call — verify the DLL is physically present next to the built exe before assuming the copy step worked.

- [ ] **Step 7: Run the full test suite once more to confirm nothing in Tasks 1-2 regressed**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe`
Expected: same test count and exit code 0 as at the end of Task 2 (plus Step 2a's addition) — this task added no new `riyaaz_tests` sources, only the new `riyaaz_app` target, so nothing here should have changed, but confirm rather than assume.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/app/MainComponent.h src/app/MainComponent.cpp src/app/Main.cpp src/audio/pipeline/PitchPipeline.h src/audio/pipeline/PitchPipeline.cpp src/audio/pipeline/PitchPipelineTests.cpp
git commit -m "feat: add MainComponent and riyaaz_app - the first runnable GUI application"
```

---

## Not covered by this plan (separate plans / your own hands-on testing)

- **You need to actually run `riyaaz_app` and try it** — sing into your mic, go through calibration, watch the live graph. This plan cannot verify any of that; it can only verify the app launches without crashing.
- Tanpura, metronome/taal engine, accuracy scoring, alankar module, packaging/distribution — unchanged from the milestone order in the original plan, and still v1.1 scope per the eng-review decisions.
- Refinements this plan deliberately does NOT attempt: a "Recalibrate" button (would need the `resetTracking()` method noted in `TODOS.md`), surfacing `engineStatus` in the UI (also noted in `TODOS.md` — right now, a broken ONNX model load just shows "Calibrating..." forever with no diagnostic), and the calibration block-size ceiling documented in `TODOS.md` (not a risk here since `MainComponent` feeds real small audio-callback blocks, not large ones — but worth remembering if that ever changes).
