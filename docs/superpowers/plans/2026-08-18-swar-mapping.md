# Swar Mapping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a `SwarMapper` that converts a stream of cents-from-Sa values into stable swar + octave-register labels, with hysteresis so pitch hovering near a swar boundary (inherent to meend/gamak ornamentation) doesn't flicker between adjacent labels.

**Architecture:** A single stateful class holds a "locked" swar center (a multiple of 100 cents). Each `update()` call only moves the lock when the input has crossed decisively past the boundary (geometric midpoint + half the hysteresis margin); small oscillation near a boundary is absorbed without changing the label. Large jumps (silence → new pitch, octave leap) snap directly to the new nearest swar in one step, not incrementally.

**Tech Stack:** C++17, JUCE 8.0.7 (`juce_core` only — no audio/GUI modules needed for this class), `juce::UnitTestRunner` for tests, CMake + vcpkg.

## Global Constraints

- C++17, matching the rest of the app (per plan section 7).
- Test framework is `juce::UnitTestRunner` — no Catch2/GoogleTest (Test Review decision 3A).
- Swar mapping must use hysteresis, not per-frame nearest quantization (Code Quality decision 2B).
- vcpkg manifest already exists at `D:\Journey\Code\riyaaz\vcpkg.json` (declares `juce`, `onnxruntime`). Toolchain file: `D:/vcpkg/scripts/buildsystems/vcpkg.cmake`, triplet `x64-windows`.
- **Operational note:** JUCE was still building via `vcpkg install` at the time this plan was written. Before Task 1's build steps, confirm the build finished: check `D:\Journey\Code\riyaaz\vcpkg-install.log` for a final success line, or run `Test-Path D:\Journey\Code\riyaaz\vcpkg_installed\x64-windows\share\juce\JUCEConfig.cmake` (PowerShell) / `test -f` (bash). If still running, wait for it — do not proceed with an incomplete install.
- This module has **zero dependency on ONNX Runtime, audio I/O, or the pitch engine** — it consumes a `float` cents value and returns a label. It can be fully implemented and tested independent of the CREPE/ONNX pipeline work.

---

## File Structure

- `CMakeLists.txt` — project root build config (created in Task 1, extended by later plans)
- `src/audio/swarmap/SwarMapper.h` — public interface: `Swar`, `OctaveRegister`, `SwarLabel`, `SwarMapper`, `swarToString()`, `registerToString()`
- `src/audio/swarmap/SwarMapper.cpp` — implementation
- `src/audio/swarmap/SwarMapperTests.cpp` — `juce::UnitTest` subclass, one test per behavior
- `src/TestMain.cpp` — console entry point that runs all registered `juce::UnitTest`s and returns a nonzero exit code on any failure

---

### Task 1: Project scaffolding + SwarMapper skeleton (locks to nearest swar, no hysteresis yet)

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/TestMain.cpp`
- Create: `src/audio/swarmap/SwarMapper.h`
- Create: `src/audio/swarmap/SwarMapper.cpp`
- Test: `src/audio/swarmap/SwarMapperTests.cpp`

**Interfaces:**
- Consumes: nothing (first task)
- Produces:
  - `enum class Swar { Sa, ReKomal, Re, GaKomal, Ga, Ma, MaTivra, Pa, DhaKomal, Dha, NiKomal, Ni };`
  - `enum class OctaveRegister { Mandra, Madhya, Taar, Other };`
  - `struct SwarLabel { Swar swar; OctaveRegister octaveRegister; int octaveIndex; float centsFromCenter; };`
  - `class SwarMapper { public: explicit SwarMapper (float hysteresisMarginCents = 15.0f); SwarLabel update (float centsFromSa); void reset(); ... };`

- [ ] **Step 1: Create the CMake project and vcpkg-integrated build**

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.24)
project(riyaaz VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(JUCE CONFIG REQUIRED)

add_library(riyaaz_swarmap STATIC
    src/audio/swarmap/SwarMapper.cpp
)
target_include_directories(riyaaz_swarmap PUBLIC src)
target_link_libraries(riyaaz_swarmap PUBLIC juce::juce_core)
target_compile_definitions(riyaaz_swarmap PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)

juce_add_console_app(riyaaz_tests PRODUCT_NAME "Riyaaz Tests")
target_sources(riyaaz_tests PRIVATE
    src/audio/swarmap/SwarMapperTests.cpp
    src/TestMain.cpp
)
target_link_libraries(riyaaz_tests PRIVATE riyaaz_swarmap juce::juce_core)
```

```cpp
// src/TestMain.cpp
#include <juce_core/juce_core.h>

int main (int, char**)
{
    juce::UnitTestRunner runner;
    runner.runAllTests();

    for (int i = 0; i < runner.getNumResults(); ++i)
        if (runner.getResult (i)->failures > 0)
            return 1;

    return 0;
}
```

- [ ] **Step 2: Write the failing test — first call locks to nearest swar**

```cpp
// src/audio/swarmap/SwarMapperTests.cpp
#include "SwarMapper.h"
#include <juce_core/juce_core.h>

class SwarMapperTests : public juce::UnitTest
{
public:
    SwarMapperTests() : juce::UnitTest ("SwarMapper", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("First call locks to nearest swar with correct deviation");
        {
            SwarMapper mapper (15.0f);
            auto label = mapper.update (5.0f); // close to Sa (0 cents)
            expectEquals ((int) label.swar, (int) Swar::Sa);
            expectEquals (label.octaveIndex, 0);
            expectWithinAbsoluteError (label.centsFromCenter, 5.0f, 0.01f);
        }
    }
};

static SwarMapperTests swarMapperTestsInstance;
```

- [ ] **Step 3: Configure and build, verify the test fails (SwarMapper doesn't exist yet)**

Run:
```
cmake -B build -S . -G Ninja -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
```
Expected: CMake configure fails or the build step fails with "SwarMapper.h: No such file or directory" — confirms the test file compiles against a header that doesn't exist yet.

- [ ] **Step 4: Write the minimal SwarMapper implementation**

```cpp
// src/audio/swarmap/SwarMapper.h
#pragma once
#include <juce_core/juce_core.h>

enum class Swar { Sa, ReKomal, Re, GaKomal, Ga, Ma, MaTivra, Pa, DhaKomal, Dha, NiKomal, Ni };
enum class OctaveRegister { Mandra, Madhya, Taar, Other };

struct SwarLabel
{
    Swar swar;
    OctaveRegister octaveRegister;
    int octaveIndex;
    float centsFromCenter;
};

class SwarMapper
{
public:
    explicit SwarMapper (float hysteresisMarginCents = 15.0f);

    SwarLabel update (float centsFromSa);
    void reset();

private:
    float hysteresisMargin;
    bool hasLockedCenter = false;
    int lockedCenterCents = 0;

    SwarLabel labelForLockedCenter (float centsFromSa) const;
};

juce::String swarToString (Swar swar);
juce::String registerToString (OctaveRegister reg);
```

```cpp
// src/audio/swarmap/SwarMapper.cpp
#include "SwarMapper.h"
#include <cmath>

namespace
{
    int floorDiv (int a, int b)
    {
        int q = a / b;
        int r = a % b;
        if (r != 0 && ((r < 0) != (b < 0)))
            --q;
        return q;
    }

    int nearestCenterCents (float cents)
    {
        return (int) std::lround (cents / 100.0f) * 100;
    }
}

SwarMapper::SwarMapper (float hysteresisMarginCents)
    : hysteresisMargin (hysteresisMarginCents)
{
}

SwarLabel SwarMapper::labelForLockedCenter (float centsFromSa) const
{
    const int semitoneTotal = lockedCenterCents / 100;
    const int octaveIndex = floorDiv (semitoneTotal, 12);
    const int semitoneIndex = semitoneTotal - octaveIndex * 12;

    OctaveRegister reg = OctaveRegister::Other;
    if (octaveIndex == -1) reg = OctaveRegister::Mandra;
    else if (octaveIndex == 0) reg = OctaveRegister::Madhya;
    else if (octaveIndex == 1) reg = OctaveRegister::Taar;

    return SwarLabel {
        (Swar) semitoneIndex,
        reg,
        octaveIndex,
        centsFromSa - (float) lockedCenterCents
    };
}

SwarLabel SwarMapper::update (float centsFromSa)
{
    if (! hasLockedCenter)
    {
        lockedCenterCents = nearestCenterCents (centsFromSa);
        hasLockedCenter = true;
    }

    return labelForLockedCenter (centsFromSa);
}

void SwarMapper::reset()
{
    hasLockedCenter = false;
    lockedCenterCents = 0;
}

juce::String swarToString (Swar swar)
{
    switch (swar)
    {
        case Swar::Sa:       return "S";
        case Swar::ReKomal:  return "r";
        case Swar::Re:       return "R";
        case Swar::GaKomal:  return "g";
        case Swar::Ga:       return "G";
        case Swar::Ma:       return "m";
        case Swar::MaTivra:  return "M'";
        case Swar::Pa:       return "P";
        case Swar::DhaKomal: return "d";
        case Swar::Dha:      return "D";
        case Swar::NiKomal:  return "n";
        case Swar::Ni:       return "N";
    }
    return "?";
}

juce::String registerToString (OctaveRegister reg)
{
    switch (reg)
    {
        case OctaveRegister::Mandra: return "mandra";
        case OctaveRegister::Madhya: return "madhya";
        case OctaveRegister::Taar:   return "taar";
        case OctaveRegister::Other:  return "other";
    }
    return "?";
}
```

- [ ] **Step 5: Build and run, verify the test passes**

Run:
```
cmake --build build --target riyaaz_tests
./build/riyaaz_tests.exe
```
Expected: exit code 0, "SwarMapper / First call locks to nearest swar with correct deviation" reported as passed.

- [ ] **Step 6: Commit**

```bash
git init
git add CMakeLists.txt vcpkg.json src/TestMain.cpp src/audio/swarmap/
git commit -m "feat: add SwarMapper skeleton with nearest-swar locking"
```

---

### Task 2: Hysteresis — no flicker near a boundary, deliberate crossing switches the label

**Files:**
- Modify: `src/audio/swarmap/SwarMapper.cpp`
- Test: `src/audio/swarmap/SwarMapperTests.cpp`

**Interfaces:**
- Consumes: `SwarMapper`, `SwarLabel`, `Swar` from Task 1 (unchanged signatures)
- Produces: no new public symbols — `update()`'s behavior gains hysteresis

- [ ] **Step 1: Write the failing tests**

```cpp
beginTest ("Small oscillation near a boundary does not flip the swar");
{
    SwarMapper mapper (15.0f); // threshold = 50 + 15/2 = 57.5 cents from center
    mapper.update (0.0f); // lock to Sa
    auto a = mapper.update (52.0f);
    auto b = mapper.update (48.0f);
    auto c = mapper.update (52.0f);
    expectEquals ((int) a.swar, (int) Swar::Sa);
    expectEquals ((int) b.swar, (int) Swar::Sa);
    expectEquals ((int) c.swar, (int) Swar::Sa);
}

beginTest ("Deliberate crossing past the hysteresis threshold switches swar");
{
    SwarMapper mapper (15.0f);
    mapper.update (0.0f); // lock to Sa
    auto label = mapper.update (60.0f); // beyond the 57.5 threshold
    expectEquals ((int) label.swar, (int) Swar::ReKomal);
    expectWithinAbsoluteError (label.centsFromCenter, -40.0f, 0.01f);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests.exe`
Expected: FAIL — Task 1's `update()` has no hysteresis, so `update(60.0f)` after locking to Sa currently re-locks every call and would report `ReKomal` even for the 52/48 oscillation case (both fail).

- [ ] **Step 3: Implement hysteresis in `update()`**

Replace the body of `SwarMapper::update` in `src/audio/swarmap/SwarMapper.cpp`:

```cpp
SwarLabel SwarMapper::update (float centsFromSa)
{
    if (! hasLockedCenter)
    {
        lockedCenterCents = nearestCenterCents (centsFromSa);
        hasLockedCenter = true;
    }
    else
    {
        const float distanceFromLocked = std::abs (centsFromSa - (float) lockedCenterCents);
        const float threshold = 50.0f + hysteresisMargin / 2.0f;

        if (distanceFromLocked > threshold)
            lockedCenterCents = nearestCenterCents (centsFromSa);
    }

    return labelForLockedCenter (centsFromSa);
}
```

- [ ] **Step 4: Run to verify all tests pass**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests.exe`
Expected: exit code 0, all 3 tests passing.

- [ ] **Step 5: Commit**

```bash
git add src/audio/swarmap/SwarMapper.cpp src/audio/swarmap/SwarMapperTests.cpp
git commit -m "feat: add hysteresis to SwarMapper boundary crossing"
```

---

### Task 3: Large jumps snap directly to the new nearest swar

**Files:**
- Test: `src/audio/swarmap/SwarMapperTests.cpp`

**Interfaces:**
- Consumes: `SwarMapper` from Tasks 1-2 (no interface changes)
- Produces: nothing new — this task is a regression-proofing test; Task 2's implementation already satisfies it, but it must be verified explicitly, not assumed

- [ ] **Step 1: Write the test**

```cpp
beginTest ("Large jump snaps directly to the new nearest swar, not stepwise");
{
    SwarMapper mapper (15.0f);
    mapper.update (0.0f); // lock to Sa
    auto label = mapper.update (645.0f); // far jump, e.g. silence then a different pitch
    expectEquals ((int) label.swar, (int) Swar::MaTivra);
    expectWithinAbsoluteError (label.centsFromCenter, 45.0f, 0.01f);
}
```

- [ ] **Step 2: Run to verify it already passes**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests.exe`
Expected: PASS — Task 2's `nearestCenterCents()` re-lock already handles this in one step (confirms no stepwise-hunting bug was introduced).

- [ ] **Step 3: Commit**

```bash
git add src/audio/swarmap/SwarMapperTests.cpp
git commit -m "test: verify large pitch jumps snap directly to nearest swar"
```

---

### Task 4: Octave register naming and `reset()`

**Files:**
- Test: `src/audio/swarmap/SwarMapperTests.cpp`

**Interfaces:**
- Consumes: `SwarMapper::reset()`, `OctaveRegister` from Task 1 (already defined, exercised for the first time here)
- Produces: nothing new

- [ ] **Step 1: Write the failing tests**

```cpp
beginTest ("Octave register naming for mandra/madhya/taar and numeric fallback");
{
    SwarMapper mapper (15.0f);
    auto madhya = mapper.update (0.0f);
    expect (madhya.octaveRegister == OctaveRegister::Madhya);

    mapper.reset();
    auto mandra = mapper.update (-150.0f);
    expect (mandra.octaveRegister == OctaveRegister::Mandra);

    mapper.reset();
    auto taar = mapper.update (1245.0f);
    expect (taar.octaveRegister == OctaveRegister::Taar);

    mapper.reset();
    auto farOut = mapper.update (2500.0f);
    expect (farOut.octaveRegister == OctaveRegister::Other);
}

beginTest ("reset() clears the locked center so the next update re-locks fresh, not treated as a jump");
{
    SwarMapper mapper (15.0f);
    mapper.update (0.0f); // lock to Sa (madhya)
    mapper.reset();
    auto label = mapper.update (1140.0f); // fresh lock, nearest to 1100 (Ni, madhya)
    expectEquals ((int) label.swar, (int) Swar::Ni);
    expect (label.octaveRegister == OctaveRegister::Madhya);
}
```

- [ ] **Step 2: Run to verify these already pass**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests.exe`
Expected: PASS — Task 1's `labelForLockedCenter()` and `reset()` already implement this; this task exists to lock the behavior down with explicit tests before other code starts depending on it.

- [ ] **Step 3: Commit**

```bash
git add src/audio/swarmap/SwarMapperTests.cpp
git commit -m "test: verify octave register naming and reset() behavior"
```

---

### Task 5: String helpers for UI display

**Files:**
- Test: `src/audio/swarmap/SwarMapperTests.cpp`

**Interfaces:**
- Consumes: `swarToString()`, `registerToString()` from Task 1
- Produces: verified string contract that the live-display UI (a later plan) will depend on — `swarToString(Swar::ReKomal) == "r"`, `swarToString(Swar::MaTivra) == "M'"`, `registerToString(OctaveRegister::Madhya) == "madhya"`

- [ ] **Step 1: Write the test**

```cpp
beginTest ("swarToString and registerToString produce the expected labels");
{
    expectEquals (swarToString (Swar::Sa), juce::String ("S"));
    expectEquals (swarToString (Swar::ReKomal), juce::String ("r"));
    expectEquals (swarToString (Swar::MaTivra), juce::String ("M'"));
    expectEquals (registerToString (OctaveRegister::Madhya), juce::String ("madhya"));
    expectEquals (registerToString (OctaveRegister::Other), juce::String ("other"));
}
```

- [ ] **Step 2: Run to verify it passes**

Run: `cmake --build build --target riyaaz_tests && ./build/riyaaz_tests.exe`
Expected: PASS — Task 1's `swarToString`/`registerToString` already implement this; locks the exact string contract for the UI task that will consume it.

- [ ] **Step 3: Commit**

```bash
git add src/audio/swarmap/SwarMapperTests.cpp
git commit -m "test: lock down swarToString/registerToString display contract"
```

---

## Not covered by this plan (separate plans)

- `PitchEngine` interface + `CrepePitchEngine` (ONNX Runtime, onnxcrepe weights, `prepare()`/`reset()`, continuity smoothing) — needs ONNX Runtime, still building.
- Tonic calibration (`TonicCalibrator`) — needs `PitchEngine` to exist first; also needs the timeout/instability handling flagged as a critical gap in the review.
- Real-time audio pipeline (`juce::AbstractFifo`, worker thread, `AsyncUpdater`, live UI graph) — needs both of the above plus JUCE's audio/GUI modules.
- Tanpura, metronome, packaging — later plans per the milestone order.
