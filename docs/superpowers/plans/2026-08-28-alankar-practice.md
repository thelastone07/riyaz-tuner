# Alankar Practice Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a guided Alankar practice mode — pick a traditional alankar pattern, and the app steps through it note-by-note (paced by the existing Metronome), showing the target note and live accuracy on the existing pitch graph, ending in a time-in-tune summary.

**Architecture:** Three pure-logic layers (`AlankarPattern` → a new `centsFromSaForSwar` utility → `AlankarPracticeEngine`), none of them audio-thread or JUCE-dependent, driven externally by `MainComponent` from its existing `pitchWorkerUpdate()` callback (pitch feed) and existing `Timer` (beat-driven step advancement, polling the Metronome's beat counter). A small extension to `PitchGraphComponent` draws the current target note as a highlighted band. No persistence anywhere in this plan.

**Tech Stack:** C++17, JUCE 8.0.7 (`juce_core`, `juce_gui_basics`), CMake + vcpkg, `juce::UnitTestRunner`.

**Spec:** `docs/superpowers/specs/2026-08-28-alankar-practice-design.md`

## Global Constraints

Copied verbatim from the spec — every task's requirements implicitly include these:

1. Descending sequences are derived by reversing the stored ascending sequence at construction time, never hand-transcribed a second time — verified during design that this reversal exactly reproduces the independently-documented descending form for all 5 patterns.
2. ±25 cents = "in tune" (`AlankarPracticeEngine::kInTuneToleranceCents`, kept **public** so callers displaying the target band use the same single value rather than a duplicated magic number).
3. One beat per note (`kBeatsPerStep = 1`, a private fixed constant) — effective speed is controlled entirely via the Metronome's existing BPM slider, not a second knob.
4. `AlankarPattern`, `centsFromSaForSwar`, and `AlankarPracticeEngine` are pure logic (no JUCE audio/Timer dependency) and are unit-tested exhaustively, not spot-checked. `PitchGraphComponent`'s paint-only extension and all `MainComponent` wiring are build-verified only, matching this codebase's established precedent for UI/top-level-wiring code.
5. This feature never touches the audio thread — no new `AudioSource`, no changes to `getNextAudioBlock()`/`prepareToPlay()`/`releaseResources()`.

**Note on the current state of `MainComponent`:** it currently contains temporary diagnostic instrumentation (a `private juce::Timer` base, a `timerCallback()` override, and a `diagnosticsLabel`) from an unrelated, still-open bug investigation. This plan's Task 5 extends the *existing* `timerCallback()` rather than adding a second `Timer` — do not remove the diagnostics code; it is not this plan's concern.

---

### Task 1: `AlankarPattern`

**Files:**
- Create: `src/practice/AlankarPattern.h`
- Create: `src/practice/AlankarPattern.cpp`
- Test: `src/practice/AlankarPatternTests.cpp`
- Modify: `CMakeLists.txt` (new `riyaaz_practice` library target after the `riyaaz_metronome` block, before `juce_add_console_app(riyaaz_tests ...)`; add the test file to `riyaaz_tests`' sources; add `riyaaz_practice` to `riyaaz_tests`' link libraries)

**Interfaces:**
- Consumes: `Swar` from the existing `src/audio/swarmap/SwarMapper.h`.
- Produces: `enum class AlankarPatternId { Alankar1, Alankar2, Alankar3, Alankar4, Alankar5 };`, `struct AlankarStep { Swar swar; int octaveOffset; };`, `class AlankarPattern { public: explicit AlankarPattern (AlankarPatternId id); juce::String name() const; const std::vector<AlankarStep>& fullSequence() const; };`

- [ ] **Step 1: Write the test file**

Create `src/practice/AlankarPatternTests.cpp`:

```cpp
#include "AlankarPattern.h"
#include <juce_core/juce_core.h>

namespace
{
    bool stepsEqual (const std::vector<AlankarStep>& actual, const std::vector<AlankarStep>& expected)
    {
        if (actual.size() != expected.size())
            return false;
        for (size_t i = 0; i < actual.size(); ++i)
            if (actual[i].swar != expected[i].swar || actual[i].octaveOffset != expected[i].octaveOffset)
                return false;
        return true;
    }
}

class AlankarPatternTests : public juce::UnitTest
{
public:
    AlankarPatternTests() : juce::UnitTest ("AlankarPattern", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("Alankar 1: full sequence (8 ascending + 8 descending) matches the verified table exactly");
        {
            AlankarPattern pattern (AlankarPatternId::Alankar1);
            expectEquals (pattern.name(), juce::String ("Alankar 1"));

            const std::vector<AlankarStep> expected = {
                { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 },
                { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 },
                { Swar::Sa, 1 }, { Swar::Ni, 0 }, { Swar::Dha, 0 }, { Swar::Pa, 0 },
                { Swar::Ma, 0 }, { Swar::Ga, 0 }, { Swar::Re, 0 }, { Swar::Sa, 0 }
            };

            expect (stepsEqual (pattern.fullSequence(), expected));
        }

        beginTest ("Alankar 2: full sequence (14 ascending + 14 descending) matches the verified table exactly");
        {
            AlankarPattern pattern (AlankarPatternId::Alankar2);
            expectEquals (pattern.name(), juce::String ("Alankar 2"));

            const std::vector<AlankarStep> expected = {
                { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 },
                { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                { Swar::Ni, 0 }, { Swar::Sa, 1 },
                { Swar::Sa, 1 }, { Swar::Ni, 0 }, { Swar::Ni, 0 }, { Swar::Dha, 0 },
                { Swar::Dha, 0 }, { Swar::Pa, 0 }, { Swar::Pa, 0 }, { Swar::Ma, 0 },
                { Swar::Ma, 0 }, { Swar::Ga, 0 }, { Swar::Ga, 0 }, { Swar::Re, 0 },
                { Swar::Re, 0 }, { Swar::Sa, 0 }
            };

            expect (stepsEqual (pattern.fullSequence(), expected));
        }

        beginTest ("Alankar 3: full sequence (18 ascending + 18 descending) matches the verified table exactly");
        {
            AlankarPattern pattern (AlankarPatternId::Alankar3);
            expectEquals (pattern.name(), juce::String ("Alankar 3"));

            const std::vector<AlankarStep> expected = {
                { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 },
                { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 },
                { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 },
                { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 },
                { Swar::Sa, 1 }, { Swar::Ni, 0 }, { Swar::Dha, 0 },
                { Swar::Ni, 0 }, { Swar::Dha, 0 }, { Swar::Pa, 0 },
                { Swar::Dha, 0 }, { Swar::Pa, 0 }, { Swar::Ma, 0 },
                { Swar::Pa, 0 }, { Swar::Ma, 0 }, { Swar::Ga, 0 },
                { Swar::Ma, 0 }, { Swar::Ga, 0 }, { Swar::Re, 0 },
                { Swar::Ga, 0 }, { Swar::Re, 0 }, { Swar::Sa, 0 }
            };

            expect (stepsEqual (pattern.fullSequence(), expected));
        }

        beginTest ("Alankar 4: full sequence (20 ascending + 20 descending) matches the verified table exactly");
        {
            AlankarPattern pattern (AlankarPatternId::Alankar4);
            expectEquals (pattern.name(), juce::String ("Alankar 4"));

            const std::vector<AlankarStep> expected = {
                { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 },
                { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 },
                { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 },
                { Swar::Sa, 1 }, { Swar::Ni, 0 }, { Swar::Dha, 0 }, { Swar::Pa, 0 },
                { Swar::Ni, 0 }, { Swar::Dha, 0 }, { Swar::Pa, 0 }, { Swar::Ma, 0 },
                { Swar::Dha, 0 }, { Swar::Pa, 0 }, { Swar::Ma, 0 }, { Swar::Ga, 0 },
                { Swar::Pa, 0 }, { Swar::Ma, 0 }, { Swar::Ga, 0 }, { Swar::Re, 0 },
                { Swar::Ma, 0 }, { Swar::Ga, 0 }, { Swar::Re, 0 }, { Swar::Sa, 0 }
            };

            expect (stepsEqual (pattern.fullSequence(), expected));
        }

        beginTest ("Alankar 5: full sequence (20 ascending + 20 descending) matches the verified table exactly");
        {
            AlankarPattern pattern (AlankarPatternId::Alankar5);
            expectEquals (pattern.name(), juce::String ("Alankar 5"));

            const std::vector<AlankarStep> expected = {
                { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 },
                { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 },
                { Swar::Sa, 1 }, { Swar::Ni, 0 }, { Swar::Dha, 0 }, { Swar::Pa, 0 }, { Swar::Ma, 0 },
                { Swar::Ni, 0 }, { Swar::Dha, 0 }, { Swar::Pa, 0 }, { Swar::Ma, 0 }, { Swar::Ga, 0 },
                { Swar::Dha, 0 }, { Swar::Pa, 0 }, { Swar::Ma, 0 }, { Swar::Ga, 0 }, { Swar::Re, 0 },
                { Swar::Pa, 0 }, { Swar::Ma, 0 }, { Swar::Ga, 0 }, { Swar::Re, 0 }, { Swar::Sa, 0 }
            };

            expect (stepsEqual (pattern.fullSequence(), expected));
        }
    }
};

static AlankarPatternTests alankarPatternTestsInstance;
```

- [ ] **Step 2: Write `AlankarPattern.h`**

```cpp
// src/practice/AlankarPattern.h
#pragma once
#include "../audio/swarmap/SwarMapper.h"
#include <juce_core/juce_core.h>
#include <vector>

// One of a small, fixed library of traditional Hindustani alankar practice
// patterns. Only the ascending half is stored per pattern - the descending
// half is derived by reversal at construction time, since it is always the
// exact reverse of ascending for these patterns (verified during design
// against two independent sources for all 5).
enum class AlankarPatternId { Alankar1, Alankar2, Alankar3, Alankar4, Alankar5 };

struct AlankarStep
{
    Swar swar;
    int octaveOffset; // 0 = Madhya (same octave as the calibrated Sa), +1 = Taar, -1 = Mandra
};

class AlankarPattern
{
public:
    explicit AlankarPattern (AlankarPatternId idIn);

    juce::String name() const;
    const std::vector<AlankarStep>& fullSequence() const;

private:
    AlankarPatternId id;
    std::vector<AlankarStep> steps;
};
```

- [ ] **Step 3: Write `AlankarPattern.cpp`**

```cpp
// src/practice/AlankarPattern.cpp
#include "AlankarPattern.h"

namespace
{
    std::vector<AlankarStep> ascendingFor (AlankarPatternId id)
    {
        switch (id)
        {
            case AlankarPatternId::Alankar1:
                return {
                    { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 },
                    { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 }
                };

            case AlankarPatternId::Alankar2:
                return {
                    { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 },
                    { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                    { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                    { Swar::Ni, 0 }, { Swar::Sa, 1 }
                };

            case AlankarPatternId::Alankar3:
                return {
                    { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 },
                    { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 },
                    { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                    { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 },
                    { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                    { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 }
                };

            case AlankarPatternId::Alankar4:
                return {
                    { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 },
                    { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                    { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 },
                    { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                    { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 }
                };

            case AlankarPatternId::Alankar5:
                return {
                    { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                    { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 },
                    { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                    { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 }
                };
        }

        jassertfalse; // unreachable - every AlankarPatternId enumerator is handled above
        return {};
    }

    juce::String nameFor (AlankarPatternId id)
    {
        switch (id)
        {
            case AlankarPatternId::Alankar1: return "Alankar 1";
            case AlankarPatternId::Alankar2: return "Alankar 2";
            case AlankarPatternId::Alankar3: return "Alankar 3";
            case AlankarPatternId::Alankar4: return "Alankar 4";
            case AlankarPatternId::Alankar5: return "Alankar 5";
        }

        jassertfalse; // unreachable - every AlankarPatternId enumerator is handled above
        return "Alankar";
    }
}

AlankarPattern::AlankarPattern (AlankarPatternId idIn) : id (idIn)
{
    auto ascending = ascendingFor (id);
    steps = ascending;

    // Descending is the exact reverse of ascending, appended directly. The
    // peak note (the topmost step of ascending) is genuinely held/
    // re-articulated twice in a row as a result - once as the last
    // ascending note, once as the first descending note - matching how
    // these patterns are actually notated and sung, not de-duplicated.
    for (auto it = ascending.rbegin(); it != ascending.rend(); ++it)
        steps.push_back (*it);
}

juce::String AlankarPattern::name() const
{
    return nameFor (id);
}

const std::vector<AlankarStep>& AlankarPattern::fullSequence() const
{
    return steps;
}
```

- [ ] **Step 4: Add the CMakeLists.txt changes**

Insert after the `riyaaz_metronome` block, before `juce_add_console_app(riyaaz_tests ...)`:

```cmake
add_library(riyaaz_practice STATIC
    src/practice/AlankarPattern.cpp
)
target_include_directories(riyaaz_practice PUBLIC src)
target_link_libraries(riyaaz_practice PUBLIC juce::juce_core riyaaz_swarmap)
target_compile_definitions(riyaaz_practice PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)
```

Modify `target_sources(riyaaz_tests PRIVATE ...)`: add `src/practice/AlankarPatternTests.cpp` as a new line.

Modify `target_link_libraries(riyaaz_tests PRIVATE ...)`: add `riyaaz_practice` as a new line.

- [ ] **Step 5: Build and run**

```bash
cmake --build build --target riyaaz_tests
cd d:/Journey/Code/riyaaz
./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe
```

Expected: PASS, all 5 new `AlankarPattern` tests green, exit code 0. Note the total test count before and after (it was 121 at the end of the most recent prior work on this codebase).

- [ ] **Step 6: Commit**

```bash
git add src/practice/AlankarPattern.h src/practice/AlankarPattern.cpp src/practice/AlankarPatternTests.cpp CMakeLists.txt
git commit -m "feat: add AlankarPattern (5-pattern library, descending derived by reversal)"
```

---

### Task 2: `centsFromSaForSwar`

**Files:**
- Modify: `src/audio/swarmap/SwarMapper.h`
- Modify: `src/audio/swarmap/SwarMapper.cpp`
- Test: `src/audio/swarmap/SwarMapperTests.cpp` (append to the existing file)

**Interfaces:**
- Consumes: `Swar` (already defined in `SwarMapper.h`).
- Produces: `float centsFromSaForSwar (Swar swar, int octaveOffset);`

- [ ] **Step 1: Read the existing files first**

Read `src/audio/swarmap/SwarMapper.h`, `src/audio/swarmap/SwarMapper.cpp`, and `src/audio/swarmap/SwarMapperTests.cpp` in full before editing — this task appends to existing files, and the exact insertion points below assume you've seen the current content (the free functions `swarToString`/`registerToString` are declared at the end of `SwarMapper.h` and defined at the end of `SwarMapper.cpp`; `SwarMapperTests.cpp` is a `juce::UnitTest` subclass with a `runTest()` you'll add one more `beginTest` block to).

- [ ] **Step 2: Write the failing test**

Append this `beginTest` block inside `SwarMapperTests.cpp`'s existing `runTest()` method (add it as a new block, anywhere among the existing ones — e.g. right before the closing brace of `runTest()`):

```cpp
        beginTest ("centsFromSaForSwar() gives the standard 12-EDO cents value for every swar, plus octaveOffset*1200");
        {
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Sa, 0), 0.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::ReKomal, 0), 100.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Re, 0), 200.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::GaKomal, 0), 300.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Ga, 0), 400.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Ma, 0), 500.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::MaTivra, 0), 600.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Pa, 0), 700.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::DhaKomal, 0), 800.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Dha, 0), 900.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::NiKomal, 0), 1000.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Ni, 0), 1100.0f, 0.0001f);

            // Non-zero octaveOffset: Sa one octave up (Taar) is 1200 cents;
            // Ga one octave down (Mandra) is 400 - 1200 = -800 cents.
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Sa, 1), 1200.0f, 0.0001f);
            expectWithinAbsoluteError (centsFromSaForSwar (Swar::Ga, -1), -800.0f, 0.0001f);
        }
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build build --target riyaaz_tests
```

Expected: FAILS to compile (`centsFromSaForSwar` is not declared yet).

- [ ] **Step 4: Add the declaration to `SwarMapper.h`**

Add this line immediately after the existing `juce::String registerToString (OctaveRegister reg);` declaration at the end of the file:

```cpp
float centsFromSaForSwar (Swar swar, int octaveOffset);
```

- [ ] **Step 5: Add the implementation to `SwarMapper.cpp`**

Add this function at the end of the file, after the existing `registerToString` definition:

```cpp
float centsFromSaForSwar (Swar swar, int octaveOffset)
{
    float baseCents = 0.0f;
    switch (swar)
    {
        case Swar::Sa:       baseCents = 0.0f;    break;
        case Swar::ReKomal:  baseCents = 100.0f;  break;
        case Swar::Re:       baseCents = 200.0f;  break;
        case Swar::GaKomal:  baseCents = 300.0f;  break;
        case Swar::Ga:       baseCents = 400.0f;  break;
        case Swar::Ma:       baseCents = 500.0f;  break;
        case Swar::MaTivra:  baseCents = 600.0f;  break;
        case Swar::Pa:       baseCents = 700.0f;  break;
        case Swar::DhaKomal: baseCents = 800.0f;  break;
        case Swar::Dha:      baseCents = 900.0f;  break;
        case Swar::NiKomal:  baseCents = 1000.0f; break;
        case Swar::Ni:       baseCents = 1100.0f; break;
    }

    return baseCents + (float) octaveOffset * 1200.0f;
}
```

- [ ] **Step 6: Run test to verify it passes**

```bash
cmake --build build --target riyaaz_tests
cd d:/Journey/Code/riyaaz
./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe
```

Expected: PASS, the new `centsFromSaForSwar` test green, exit code 0. No CMakeLists.txt changes are needed for this task (`SwarMapper.cpp`/`.h` and `SwarMapperTests.cpp` are already part of `riyaaz_swarmap`/`riyaaz_tests`).

- [ ] **Step 7: Commit**

```bash
git add src/audio/swarmap/SwarMapper.h src/audio/swarmap/SwarMapper.cpp src/audio/swarmap/SwarMapperTests.cpp
git commit -m "feat: add centsFromSaForSwar (forward swar-to-cents mapping for Alankar practice)"
```

---

### Task 3: `AlankarPracticeEngine`

**Files:**
- Create: `src/practice/AlankarPracticeEngine.h`
- Create: `src/practice/AlankarPracticeEngine.cpp`
- Test: `src/practice/AlankarPracticeEngineTests.cpp`
- Modify: `CMakeLists.txt` (add `AlankarPracticeEngine.cpp` to `riyaaz_practice`'s sources; add the test file to `riyaaz_tests`' sources)

**Interfaces:**
- Consumes: `AlankarPattern`/`AlankarPatternId`/`AlankarStep` (Task 1), `centsFromSaForSwar` (Task 2).
- Produces: `struct AlankarStepResult { Swar swar; int octaveOffset; int framesInTune; int framesTotal; };`, `struct AlankarSummary { std::vector<AlankarStepResult> perStep; float overallTimeInTunePercent; std::vector<std::pair<Swar, float>> perSwarTimeInTunePercent; };`, `class AlankarPracticeEngine { public: static constexpr float kInTuneToleranceCents = 25.0f; explicit AlankarPracticeEngine (AlankarPatternId patternId); void onBeatElapsed(); void onPitchReading (float centsFromSa); bool isFinished() const; int currentStepIndex() const; int totalSteps() const; float currentStepTargetCents() const; AlankarSummary getSummary() const; };` — `kInTuneToleranceCents` is deliberately **public**: `MainComponent` (Task 5) needs the same tolerance value to draw the target band, and must not duplicate the magic number.

- [ ] **Step 1: Write the test file**

Create `src/practice/AlankarPracticeEngineTests.cpp`:

```cpp
#include "AlankarPracticeEngine.h"
#include <juce_core/juce_core.h>
#include <algorithm>

class AlankarPracticeEngineTests : public juce::UnitTest
{
public:
    AlankarPracticeEngineTests() : juce::UnitTest ("AlankarPracticeEngine", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("A fresh engine starts at step 0, not finished");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1); // 16 steps total
            expect (! engine.isFinished());
            expectEquals (engine.currentStepIndex(), 0);
            expectEquals (engine.totalSteps(), 16);
        }

        beginTest ("onBeatElapsed() advances exactly one step per call (kBeatsPerStep == 1), and isFinished() becomes true at exactly the pattern's length");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1); // 16 steps

            for (int i = 0; i < 15; ++i)
            {
                expect (! engine.isFinished(), "should not be finished before all 16 steps have had their beat");
                engine.onBeatElapsed();
            }

            expect (! engine.isFinished(), "15 beats have elapsed - the 16th (last) step hasn't been advanced past yet");
            expectEquals (engine.currentStepIndex(), 15);

            engine.onBeatElapsed(); // the 16th beat
            expect (engine.isFinished());
        }

        beginTest ("onPitchReading() within tolerance counts as in-tune; outside tolerance does not");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1);
            // Step 0 is Sa (0 cents). Within +-25 cents = in tune.
            engine.onPitchReading (10.0f);  // in tune
            engine.onPitchReading (-20.0f); // in tune
            engine.onPitchReading (30.0f);  // NOT in tune (outside 25)
            engine.onPitchReading (0.0f);   // in tune

            const auto summary = engine.getSummary();
            expectEquals (summary.perStep[0].framesTotal, 4);
            expectEquals (summary.perStep[0].framesInTune, 3);
        }

        beginTest ("A step that never receives a pitch reading reports 0% (framesTotal == 0), not a crash");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1);
            engine.onBeatElapsed(); // advance past step 0 without ever calling onPitchReading() for it

            const auto summary = engine.getSummary();
            expectEquals (summary.perStep[0].framesTotal, 0);
            expectEquals (summary.perStep[0].framesInTune, 0);
        }

        beginTest ("overallTimeInTunePercent aggregates correctly across steps, and is 0 with no frames at all");
        {
            AlankarPracticeEngine freshEngine (AlankarPatternId::Alankar1);
            expectWithinAbsoluteError (freshEngine.getSummary().overallTimeInTunePercent, 0.0f, 0.0001f);

            AlankarPracticeEngine engine (AlankarPatternId::Alankar1);
            engine.onPitchReading (0.0f);   // step 0 (Sa, 0 cents) - in tune
            engine.onPitchReading (0.0f);   // step 0 - in tune
            engine.onBeatElapsed();          // advance to step 1 (Re, 200 cents)
            engine.onPitchReading (500.0f); // step 1 - NOT in tune (way off)

            const auto summary = engine.getSummary();
            // 2 in-tune out of 3 total frames = 66.67%
            expectWithinAbsoluteError (summary.overallTimeInTunePercent, 66.6667f, 0.01f);
        }

        beginTest ("perSwarTimeInTunePercent aggregates across steps that repeat the same swar (different positions)");
        {
            // Alankar 3's full sequence starts: Sa, Re, Ga, Re, Ga, Ma, ... -
            // "Re" appears at both step index 1 and step index 3. Feed
            // different accuracy to each occurrence and confirm the per-swar
            // total combines both.
            AlankarPracticeEngine engine (AlankarPatternId::Alankar3);

            engine.onPitchReading (0.0f);    // step 0 = Sa (0c) - in tune
            engine.onBeatElapsed();           // -> step 1 = Re (200c)
            engine.onPitchReading (200.0f);  // step 1 = Re - in tune
            engine.onBeatElapsed();           // -> step 2 = Ga (400c)
            engine.onPitchReading (400.0f);  // step 2 = Ga - in tune
            engine.onBeatElapsed();           // -> step 3 = Re (200c)
            engine.onPitchReading (900.0f);  // step 3 = Re - NOT in tune (way off)

            const auto summary = engine.getSummary();
            const auto reEntry = std::find_if (summary.perSwarTimeInTunePercent.begin(),
                                               summary.perSwarTimeInTunePercent.end(),
                                               [] (const auto& p) { return p.first == Swar::Re; });
            expect (reEntry != summary.perSwarTimeInTunePercent.end());
            // Re: 1 in-tune out of 2 total frames (steps 1 and 3 combined) = 50%
            expectWithinAbsoluteError (reEntry->second, 50.0f, 0.01f);
        }

        beginTest ("currentStepTargetCents() matches centsFromSaForSwar() for the current step");
        {
            AlankarPracticeEngine engine (AlankarPatternId::Alankar1); // step 0 = Sa, 0 cents
            expectWithinAbsoluteError (engine.currentStepTargetCents(), 0.0f, 0.0001f);

            for (int i = 0; i < 6; ++i)
                engine.onBeatElapsed(); // Alankar 1: Sa Re Ga Ma Pa Dha Ni Sa'... - advance to step 6 = Ni

            expectEquals (engine.currentStepIndex(), 6);
            expectWithinAbsoluteError (engine.currentStepTargetCents(), 1100.0f, 0.0001f);
        }
    }
};

static AlankarPracticeEngineTests alankarPracticeEngineTestsInstance;
```

- [ ] **Step 2: Write `AlankarPracticeEngine.h`**

```cpp
// src/practice/AlankarPracticeEngine.h
#pragma once
#include "AlankarPattern.h"
#include <utility>
#include <vector>

struct AlankarStepResult
{
    Swar swar;
    int octaveOffset;
    int framesInTune;
    int framesTotal; // 0 if no confident pitch reading ever arrived during this step's window - happens at very high BPM relative to the pitch engine's ~64ms hop; reported as 0% for that step, not an error
};

struct AlankarSummary
{
    std::vector<AlankarStepResult> perStep;
    float overallTimeInTunePercent;                                // sum(framesInTune) / sum(framesTotal) across all steps, 0 if no frames at all
    std::vector<std::pair<Swar, float>> perSwarTimeInTunePercent;   // aggregated across all steps using that swar (any octave), sorted worst-to-best
};

// Pure logic, externally driven - no audio/JUCE-Timer dependency. The
// caller (MainComponent) is responsible for calling onBeatElapsed() once
// per real metronome beat boundary, and onPitchReading() for every
// confident pitch frame while practice is active.
class AlankarPracticeEngine
{
public:
    // Exposed so callers (e.g. MainComponent's target-band display) use the
    // exact same tolerance value this class scores against, not a
    // duplicated magic number.
    static constexpr float kInTuneToleranceCents = 25.0f;

    explicit AlankarPracticeEngine (AlankarPatternId patternId);

    void onBeatElapsed();
    void onPitchReading (float centsFromSa);

    bool isFinished() const;
    int currentStepIndex() const;  // valid until isFinished()
    int totalSteps() const;
    float currentStepTargetCents() const; // valid until isFinished()

    AlankarSummary getSummary() const; // valid at any point, including mid-practice for a live partial readout

private:
    static constexpr int kBeatsPerStep = 1;

    AlankarPattern pattern;
    int stepIndex = 0;
    int beatsIntoCurrentStep = 0;
    std::vector<AlankarStepResult> stepResults;
};
```

- [ ] **Step 3: Write `AlankarPracticeEngine.cpp`**

```cpp
// src/practice/AlankarPracticeEngine.cpp
#include "AlankarPracticeEngine.h"
#include "../audio/swarmap/SwarMapper.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <map>

AlankarPracticeEngine::AlankarPracticeEngine (AlankarPatternId patternId)
    : pattern (patternId)
{
    stepResults.reserve (pattern.fullSequence().size());
    for (const auto& step : pattern.fullSequence())
        stepResults.push_back ({ step.swar, step.octaveOffset, 0, 0 });
}

void AlankarPracticeEngine::onBeatElapsed()
{
    if (isFinished())
        return;

    ++beatsIntoCurrentStep;
    if (beatsIntoCurrentStep >= kBeatsPerStep)
    {
        beatsIntoCurrentStep = 0;
        ++stepIndex;
    }
}

void AlankarPracticeEngine::onPitchReading (float centsFromSa)
{
    if (isFinished())
        return;

    const auto& step = pattern.fullSequence()[(size_t) stepIndex];
    const float targetCents = centsFromSaForSwar (step.swar, step.octaveOffset);

    auto& result = stepResults[(size_t) stepIndex];
    ++result.framesTotal;
    if (std::abs (centsFromSa - targetCents) <= kInTuneToleranceCents)
        ++result.framesInTune;
}

bool AlankarPracticeEngine::isFinished() const
{
    return stepIndex >= (int) pattern.fullSequence().size();
}

int AlankarPracticeEngine::currentStepIndex() const
{
    return stepIndex;
}

int AlankarPracticeEngine::totalSteps() const
{
    return (int) pattern.fullSequence().size();
}

float AlankarPracticeEngine::currentStepTargetCents() const
{
    jassert (! isFinished()); // contract: caller must check isFinished() first - see header doc comment
    const auto& step = pattern.fullSequence()[(size_t) stepIndex];
    return centsFromSaForSwar (step.swar, step.octaveOffset);
}

AlankarSummary AlankarPracticeEngine::getSummary() const
{
    AlankarSummary summary;
    summary.perStep = stepResults;

    int totalInTune = 0;
    int totalFrames = 0;
    std::map<Swar, std::pair<int, int>> perSwarTotals; // swar -> (framesInTune, framesTotal)

    for (const auto& result : stepResults)
    {
        totalInTune += result.framesInTune;
        totalFrames += result.framesTotal;

        auto& swarTotals = perSwarTotals[result.swar];
        swarTotals.first += result.framesInTune;
        swarTotals.second += result.framesTotal;
    }

    summary.overallTimeInTunePercent = totalFrames > 0
        ? (100.0f * (float) totalInTune / (float) totalFrames)
        : 0.0f;

    for (const auto& entry : perSwarTotals)
    {
        const float percent = entry.second.second > 0
            ? (100.0f * (float) entry.second.first / (float) entry.second.second)
            : 0.0f;
        summary.perSwarTimeInTunePercent.push_back ({ entry.first, percent });
    }

    std::sort (summary.perSwarTimeInTunePercent.begin(), summary.perSwarTimeInTunePercent.end(),
               [] (const auto& a, const auto& b) { return a.second < b.second; }); // worst-to-best

    return summary;
}
```

- [ ] **Step 4: Add the CMakeLists.txt changes**

Modify the `riyaaz_practice` target's sources (created in Task 1): add `src/practice/AlankarPracticeEngine.cpp` as a new line.

Modify `target_sources(riyaaz_tests PRIVATE ...)`: add `src/practice/AlankarPracticeEngineTests.cpp` as a new line.

- [ ] **Step 5: Build and run**

```bash
cmake --build build --target riyaaz_tests
cd d:/Journey/Code/riyaaz
./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe
```

Expected: PASS, all 7 new `AlankarPracticeEngine` tests green, exit code 0.

- [ ] **Step 6: Commit**

```bash
git add src/practice/AlankarPracticeEngine.h src/practice/AlankarPracticeEngine.cpp src/practice/AlankarPracticeEngineTests.cpp CMakeLists.txt
git commit -m "feat: add AlankarPracticeEngine (beat-driven step advancement + time-in-tune scoring)"
```

---

### Task 4: `PitchGraphComponent` target-band extension

**Files:**
- Modify: `src/ui/PitchGraphComponent.h`
- Modify: `src/ui/PitchGraphComponent.cpp`

No dedicated test file — this is a paint-only extension to an existing, already-not-unit-tested `Component`, matching this codebase's established precedent. Verification is a successful build of `riyaaz_tests` and `riyaaz_app` (both already compile `PitchGraphComponent.cpp`).

**Interfaces:**
- Produces: `void PitchGraphComponent::setTargetBand (std::optional<std::pair<float, float>> centsRange);` — `centsRange` is `{low, high}` in cents-from-Sa (low < high always); `std::nullopt` clears the band.

- [ ] **Step 1: Read the existing files first**

Read `src/ui/PitchGraphComponent.h` and `src/ui/PitchGraphComponent.cpp` in full — this task inserts into the existing `paint()` method at a specific point (after the Sa line is drawn, before the early `return` for fewer than 2 points), which you need to see in context before editing.

- [ ] **Step 2: Modify `PitchGraphComponent.h`**

Add two includes after the existing ones (`#include <cstdint>` / `#include <deque>`):

```cpp
#include <optional>
#include <utility>
```

Add this public method to the `PitchGraphComponent` class, after the existing `void clear();` declaration:

```cpp
    // Highlights a cents-from-Sa range as a band behind the trace - used by
    // Alankar practice mode to show the current target note's tolerance
    // range. nullopt clears it (no band drawn). centsRange is {low, high}.
    void setTargetBand (std::optional<std::pair<float, float>> centsRange);
```

Add this private member after the existing `PitchGraphPointBuffer buffer;`:

```cpp
    std::optional<std::pair<float, float>> targetBand;
```

- [ ] **Step 3: Modify `PitchGraphComponent.cpp`**

Add this method definition, e.g. right after the existing `void PitchGraphComponent::clear()` definition:

```cpp
void PitchGraphComponent::setTargetBand (std::optional<std::pair<float, float>> centsRange)
{
    targetBand = centsRange;
    repaint();
}
```

In `paint()`, insert the following block immediately after the existing Sa-line drawing (`g.setColour (juce::Colours::darkgrey); g.drawHorizontalLine (...)`) and before the line `const auto& points = buffer.getPoints();`:

```cpp
    // Alankar practice mode's live target-note band, drawn after the
    // gridlines/Sa line but before the trace, so the trace is always
    // visible on top of it. Drawn even if there aren't 2 points yet (i.e.
    // before the function's early return below), so the target is visible
    // from the very start of a step.
    if (targetBand.has_value())
    {
        const float bandLowCents = juce::jlimit (-kCentsRange, kCentsRange, targetBand->first);
        const float bandHighCents = juce::jlimit (-kCentsRange, kCentsRange, targetBand->second);
        const float yHigh = centsToY (bandHighCents); // higher cents -> smaller Y (visually higher)
        const float yLow = centsToY (bandLowCents);

        g.setColour (juce::Colours::yellow.withAlpha (0.15f));
        g.fillRect (bounds.getX(), yHigh, bounds.getWidth(), yLow - yHigh);
    }
```

- [ ] **Step 4: Build to confirm it compiles**

```bash
cmake --build build --target riyaaz_tests
cd d:/Journey/Code/riyaaz
./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe
cmake --build build --target riyaaz_app
```

Expected: builds cleanly, test count/pass status unchanged from the end of Task 3 (this task adds no tests), `riyaaz_app` builds and links.

- [ ] **Step 5: Commit**

```bash
git add src/ui/PitchGraphComponent.h src/ui/PitchGraphComponent.cpp
git commit -m "feat: add target-band overlay to PitchGraphComponent for Alankar practice"
```

---

### Task 5: Wire Alankar practice into `MainComponent`

**Files:**
- Modify: `src/app/MainComponent.h`
- Modify: `src/app/MainComponent.cpp`
- Modify: `CMakeLists.txt` (add `riyaaz_practice` to `riyaaz_app`'s link libraries — this is the first task where `riyaaz_app` links it)

**Interfaces:**
- Consumes: `AlankarPattern`/`AlankarPatternId` (Task 1), `AlankarPracticeEngine`/`AlankarStepResult`/`AlankarSummary` (Task 3), `PitchGraphComponent::setTargetBand` (Task 4), the existing `MetronomeAudioSource::getTotalBeatsElapsed()`/`setTaal()`/`setEnabled()` and `TaalType::PlainClick`.
- Produces: nothing new for later tasks — this is the final integration task for this plan.

No test file: matches the established precedent that `MainComponent` is build-verified and manually verified, not unit-tested — the logic it wires together (Tasks 1-4) already carries the real test coverage.

**Important — read this before editing:** `MainComponent.h`/`.cpp` currently contain temporary diagnostic instrumentation from an unrelated, still-open bug investigation: a `private juce::Timer` base class, a `timerCallback()` override, and a `diagnosticsLabel` member, all marked `// TEMPORARY DIAGNOSTICS` in comments. **Read the actual current content of both files before editing** — do not assume the state described in any older plan document. This task extends the *existing* `timerCallback()` (do not add a second `Timer`) and must not remove or disturb the diagnostics code; leaving it in place is correct, not an oversight to fix.

- [ ] **Step 1: Modify `MainComponent.h`**

Add two includes near the top, after the existing `#include "../ui/BeatIndicatorComponent.h"` line:

```cpp
#include "../practice/AlankarPattern.h"
#include "../practice/AlankarPracticeEngine.h"
```

Add new private members after the existing `bool metronomeRunning = false;` line (or anywhere in the private section — exact position doesn't matter, just add all of these):

```cpp
    juce::ComboBox modeCombo;
    juce::ComboBox alankarPatternCombo;
    juce::TextButton alankarStartButton;
    juce::Label alankarResultsLabel;
    std::unique_ptr<AlankarPracticeEngine> alankarEngine;
    int lastSeenMetronomeBeats = 0; // matches MetronomeAudioSource::getTotalBeatsElapsed()'s int return type
```

- [ ] **Step 2: Modify `MainComponent.cpp`'s constructor**

Read the actual current constructor body first (it ends with the `deviceManager.getCurrentAudioDevice() == nullptr` check, after the `--- TEMPORARY DIAGNOSTICS ---` block that adds `diagnosticsLabel` and calls `startTimerHz(2)`). Insert the following new block right after that `--- END TEMPORARY DIAGNOSTICS ---` comment and before `addAndMakeVisible (pitchGraph);`:

```cpp
    addAndMakeVisible (modeCombo);
    modeCombo.addItem ("Free practice", 1);
    modeCombo.addItem ("Alankar practice", 2);
    modeCombo.setSelectedId (1, juce::dontSendNotification);
    modeCombo.onChange = [this]
    {
        const bool nowAlankarMode = (modeCombo.getSelectedId() == 2);
        alankarPatternCombo.setVisible (nowAlankarMode);
        alankarStartButton.setVisible (nowAlankarMode);
        alankarResultsLabel.setVisible (nowAlankarMode);

        if (! nowAlankarMode)
        {
            // Leaving Alankar mode - stop any active practice and its pacing click.
            alankarEngine.reset();
            metronomeSource.setEnabled (false);
            metronomeRunning = false;
            metronomeStartStopButton.setButtonText ("Start metronome");
            pitchGraph.setTargetBand (std::nullopt);
            alankarResultsLabel.setText ("", juce::dontSendNotification);
        }
    };

    addAndMakeVisible (alankarPatternCombo);
    alankarPatternCombo.addItem ("Alankar 1", 1);
    alankarPatternCombo.addItem ("Alankar 2", 2);
    alankarPatternCombo.addItem ("Alankar 3", 3);
    alankarPatternCombo.addItem ("Alankar 4", 4);
    alankarPatternCombo.addItem ("Alankar 5", 5);
    alankarPatternCombo.setSelectedId (1, juce::dontSendNotification);
    alankarPatternCombo.setVisible (false); // hidden until Alankar mode is selected

    addAndMakeVisible (alankarStartButton);
    alankarStartButton.setButtonText ("Start Alankar Practice");
    alankarStartButton.setVisible (false);
    // Disabled until calibration succeeds (see the justBecameLive branch in
    // pitchWorkerUpdate() below) - starting practice before Sa is known
    // would advance steps with zero pitch data reaching them.
    alankarStartButton.setEnabled (false);
    alankarStartButton.onClick = [this]
    {
        static const AlankarPatternId patternIds[] = {
            AlankarPatternId::Alankar1, AlankarPatternId::Alankar2, AlankarPatternId::Alankar3,
            AlankarPatternId::Alankar4, AlankarPatternId::Alankar5
        };
        const int selectedIndex = alankarPatternCombo.getSelectedId() - 1; // ids are 1-5, array is 0-4
        alankarEngine = std::make_unique<AlankarPracticeEngine> (patternIds[(size_t) selectedIndex]);
        lastSeenMetronomeBeats = metronomeSource.getTotalBeatsElapsed();

        metronomeSource.setTaal (TaalType::PlainClick);
        metronomeSource.setEnabled (true);
        metronomeRunning = true;
        metronomeStartStopButton.setButtonText ("Stop metronome");

        alankarResultsLabel.setText ("Practicing...", juce::dontSendNotification);
    };

    addAndMakeVisible (alankarResultsLabel);
    alankarResultsLabel.setVisible (false);
```

Also, in the constructor, change the existing `setSize (600, 500);` line to `setSize (600, 560);` — the two new rows added to `resized()` below (Step 5) need roughly 48px of extra height; 60 leaves a small margin.

- [ ] **Step 3: Modify `MainComponent.cpp`'s `pitchWorkerUpdate()`**

Read the actual current method body first. Two changes:

**Change A** — in the `if (justBecameLive)` block (which currently ends with `tanpuraSource.setEnabled (true);`), add one line immediately after it:

```cpp
            alankarStartButton.setEnabled (true);
```

**Change B** — replace the existing block:

```cpp
        // Plot whenever this frame carried a pitch, independently of which
        // text branch above ran (the transition frame can be voiced too).
        if (update.centsFromSa.has_value())
            pitchGraph.addPoint (update.timestampMs, *update.centsFromSa);
```

with:

```cpp
        // Plot whenever this frame carried a pitch, independently of which
        // text branch above ran (the transition frame can be voiced too).
        if (update.centsFromSa.has_value())
        {
            pitchGraph.addPoint (update.timestampMs, *update.centsFromSa);

            if (alankarEngine != nullptr && ! alankarEngine->isFinished())
            {
                alankarEngine->onPitchReading (*update.centsFromSa);
                const float target = alankarEngine->currentStepTargetCents();
                pitchGraph.setTargetBand (std::make_pair (target - AlankarPracticeEngine::kInTuneToleranceCents,
                                                           target + AlankarPracticeEngine::kInTuneToleranceCents));
            }
        }
```

- [ ] **Step 4: Modify `MainComponent.cpp`'s `timerCallback()`**

Read the actual current method body first (it starts with `if (worker == nullptr) { diagnosticsLabel.setText(...); return; }` then computes and sets the diagnostics text). Insert the following new block at the very start of the function, **before** that existing `if (worker == nullptr)` check (so Alankar step advancement runs regardless of whether `worker` exists — the metronome, and therefore Alankar practice, doesn't depend on the pitch worker at all):

```cpp
    // --- Alankar practice: beat-driven step advancement ---
    if (alankarEngine != nullptr && ! alankarEngine->isFinished())
    {
        const auto currentBeats = metronomeSource.getTotalBeatsElapsed();
        for (auto b = lastSeenMetronomeBeats; b < currentBeats; ++b)
            alankarEngine->onBeatElapsed();
        lastSeenMetronomeBeats = currentBeats;

        if (alankarEngine->isFinished())
        {
            metronomeSource.setEnabled (false);
            metronomeRunning = false;
            metronomeStartStopButton.setButtonText ("Start metronome");
            pitchGraph.setTargetBand (std::nullopt);

            const auto summary = alankarEngine->getSummary();
            juce::String text = "Done! Overall: " + juce::String (summary.overallTimeInTunePercent, 1) + "% in tune.";
            if (! summary.perSwarTimeInTunePercent.empty())
            {
                text += "  Weakest: " + swarToString (summary.perSwarTimeInTunePercent.front().first)
                        + " (" + juce::String (summary.perSwarTimeInTunePercent.front().second, 1) + "%)";
            }
            alankarResultsLabel.setText (text, juce::dontSendNotification);
        }
        else
        {
            const auto liveSummary = alankarEngine->getSummary();
            alankarResultsLabel.setText (
                "Practicing... step " + juce::String (alankarEngine->currentStepIndex() + 1) + "/"
                    + juce::String (alankarEngine->totalSteps())
                    + "   (" + juce::String (liveSummary.overallTimeInTunePercent, 1) + "% in tune so far)",
                juce::dontSendNotification);
        }
    }
```

Leave the rest of `timerCallback()` (the diagnostics logic) completely unchanged.

- [ ] **Step 5: Modify `MainComponent.cpp`'s `resized()`**

Read the actual current method body first (it ends with `diagnosticsLabel.setBounds (area.removeFromTop (18)); pitchGraph.setBounds (area);`). Insert the following new block right after the `diagnosticsLabel.setBounds(...)` line and before `pitchGraph.setBounds (area);`:

```cpp
    auto alankarControlsRow = area.removeFromTop (30);
    modeCombo.setBounds (alankarControlsRow.removeFromLeft (150).reduced (2));
    alankarStartButton.setBounds (alankarControlsRow.removeFromRight (150).reduced (2));
    alankarPatternCombo.setBounds (alankarControlsRow.reduced (2));

    alankarResultsLabel.setBounds (area.removeFromTop (18));
```

- [ ] **Step 6: Add the CMakeLists.txt change**

Modify `target_link_libraries(riyaaz_app PRIVATE ...)`: add `riyaaz_practice` as a new line, e.g. after `riyaaz_metronome`.

- [ ] **Step 7: Build and run the full test suite, then build `riyaaz_app`**

```bash
cmake --build build --target riyaaz_tests
cd d:/Journey/Code/riyaaz
./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe
cmake --build build --target riyaaz_app
```

Expected: test suite still fully green (this task adds no new tests, only wiring), and `riyaaz_app` builds and links successfully.

- [ ] **Step 8: Commit**

```bash
git add src/app/MainComponent.h src/app/MainComponent.cpp CMakeLists.txt
git commit -m "feat: wire Alankar practice mode into MainComponent"
```
