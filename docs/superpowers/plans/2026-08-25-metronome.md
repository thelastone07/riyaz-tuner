# Metronome / Taal Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a standalone laya (tempo) practice aid — synthesized metronome clicks with traditional taal accent structure (Teentaal/Jhaptaal/Ektaal, plus a plain-click mode), a visual beat indicator, and BPM/taal/start-stop controls — wired alongside the existing tanpura drone and pitch tracking in `MainComponent`.

**Architecture:** Four new components layered like the Tanpura module: `TaalPattern` (pure logic — which beat is Sam/Clap/Khali/Plain) → `MetronomeClick` (pure DSP — a damped sine burst, no delay line) → `MetronomeAudioSource` (JUCE `AudioSource` wrapper, lock-free atomics, additive rendering) → `BeatIndicatorComponent` (UI, polling `Timer`). `MainComponent` wires `MetronomeAudioSource` in parallel with `TanpuraAudioSource`, additively, independent of pitch calibration.

**Tech Stack:** C++17, JUCE 8.0.7 (`juce_core`, `juce_audio_basics`, `juce_gui_basics`), CMake + vcpkg, `juce::UnitTestRunner`.

**Spec:** `docs/superpowers/specs/2026-08-25-metronome-design.md`

## Global Constraints

Copied verbatim from the spec's binding constraints — every task's requirements implicitly include these:

1. No heap allocation on the audio thread, ever.
2. No locks (`CriticalSection`, etc.) on the hot path — atomics only, `exchange()` for consume-once semantics, plain loads/stores otherwise.
3. No sampled/recorded audio — every sound is synthesized.
4. Any duration or sample-count derived from a sample rate must be computed from the *actual* sample rate passed in at the relevant call (`prepareToPlay`, `trigger`), never a baked-in constant. (Direct lesson from the Tanpura final review's Important #1 and #3 findings.)
5. `MetronomeAudioSource::addNextAudioBlock()` must only ever add into the buffer (`AudioBuffer::addSample`), never overwrite (`setSample`) — it runs after the tanpura's overwrite in `MainComponent`.
6. Every synthesis component needs at least one test that verifies its actual output (frequency, timing), not just silence-vs-non-silence. Sample-rate coverage (44100/48000/96000 Hz minimum) is mandatory for any test whose logic depends on sample rate. (Direct lesson from the Tanpura final review's Important #4 finding.)
7. Confirmed taal tables (0-indexed beat positions):

| Taal | Beats | Sam | Khali | Clap |
|---|---|---|---|---|
| PlainClick | 1 | — (always `Plain`) | — | — |
| Teentaal | 16 | 0 | 8 | 4, 12 |
| Jhaptaal | 10 | 0 | 2 | 5, 7 |
| Ektaal | 12 | 0 | 2, 6 | 4, 8, 10 |

8. Khali is audible (a distinct, quieter/duller timbre than a clap), not silent — an explicit, user-confirmed departure from pure tradition for solo practice usability.

---

### Task 1: `TaalPattern`

**Files:**
- Create: `src/audio/metronome/TaalPattern.h`
- Create: `src/audio/metronome/TaalPattern.cpp`
- Test: `src/audio/metronome/TaalPatternTests.cpp`
- Modify: `CMakeLists.txt` (new `riyaaz_metronome` library target after line 80, before the `riyaaz_tests` console app block; add the test file to `riyaaz_tests`' sources; add `riyaaz_metronome` to `riyaaz_tests`' link libraries)

**Interfaces:**
- Produces: `enum class TaalType { PlainClick, Teentaal, Jhaptaal, Ektaal };`, `enum class BeatType { Sam, Clap, Khali, Plain };`, `class TaalPattern { public: explicit TaalPattern (TaalType type); int beatCount() const; BeatType classify (int beatIndex) const; };`

- [ ] **Step 1: Write the test file**

Create `src/audio/metronome/TaalPatternTests.cpp`:

```cpp
#include "TaalPattern.h"
#include <juce_core/juce_core.h>

class TaalPatternTests : public juce::UnitTest
{
public:
    TaalPatternTests() : juce::UnitTest ("TaalPattern", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("PlainClick has a single beat, always classified as Plain, no cycle");
        {
            TaalPattern pattern (TaalType::PlainClick);
            expectEquals (pattern.beatCount(), 1);
            expect (pattern.classify (0) == BeatType::Plain);
        }

        beginTest ("Teentaal (16 beats): Sam at 0, Khali at 8, Clap at 4 and 12, Plain elsewhere");
        {
            TaalPattern pattern (TaalType::Teentaal);
            expectEquals (pattern.beatCount(), 16);

            const BeatType expected[16] = {
                BeatType::Sam,   BeatType::Plain, BeatType::Plain, BeatType::Plain,
                BeatType::Clap,  BeatType::Plain, BeatType::Plain, BeatType::Plain,
                BeatType::Khali, BeatType::Plain, BeatType::Plain, BeatType::Plain,
                BeatType::Clap,  BeatType::Plain, BeatType::Plain, BeatType::Plain
            };

            for (int i = 0; i < 16; ++i)
                expect (pattern.classify (i) == expected[i], "beat " + juce::String (i));
        }

        beginTest ("Jhaptaal (10 beats): Sam at 0, Khali at 2, Clap at 5 and 7, Plain elsewhere");
        {
            TaalPattern pattern (TaalType::Jhaptaal);
            expectEquals (pattern.beatCount(), 10);

            const BeatType expected[10] = {
                BeatType::Sam,  BeatType::Plain, BeatType::Khali, BeatType::Plain, BeatType::Plain,
                BeatType::Clap, BeatType::Plain, BeatType::Clap,  BeatType::Plain, BeatType::Plain
            };

            for (int i = 0; i < 10; ++i)
                expect (pattern.classify (i) == expected[i], "beat " + juce::String (i));
        }

        beginTest ("Ektaal (12 beats): Sam at 0, Khali at 2 and 6, Clap at 4, 8 and 10, Plain elsewhere");
        {
            TaalPattern pattern (TaalType::Ektaal);
            expectEquals (pattern.beatCount(), 12);

            const BeatType expected[12] = {
                BeatType::Sam,  BeatType::Plain, BeatType::Khali, BeatType::Plain,
                BeatType::Clap, BeatType::Plain, BeatType::Khali, BeatType::Plain,
                BeatType::Clap, BeatType::Plain, BeatType::Clap,  BeatType::Plain
            };

            for (int i = 0; i < 12; ++i)
                expect (pattern.classify (i) == expected[i], "beat " + juce::String (i));
        }
    }
};

static TaalPatternTests taalPatternTestsInstance;
```

- [ ] **Step 2: Write `TaalPattern.h`**

```cpp
#pragma once

// Which taal (or plain click) the metronome is keeping. Four fixed patterns
// forever - not worth a generic vibhag-pattern parser (YAGNI).
enum class TaalType { PlainClick, Teentaal, Jhaptaal, Ektaal };

// How a single beat in the cycle is accented. Sam = beat 1, the start of the
// cycle. Clap = a vibhag (section) boundary that gets a clap accent. Khali =
// a vibhag boundary that is traditionally silent in real taal practice, but
// is rendered as a distinct, quieter/duller sound here (an explicit,
// user-confirmed decision for solo practice usability - see the spec).
// Plain = every other beat.
enum class BeatType { Sam, Clap, Khali, Plain };

// Stateless besides which taal it was constructed for - classify() answers
// "what kind of beat is index i" for that taal.
class TaalPattern
{
public:
    explicit TaalPattern (TaalType type);

    int beatCount() const;

    // beatIndex must be in [0, beatCount()) - a contract violation outside
    // that range, not a runtime case to handle gracefully (callers always
    // keep their beat index in range via modulo).
    BeatType classify (int beatIndex) const;

private:
    TaalType type;
};
```

- [ ] **Step 3: Write `TaalPattern.cpp`**

```cpp
#include "TaalPattern.h"
#include <juce_core/juce_core.h>

TaalPattern::TaalPattern (TaalType typeIn) : type (typeIn)
{
}

int TaalPattern::beatCount() const
{
    switch (type)
    {
        case TaalType::PlainClick: return 1;
        case TaalType::Teentaal:   return 16;
        case TaalType::Jhaptaal:   return 10;
        case TaalType::Ektaal:     return 12;
    }

    jassertfalse; // unreachable - every TaalType enumerator is handled above
    return 1;
}

BeatType TaalPattern::classify (int beatIndex) const
{
    jassert (beatIndex >= 0 && beatIndex < beatCount());

    switch (type)
    {
        case TaalType::PlainClick:
            return BeatType::Plain;

        case TaalType::Teentaal:
            if (beatIndex == 0) return BeatType::Sam;
            if (beatIndex == 8) return BeatType::Khali;
            if (beatIndex == 4 || beatIndex == 12) return BeatType::Clap;
            return BeatType::Plain;

        case TaalType::Jhaptaal:
            if (beatIndex == 0) return BeatType::Sam;
            if (beatIndex == 2) return BeatType::Khali;
            if (beatIndex == 5 || beatIndex == 7) return BeatType::Clap;
            return BeatType::Plain;

        case TaalType::Ektaal:
            if (beatIndex == 0) return BeatType::Sam;
            if (beatIndex == 2 || beatIndex == 6) return BeatType::Khali;
            if (beatIndex == 4 || beatIndex == 8 || beatIndex == 10) return BeatType::Clap;
            return BeatType::Plain;
    }

    jassertfalse; // unreachable - every TaalType enumerator is handled above
    return BeatType::Plain;
}
```

- [ ] **Step 4: Add the CMakeLists.txt changes**

Insert after line 80 (the end of the `riyaaz_tanpura` block), before line 82 (`juce_add_console_app(riyaaz_tests ...)`):

```cmake
add_library(riyaaz_metronome STATIC
    src/audio/metronome/TaalPattern.cpp
)
target_include_directories(riyaaz_metronome PUBLIC src)
target_link_libraries(riyaaz_metronome PUBLIC juce::juce_core)
target_compile_definitions(riyaaz_metronome PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)
```

Modify the `target_sources(riyaaz_tests PRIVATE ...)` block (currently lines 83-97): add `src/audio/metronome/TaalPatternTests.cpp` as a new line, anywhere among the other test source lines (e.g. after `src/audio/tanpura/TanpuraAudioSourceTests.cpp`).

Modify the `target_link_libraries(riyaaz_tests PRIVATE ...)` block (currently lines 110-119): add `riyaaz_metronome` as a new line, e.g. after `riyaaz_tanpura`.

- [ ] **Step 5: Build and confirm the test compiles and fails without the header found, then passes**

```bash
cmake -B build -S . -G Ninja -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --target riyaaz_tests
```

Expected: builds cleanly (Steps 2-4 already provide the real header/cpp/CMake changes, so there is no separate "watch it fail" checkpoint here beyond confirming the new library and test target actually wire together - if this is the very first attempt, deleting `TaalPattern.h`/`TaalPattern.cpp` temporarily and rebuilding is one way to confirm the test file alone doesn't silently no-op, but do not leave the files deleted).

Then run the test binary from the repo root (the model path is relative to the repo root, not the build output directory):

```bash
cd d:/Journey/Code/riyaaz
./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe
```

Expected: PASS, all 4 new `TaalPattern` tests green, exit code 0. Note the total test count before and after (it was 67 at the end of the Tanpura plan).

- [ ] **Step 6: Commit**

```bash
git add src/audio/metronome/TaalPattern.h src/audio/metronome/TaalPattern.cpp src/audio/metronome/TaalPatternTests.cpp CMakeLists.txt
git commit -m "feat: add TaalPattern (Teentaal/Jhaptaal/Ektaal/Plain beat classification)"
```

---

### Task 2: `MetronomeClick`

**Files:**
- Create: `src/audio/metronome/MetronomeClick.h`
- Create: `src/audio/metronome/MetronomeClick.cpp`
- Test: `src/audio/metronome/MetronomeClickTests.cpp`
- Modify: `CMakeLists.txt` (add `src/audio/metronome/MetronomeClick.cpp` to the `riyaaz_metronome` target's sources; add the test file to `riyaaz_tests`' sources)

**Interfaces:**
- Consumes: `BeatType` from `TaalPattern.h` (Task 1).
- Produces: `class MetronomeClick { public: void trigger (BeatType type, double sampleRateIn); float renderNextSample(); bool isSounding() const; };`

Exact per-`BeatType` synthesis parameters (fixed here so Task 2's implementation and its own test agree — no other task depends on these exact numbers):

| BeatType | Frequency (Hz) | Duration (s) | Initial amplitude |
|---|---|---|---|
| Sam | 1200 | 0.050 | 1.00 |
| Clap | 900 | 0.040 | 0.75 |
| Khali | 500 | 0.035 | 0.45 |
| Plain | 700 | 0.020 | 0.30 |

- [ ] **Step 1: Write the test file**

Create `src/audio/metronome/MetronomeClickTests.cpp`:

```cpp
#include "MetronomeClick.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

namespace
{
    // 44100 alone previously hid two real Tanpura bugs that were purely
    // sample-rate-dependent - the set deliberately includes a rate above
    // 48000.
    constexpr double kTestSampleRates[] = { 44100.0, 48000.0, 96000.0 };

    struct ClickSpec { BeatType type; const char* name; float frequencyHz; float durationSeconds; };
    constexpr ClickSpec kClickSpecs[] = {
        { BeatType::Sam,   "Sam",   1200.0f, 0.050f },
        { BeatType::Clap,  "Clap",  900.0f,  0.040f },
        { BeatType::Khali, "Khali", 500.0f,  0.035f },
        { BeatType::Plain, "Plain", 700.0f,  0.020f },
    };

    // Magnitude of the DFT evaluated at one arbitrary (non-bin-aligned)
    // frequency, via the Goertzel recurrence.
    double goertzelMagnitude (const std::vector<float>& samples, double frequencyHz, double sampleRate)
    {
        const double omega = 2.0 * juce::MathConstants<double>::pi * frequencyHz / sampleRate;
        const double coeff = 2.0 * std::cos (omega);
        double s1 = 0.0, s2 = 0.0;

        for (const float v : samples)
        {
            const double s0 = (double) v + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        return std::sqrt (s1 * s1 + s2 * s2 - coeff * s1 * s2);
    }

    double measureDominantFrequency (const std::vector<float>& samples, double sampleRate,
                                     double targetHz, double searchRadiusHz, double stepHz)
    {
        double bestHz = targetHz;
        double bestMagnitude = -1.0;

        for (double f = targetHz - searchRadiusHz; f <= targetHz + searchRadiusHz + 1.0e-9; f += stepHz)
        {
            const double magnitude = goertzelMagnitude (samples, f, sampleRate);
            if (magnitude > bestMagnitude)
            {
                bestMagnitude = magnitude;
                bestHz = f;
            }
        }

        return bestHz;
    }

    double centsBetween (double measuredHz, double referenceHz)
    {
        return 1200.0 * std::log2 (measuredHz / referenceHz);
    }
}

class MetronomeClickTests : public juce::UnitTest
{
public:
    MetronomeClickTests() : juce::UnitTest ("MetronomeClick", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("A fresh (never-triggered) click is not sounding and renders silence");
        {
            MetronomeClick click;
            expect (! click.isSounding());
            expectWithinAbsoluteError (click.renderNextSample(), 0.0f, 0.0001f);
        }

        for (const auto& spec : kClickSpecs)
        {
            for (const double sampleRate : kTestSampleRates)
            {
                beginTest (juce::String ("Immediately after trigger(), ") + spec.name
                           + " has significant energy (non-silent) at " + juce::String (sampleRate, 0) + "Hz");
                {
                    MetronomeClick click;
                    click.trigger (spec.type, sampleRate);
                    expect (click.isSounding());

                    float sumSquares = 0.0f;
                    constexpr int kSamplesToCheck = 20;
                    for (int i = 0; i < kSamplesToCheck; ++i)
                    {
                        const float s = click.renderNextSample();
                        sumSquares += s * s;
                    }
                    const float rms = std::sqrt (sumSquares / (float) kSamplesToCheck);
                    expect (rms > 0.05f);
                }
            }
        }

        for (const auto& spec : kClickSpecs)
        {
            for (const double sampleRate : kTestSampleRates)
            {
                beginTest (juce::String (spec.name) + "'s dominant frequency matches its intended pitch at "
                           + juce::String (sampleRate, 0) + "Hz");
                {
                    MetronomeClick click;
                    click.trigger (spec.type, sampleRate);

                    const int windowSamples = (int) (sampleRate * (double) spec.durationSeconds);
                    std::vector<float> window ((size_t) windowSamples, 0.0f);
                    for (int i = 0; i < windowSamples; ++i)
                        window[(size_t) i] = click.renderNextSample();

                    const double measuredHz = measureDominantFrequency (window, sampleRate,
                                                                        (double) spec.frequencyHz, 30.0, 1.0);
                    const double errorCents = centsBetween (measuredHz, (double) spec.frequencyHz);

                    // A generous tolerance: the decay envelope shortens the
                    // effective analysis window and can smear the spectral
                    // peak slightly, especially for the shortest burst
                    // (Plain, ~14 cycles at 700Hz). 30 cents is still easily
                    // enough to catch a wrong-frequency regression.
                    expect (std::abs (errorCents) < 30.0,
                            "measured " + juce::String (measuredHz, 3) + "Hz ("
                                + juce::String (errorCents, 2) + " cents) vs target "
                                + juce::String (spec.frequencyHz, 1) + "Hz");
                }
            }
        }

        for (const auto& spec : kClickSpecs)
        {
            for (const double sampleRate : kTestSampleRates)
            {
                beginTest (juce::String (spec.name) + " decays to silence within its intended duration at "
                           + juce::String (sampleRate, 0) + "Hz");
                {
                    MetronomeClick click;
                    click.trigger (spec.type, sampleRate);

                    // Render past the longest possible duration (Sam's
                    // 0.05s) with a small safety margin, regardless of which
                    // type this is - simpler than tracking each type's own
                    // duration here, and just as conclusive.
                    const int renderSamples = (int) (sampleRate * 0.05) + 10;
                    for (int i = 0; i < renderSamples; ++i)
                        click.renderNextSample();

                    expect (! click.isSounding());
                    expectWithinAbsoluteError (click.renderNextSample(), 0.0f, 0.0001f);
                }
            }
        }
    }
};

static MetronomeClickTests metronomeClickTestsInstance;
```

- [ ] **Step 2: Write `MetronomeClick.h`**

```cpp
#pragma once
#include "TaalPattern.h" // for BeatType

// A single synthesized metronome tick: a damped sine burst. No delay line,
// no buffer of any kind - there is nothing to allocate or reserve, unlike
// KarplusStrongString.
class MetronomeClick
{
public:
    void trigger (BeatType type, double sampleRateIn);
    float renderNextSample();
    bool isSounding() const;

private:
    double phase = 0.0;
    double phaseIncrement = 0.0;
    float amplitude = 0.0f;
    float decayPerSample = 1.0f;
    int samplesRemaining = 0;
};
```

- [ ] **Step 3: Write `MetronomeClick.cpp`**

```cpp
#include "MetronomeClick.h"
#include <juce_core/juce_core.h>
#include <cmath>

void MetronomeClick::trigger (BeatType type, double sampleRateIn)
{
    float frequencyHz;
    float initialAmplitude;
    float durationSeconds;

    switch (type)
    {
        case BeatType::Sam:   frequencyHz = 1200.0f; initialAmplitude = 1.00f; durationSeconds = 0.050f; break;
        case BeatType::Clap:  frequencyHz = 900.0f;  initialAmplitude = 0.75f; durationSeconds = 0.040f; break;
        case BeatType::Khali: frequencyHz = 500.0f;  initialAmplitude = 0.45f; durationSeconds = 0.035f; break;
        case BeatType::Plain: frequencyHz = 700.0f;  initialAmplitude = 0.30f; durationSeconds = 0.020f; break;
        default:              frequencyHz = 700.0f;  initialAmplitude = 0.30f; durationSeconds = 0.020f; break; // unreachable
    }

    phase = 0.0;
    phaseIncrement = 2.0 * juce::MathConstants<double>::pi * (double) frequencyHz / sampleRateIn;
    amplitude = initialAmplitude;
    samplesRemaining = juce::jmax (1, (int) (sampleRateIn * (double) durationSeconds));

    // decayPerSample is derived so the envelope reaches -60dB (0.001x)
    // exactly at samplesRemaining==0, computed from the ACTUAL sampleRateIn
    // passed here (never a baked-in rate) - this is what keeps the cutoff
    // from ever truncating an audible tail, the exact mistake
    // KarplusStrongString's original kMaxRingSamples made.
    decayPerSample = (float) std::pow (0.001, 1.0 / (double) samplesRemaining);
}

float MetronomeClick::renderNextSample()
{
    if (samplesRemaining <= 0)
        return 0.0f;

    const float out = amplitude * (float) std::sin (phase);

    phase += phaseIncrement;
    if (phase >= 2.0 * juce::MathConstants<double>::pi)
        phase -= 2.0 * juce::MathConstants<double>::pi;

    amplitude *= decayPerSample;
    --samplesRemaining;

    return out;
}

bool MetronomeClick::isSounding() const
{
    return samplesRemaining > 0;
}
```

- [ ] **Step 4: Add the CMakeLists.txt changes**

Modify the `riyaaz_metronome` target's sources (added in Task 1): add `src/audio/metronome/MetronomeClick.cpp` as a new line inside `add_library(riyaaz_metronome STATIC ...)`.

Modify the `target_sources(riyaaz_tests PRIVATE ...)` block: add `src/audio/metronome/MetronomeClickTests.cpp` as a new line, near the `TaalPatternTests.cpp` line added in Task 1.

- [ ] **Step 5: Build and run**

```bash
cmake --build build --target riyaaz_tests
cd d:/Journey/Code/riyaaz
./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe
```

Expected: PASS, all new `MetronomeClick` tests green (4 types × 3 sample rates × 3 test categories = 36 new tests, plus 1 "fresh click" test = 37), exit code 0.

- [ ] **Step 6: Commit**

```bash
git add src/audio/metronome/MetronomeClick.h src/audio/metronome/MetronomeClick.cpp src/audio/metronome/MetronomeClickTests.cpp CMakeLists.txt
git commit -m "feat: add MetronomeClick (synthesized damped-sine-burst tick, per-BeatType timbre)"
```

---

### Task 3: `MetronomeAudioSource`

**Files:**
- Create: `src/audio/metronome/MetronomeAudioSource.h`
- Create: `src/audio/metronome/MetronomeAudioSource.cpp`
- Test: `src/audio/metronome/MetronomeAudioSourceTests.cpp`
- Modify: `CMakeLists.txt` (add `MetronomeAudioSource.cpp` to `riyaaz_metronome`'s sources, add `juce::juce_audio_basics` to `riyaaz_metronome`'s link libraries, add the test file to `riyaaz_tests`' sources)

**Interfaces:**
- Consumes: `TaalPattern`/`TaalType`/`BeatType` (Task 1), `MetronomeClick` (Task 2).
- Produces: `class MetronomeAudioSource : public juce::AudioSource { public: void setBpm (float newBpm); void setTaal (TaalType newType); void setEnabled (bool shouldBeEnabled); int getCurrentBeatIndex() const; int getTotalBeatsElapsed() const; void prepareToPlay (int, double) override; void releaseResources() override; void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override; void addNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill); };` — `getTotalBeatsElapsed()` is a monotonically-increasing counter (never wraps, unlike `getCurrentBeatIndex()`) that Task 4's `BeatIndicatorComponent` needs to detect "a new beat just fired" even when `beatCount()==1` and the beat index itself never changes.

- [ ] **Step 1: Write the test file**

Create `src/audio/metronome/MetronomeAudioSourceTests.cpp`:

```cpp
#include "MetronomeAudioSource.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

class MetronomeAudioSourceTests : public juce::UnitTest
{
public:
    MetronomeAudioSourceTests() : juce::UnitTest ("MetronomeAudioSource", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("Disabled source contributes nothing - a pre-filled buffer is preserved");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            // setEnabled() NOT called - defaults to disabled

            juce::AudioBuffer<float> buffer (2, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (ch, i, 0.37f); // known nonzero value

            juce::AudioSourceChannelInfo info (&buffer, 0, 512);
            source.addNextAudioBlock (info);

            // Proves additive-not-overwrite AND disabled-contributes-nothing
            // in one check: if addNextAudioBlock ever wrote via setSample()
            // instead of addSample(), or ran while disabled, this changes.
            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.37f, 0.0001f);
            expectWithinAbsoluteError (buffer.getSample (1, 511), 0.37f, 0.0001f);

            source.releaseResources();
        }

        beginTest ("Enabling the source adds audible content on top of existing buffer contents");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setTaal (TaalType::Teentaal);
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (ch, i, 0.37f);

            juce::AudioSourceChannelInfo info (&buffer, 0, 512);
            source.addNextAudioBlock (info); // Sam triggers synchronously inside this first call

            // Sample 0 of a fresh trigger is sin(phase=0)=0 exactly, so it
            // alone can't prove anything was added - search the whole block.
            bool foundDifference = false;
            for (int i = 0; i < 512; ++i)
            {
                if (std::abs (buffer.getSample (0, i) - 0.37f) > 0.001f)
                {
                    foundDifference = true;
                    break;
                }
            }
            expect (foundDifference);

            source.releaseResources();
        }

        beginTest ("Changing taal resets the beat index to 0 at the next block");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setTaal (TaalType::Teentaal);
            source.setBpm (300.0f); // samplesPerBeat = 44100*60/300 = 8820
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            // 18 blocks of 512 = 9216 samples > 8820 - guarantees at least
            // one beat boundary has passed, so the beat index is provably
            // no longer 0.
            for (int block = 0; block < 18; ++block)
                source.addNextAudioBlock (info);
            expect (source.getCurrentBeatIndex() != 0);

            // Switching taal must reset the clock regardless of which beat
            // we were mid-cycle on - critical because Jhaptaal only has 10
            // beats, so a Teentaal beat index above 9 would be out of
            // TaalPattern::classify()'s valid range for Jhaptaal if it were
            // not reset.
            source.setTaal (TaalType::Jhaptaal);
            source.addNextAudioBlock (info);
            expectEquals (source.getCurrentBeatIndex(), 0);

            source.releaseResources();
        }

        beginTest ("setEnabled(true) resets the beat index to 0 (Stop-then-Start always restarts at Sam)");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setTaal (TaalType::Teentaal);
            source.setBpm (300.0f); // samplesPerBeat = 8820
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            for (int block = 0; block < 18; ++block) // moves well past beat 0, as above
                source.addNextAudioBlock (info);
            expect (source.getCurrentBeatIndex() != 0);

            source.setEnabled (false);
            source.addNextAudioBlock (info); // disabled - contributes nothing, does not advance the clock
            source.setEnabled (true);        // re-enabling must reset to beat 0
            source.addNextAudioBlock (info);
            expectEquals (source.getCurrentBeatIndex(), 0);

            source.releaseResources();
        }

        beginTest ("A BPM change takes effect on the NEXT beat boundary, not the one already in progress");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setTaal (TaalType::Teentaal);
            source.setBpm (80.0f); // samplesPerBeat = 44100*60/80 = 33075
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            source.addNextAudioBlock (info); // resetClock() fires here: beat 0 starts at 80 BPM (33075 samples/beat)
            source.setBpm (300.0f);          // speed up mid-beat-0; must NOT shorten the beat already in progress

            // 64 more blocks (65 total) = 33280 samples, just past the
            // 80-BPM beat-0 boundary (33075) - beat index must become 1,
            // and only THEN is the new, faster 300-BPM tempo (8820
            // samples/beat) read for beat 1.
            for (int block = 0; block < 64; ++block)
                source.addNextAudioBlock (info);
            expectEquals (source.getCurrentBeatIndex(), 1);

            // 18 more blocks = 9216 samples, comfortably past 8820 (beat 1's
            // new, faster duration) but nowhere close to another 33075 -
            // only reachable if the BPM change actually took effect.
            for (int block = 0; block < 18; ++block)
                source.addNextAudioBlock (info);
            expectEquals (source.getCurrentBeatIndex(), 2);

            source.releaseResources();
        }

        beginTest ("PlainClick taal keeps the beat index at 0 across many blocks (single-beat cycle)");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setTaal (TaalType::PlainClick);
            source.setBpm (300.0f); // fast, to cross many beat boundaries quickly
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            // samplesPerBeat = 8820 at 300 BPM; 40 blocks = 20480 samples,
            // more than two full beats.
            for (int block = 0; block < 40; ++block)
            {
                source.addNextAudioBlock (info);
                expectEquals (source.getCurrentBeatIndex(), 0); // beatCount()==1, so index % 1 is always 0
            }

            source.releaseResources();
        }

        beginTest ("getTotalBeatsElapsed() keeps incrementing even when the beat index wraps back to 0");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setTaal (TaalType::PlainClick); // beatCount()==1, index never leaves 0
            source.setBpm (300.0f); // samplesPerBeat = 8820
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            const int startTotal = source.getTotalBeatsElapsed();

            // 40 blocks = 20480 samples = 2 full beats at 8820 samples/beat
            // (17640) plus change - at least 2 more beats must have fired.
            for (int block = 0; block < 40; ++block)
                source.addNextAudioBlock (info);

            expect (source.getTotalBeatsElapsed() >= startTotal + 2);

            source.releaseResources();
        }
    }
};

static MetronomeAudioSourceTests metronomeAudioSourceTestsInstance;
```

- [ ] **Step 2: Write `MetronomeAudioSource.h`**

```cpp
#pragma once
#include "TaalPattern.h"
#include "MetronomeClick.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

class MetronomeAudioSource : public juce::AudioSource
{
public:
    void setBpm (float newBpm);
    void setTaal (TaalType newType);
    void setEnabled (bool shouldBeEnabled);

    int getCurrentBeatIndex() const;   // for the UI: which light in the row is current
    int getTotalBeatsElapsed() const;  // for the UI: monotonically increasing, detects "a new beat fired" even when beatCount()==1

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;

    // Required by the AudioSource interface, never called by this app (see
    // MainComponent's getNextAudioBlock() for the real render path) -
    // implemented as clearActiveBufferRegion() followed by
    // addNextAudioBlock(bufferToFill), so if anything ever does call it
    // (e.g. a future refactor), it correctly satisfies AudioSource's
    // overwrite convention in terms of the additive primitive below, rather
    // than being a dead or incorrect stub.
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;

    // The real render path - ADDS into the buffer, never overwrites. Called
    // after TanpuraAudioSource::getNextAudioBlock() in MainComponent, which
    // overwrites the buffer first.
    void addNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill);

private:
    void resetClock();
    void triggerBeat (int beatIndex);

    MetronomeClick click;                          // audio-thread only
    TaalPattern pattern { TaalType::PlainClick };   // rebuilt on the audio thread when a taal change is consumed

    double sampleRate = 44100.0;
    double samplesPerBeat = 0.0;   // recomputed from bpm only at each beat boundary, not continuously mid-beat
    double samplesIntoBeat = 0.0;  // advances by 1.0 per sample; wraps by subtracting samplesPerBeat, not resetting to 0, to avoid long-run tempo drift
    int currentBeatIndex = 0;

    // UI-thread-writable, audio-thread-readable. taalChangePending and
    // resetPending need exchange() (consume-once) semantics; bpm/enabled are
    // simple values read fresh, no "consume once" requirement.
    std::atomic<float> bpm { 80.0f };
    std::atomic<TaalType> pendingTaalType { TaalType::PlainClick };
    std::atomic<bool> taalChangePending { false };
    std::atomic<bool> resetPending { false };
    std::atomic<bool> enabled { false };

    // Audio-thread-writable, UI-thread-readable (single writer / single
    // reader, no tear risk for a plain int).
    std::atomic<int> currentBeatIndexForUi { 0 };
    std::atomic<int> totalBeatsElapsedForUi { 0 };
};
```

- [ ] **Step 3: Write `MetronomeAudioSource.cpp`**

```cpp
#include "MetronomeAudioSource.h"

void MetronomeAudioSource::setBpm (float newBpm)
{
    bpm = juce::jlimit (20.0f, 300.0f, newBpm);
}

void MetronomeAudioSource::setTaal (TaalType newType)
{
    pendingTaalType = newType;
    taalChangePending = true;
}

void MetronomeAudioSource::setEnabled (bool shouldBeEnabled)
{
    if (shouldBeEnabled && ! enabled.load())
        resetPending = true; // Stop-then-Start always restarts at beat 0 (Sam)
    enabled = shouldBeEnabled;
}

int MetronomeAudioSource::getCurrentBeatIndex() const
{
    return currentBeatIndexForUi.load();
}

int MetronomeAudioSource::getTotalBeatsElapsed() const
{
    return totalBeatsElapsedForUi.load();
}

void MetronomeAudioSource::prepareToPlay (int /*samplesPerBlockExpected*/, double sampleRateIn)
{
    sampleRate = sampleRateIn;
    resetPending = true; // force an initial clock reset on the audio thread using whatever taal/bpm is pending
}

void MetronomeAudioSource::releaseResources()
{
}

void MetronomeAudioSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();
    addNextAudioBlock (bufferToFill);
}

void MetronomeAudioSource::triggerBeat (int beatIndex)
{
    currentBeatIndex = beatIndex;
    click.trigger (pattern.classify (beatIndex), sampleRate);
    currentBeatIndexForUi.store (beatIndex);
    totalBeatsElapsedForUi.fetch_add (1, std::memory_order_relaxed);
}

void MetronomeAudioSource::resetClock()
{
    samplesIntoBeat = 0.0;
    samplesPerBeat = sampleRate * 60.0 / (double) bpm.load();
    triggerBeat (0);
}

void MetronomeAudioSource::addNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Both flags are unconditionally consumed exactly once, regardless of
    // which (or both) were set - if only one branch's exchange() ran, the
    // other flag could stay pending and fire an extra, unwanted resetClock()
    // on some later call.
    const bool taalChanged = taalChangePending.exchange (false);
    const bool resetRequested = resetPending.exchange (false);

    if (taalChanged)
        pattern = TaalPattern (pendingTaalType.load());

    if (taalChanged || resetRequested)
        resetClock();

    if (! enabled.load())
        return;

    auto* buffer = bufferToFill.buffer;
    const int numChannels = buffer->getNumChannels();

    for (int i = 0; i < bufferToFill.numSamples; ++i)
    {
        const float s = click.renderNextSample();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer->addSample (ch, bufferToFill.startSample + i, s);

        samplesIntoBeat += 1.0;
        if (samplesIntoBeat >= samplesPerBeat)
        {
            samplesIntoBeat -= samplesPerBeat; // preserve remainder, avoids long-run tempo drift
            samplesPerBeat = sampleRate * 60.0 / (double) bpm.load(); // re-read bpm fresh at this new beat boundary
            triggerBeat ((currentBeatIndex + 1) % pattern.beatCount());
        }
    }
}
```

- [ ] **Step 4: Add the CMakeLists.txt changes**

Modify the `riyaaz_metronome` target's sources: add `src/audio/metronome/MetronomeAudioSource.cpp` as a new line.

Modify `target_link_libraries(riyaaz_metronome PUBLIC juce::juce_core)` (from Task 1) to also link `juce::juce_audio_basics` — either append it to the same line (`PUBLIC juce::juce_core juce::juce_audio_basics`) or add a second `target_link_libraries(riyaaz_metronome PUBLIC juce::juce_audio_basics)` call, matching how `riyaaz_tanpura` does it.

Modify the `target_sources(riyaaz_tests PRIVATE ...)` block: add `src/audio/metronome/MetronomeAudioSourceTests.cpp` as a new line.

- [ ] **Step 5: Build and run**

```bash
cmake --build build --target riyaaz_tests
cd d:/Journey/Code/riyaaz
./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe
```

Expected: PASS, all 7 new `MetronomeAudioSource` tests green, exit code 0.

- [ ] **Step 6: Commit**

```bash
git add src/audio/metronome/MetronomeAudioSource.h src/audio/metronome/MetronomeAudioSource.cpp src/audio/metronome/MetronomeAudioSourceTests.cpp CMakeLists.txt
git commit -m "feat: add MetronomeAudioSource (lock-free taal-aware beat clock, additive rendering)"
```

---

### Task 4: `BeatIndicatorComponent`

**Files:**
- Create: `src/ui/BeatIndicatorComponent.h`
- Create: `src/ui/BeatIndicatorComponent.cpp`
- Modify: `CMakeLists.txt` (add `src/ui/BeatIndicatorComponent.cpp` to `riyaaz_app`'s sources; add `riyaaz_metronome` to `riyaaz_app`'s link libraries)

No dedicated test file: this is a paint-only `Component`, mirroring `PitchGraphComponent`'s established precedent of not unit-testing the painting logic itself — the pure-logic class it displays (`TaalPattern`) already has its own full test coverage from Task 1. Verification for this task is a successful build of `riyaaz_app` (Step 4 below).

**Interfaces:**
- Consumes: `MetronomeAudioSource::getCurrentBeatIndex()`, `MetronomeAudioSource::getTotalBeatsElapsed()` (Task 3); `TaalPattern`/`TaalType`/`BeatType` (Task 1).
- Produces: `class BeatIndicatorComponent : public juce::Component { public: explicit BeatIndicatorComponent (MetronomeAudioSource& sourceIn); void setTaal (TaalType newType); void paint (juce::Graphics& g) override; };` — `setTaal()` is called by `MainComponent`'s taal `ComboBox` handler (Task 5) to keep this component's own display pattern in sync; it does not read taal state from the audio thread.

- [ ] **Step 1: Write `BeatIndicatorComponent.h`**

```cpp
#pragma once
#include "../audio/metronome/TaalPattern.h"
#include "../audio/metronome/MetronomeAudioSource.h"
#include <juce_gui_basics/juce_gui_basics.h>

// A row of lights, one per beat in the current taal cycle, colored/sized by
// BeatType and highlighted at the current beat - or, for PlainClick
// (beatCount()==1, no cycle to show progression through), a single dot that
// pulses on every beat instead.
class BeatIndicatorComponent : public juce::Component, private juce::Timer
{
public:
    explicit BeatIndicatorComponent (MetronomeAudioSource& sourceIn);
    ~BeatIndicatorComponent() override;

    // Re-derives this component's own display pattern from the caller's
    // taal selection - this component does not read taal state from the
    // audio thread, only the current beat index/total.
    void setTaal (TaalType newType);

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;
    juce::Colour colourFor (BeatType type) const;

    MetronomeAudioSource& source;
    TaalPattern displayPattern { TaalType::PlainClick };

    int lastSeenTotalBeats = 0;
    float flashIntensity = 0.0f; // 0 (no flash) to 1 (just fired), decays each timer tick
};
```

- [ ] **Step 2: Write `BeatIndicatorComponent.cpp`**

```cpp
#include "BeatIndicatorComponent.h"

BeatIndicatorComponent::BeatIndicatorComponent (MetronomeAudioSource& sourceIn)
    : source (sourceIn)
{
    startTimerHz (30);
}

BeatIndicatorComponent::~BeatIndicatorComponent()
{
    stopTimer();
}

void BeatIndicatorComponent::setTaal (TaalType newType)
{
    displayPattern = TaalPattern (newType);
    repaint();
}

juce::Colour BeatIndicatorComponent::colourFor (BeatType type) const
{
    switch (type)
    {
        case BeatType::Sam:   return juce::Colours::red;
        case BeatType::Clap:  return juce::Colours::dodgerblue;
        case BeatType::Khali: return juce::Colours::grey;
        case BeatType::Plain: return juce::Colours::lightgrey;
    }

    jassertfalse; // unreachable - every BeatType enumerator is handled above
    return juce::Colours::lightgrey;
}

void BeatIndicatorComponent::timerCallback()
{
    const int totalBeats = source.getTotalBeatsElapsed();
    if (totalBeats != lastSeenTotalBeats)
    {
        lastSeenTotalBeats = totalBeats;
        flashIntensity = 1.0f;
    }
    else
    {
        flashIntensity = juce::jmax (0.0f, flashIntensity - 0.12f); // decays to 0 over ~8 ticks (~0.27s at 30Hz)
    }

    repaint();
}

void BeatIndicatorComponent::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const int beatCount = displayPattern.beatCount();
    const int currentBeat = source.getCurrentBeatIndex();

    if (beatCount <= 1)
    {
        // PlainClick: no cycle to show progression through, so pulse a
        // single dot on every beat instead of highlighting a row position.
        const float diameter = juce::jmin (area.getWidth(), area.getHeight()) * (0.4f + 0.4f * flashIntensity);
        auto circle = juce::Rectangle<float> (diameter, diameter).withCentre (area.getCentre());
        g.setColour (colourFor (BeatType::Plain).brighter (flashIntensity));
        g.fillEllipse (circle);
        return;
    }

    const float slotWidth = area.getWidth() / (float) beatCount;
    for (int i = 0; i < beatCount; ++i)
    {
        const BeatType type = displayPattern.classify (i);
        const bool isCurrent = (i == currentBeat);

        const float baseDiameter = juce::jmin (slotWidth, area.getHeight()) * 0.6f;
        const float diameter = isCurrent ? baseDiameter * (1.0f + 0.3f * flashIntensity) : baseDiameter;

        auto slot = area.withX (area.getX() + slotWidth * (float) i).withWidth (slotWidth);
        auto circle = juce::Rectangle<float> (diameter, diameter).withCentre (slot.getCentre());

        auto colour = colourFor (type);
        if (isCurrent)
            colour = colour.brighter (flashIntensity);

        g.setColour (colour);
        g.fillEllipse (circle);
    }
}
```

- [ ] **Step 3: Add the CMakeLists.txt changes**

Modify `target_sources(riyaaz_app PRIVATE ...)` (currently lines 123-132): add `src/ui/BeatIndicatorComponent.cpp` as a new line, e.g. after `src/ui/PitchGraphComponent.cpp`.

Modify `target_link_libraries(riyaaz_app PRIVATE ...)` (currently lines 133-141): add `riyaaz_metronome` as a new line, e.g. after `riyaaz_tanpura`.

- [ ] **Step 4: Build `riyaaz_app` to confirm it compiles and links**

```bash
cmake --build build --target riyaaz_app
```

Expected: builds and links successfully. `BeatIndicatorComponent` is not yet instantiated anywhere (that happens in Task 5), so this step only confirms the new files compile cleanly against `MetronomeAudioSource`/`TaalPattern` and that `riyaaz_app` now has everything it needs linked.

Also rebuild and rerun the test suite to confirm this task didn't disturb it:

```bash
cmake --build build --target riyaaz_tests
cd d:/Journey/Code/riyaaz
./build/riyaaz_tests_artefacts/Debug/riyaaz_tests.exe
```

Expected: same test count and PASS as at the end of Task 3 (this task adds no new tests).

- [ ] **Step 5: Commit**

```bash
git add src/ui/BeatIndicatorComponent.h src/ui/BeatIndicatorComponent.cpp CMakeLists.txt
git commit -m "feat: add BeatIndicatorComponent (per-beat visual, pulsing dot for PlainClick)"
```

---

### Task 5: Wire the metronome into `MainComponent`

**Files:**
- Modify: `src/app/MainComponent.h`
- Modify: `src/app/MainComponent.cpp`

**Interfaces:**
- Consumes: `MetronomeAudioSource` (Task 3), `BeatIndicatorComponent` (Task 4), `TaalType`/`BeatType` (Task 1).
- Produces: nothing new for later tasks — this is the final integration task for this plan.

No test file: `MainComponent` is verified by building `riyaaz_app` and by the user manually running it, matching the established precedent from the Tanpura and RealtimeApp plans (JUCE `Component`-level wiring in this codebase is not unit-tested; the logic it wires together already is).

- [ ] **Step 1: Modify `MainComponent.h`**

Add two new includes near the top (after the existing `#include "../audio/tanpura/TanpuraAudioSource.h"` line):

```cpp
#include "../audio/metronome/MetronomeAudioSource.h"
#include "../ui/BeatIndicatorComponent.h"
```

Add a private static helper declaration (near `pitchWorkerUpdate`):

```cpp
static TaalType taalTypeForComboId (int comboId);
```

Add new private members (after the existing `TanpuraAudioSource tanpuraSource;` member):

```cpp
    MetronomeAudioSource metronomeSource;
```

Add new UI members (after the existing `tanpuraVolumeLabel` member, before `pitchGraph`):

```cpp
    juce::ComboBox metronomeTaalCombo;
    juce::Slider metronomeBpmSlider;
    juce::Label metronomeBpmLabel;
    juce::TextButton metronomeStartStopButton;
    BeatIndicatorComponent beatIndicator { metronomeSource };
```

Add a message-thread-only bool (near `lastUpdate`):

```cpp
    bool metronomeRunning = false;
```

The full modified header should read:

```cpp
// src/app/MainComponent.h
#pragma once
#include "../audio/pitchengine/CrepePitchEngine.h"
#include "../audio/pipeline/PitchPipeline.h"
#include "../audio/tanpura/TanpuraAudioSource.h"
#include "../audio/metronome/MetronomeAudioSource.h"
#include "../audio/worker/PitchWorker.h"
#include "../ui/PitchGraphComponent.h"
#include "../ui/BeatIndicatorComponent.h"
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
    static TaalType taalTypeForComboId (int comboId);

    CrepePitchEngine engine { juce::String ("models/crepe/small.onnx") };
    std::unique_ptr<PitchPipeline> pipeline;   // constructed in prepareToPlay(), once the real sample rate is known
    std::unique_ptr<PitchWorker> worker;       // ditto

    // Owned directly (not a unique_ptr like pipeline/worker): neither has a
    // construction-time dependency on the sample rate - prepareToPlay()
    // hands them the rate later, and both are safe to render from before
    // then (tanpuraSource outputs silence while disabled; metronomeSource
    // does too, since it also defaults to disabled).
    TanpuraAudioSource tanpuraSource;
    MetronomeAudioSource metronomeSource;

    juce::Label statusLabel;
    juce::Slider tanpuraVolumeSlider;
    juce::Label tanpuraVolumeLabel;
    juce::ComboBox metronomeTaalCombo;
    juce::Slider metronomeBpmSlider;
    juce::Label metronomeBpmLabel;
    juce::TextButton metronomeStartStopButton;
    BeatIndicatorComponent beatIndicator { metronomeSource };
    PitchGraphComponent pitchGraph;

    PitchPipelineUpdate lastUpdate { PitchPipelinePhase::Calibrating };
    bool metronomeRunning = false;
};
```

- [ ] **Step 2: Modify `MainComponent.cpp`'s constructor**

Add a free function at the top of the file, after the `#include "MainComponent.h"` line:

```cpp
TaalType MainComponent::taalTypeForComboId (int comboId)
{
    switch (comboId)
    {
        case 1: return TaalType::PlainClick;
        case 2: return TaalType::Teentaal;
        case 3: return TaalType::Jhaptaal;
        case 4: return TaalType::Ektaal;
        default: jassertfalse; return TaalType::PlainClick; // unreachable - the combo box only ever offers ids 1-4
    }
}
```

In the constructor, after the existing tanpura wiring block (after the line `tanpuraSource.setGain ((float) tanpuraVolumeSlider.getValue());` and before `addAndMakeVisible (pitchGraph);`), insert:

```cpp
    addAndMakeVisible (metronomeTaalCombo);
    metronomeTaalCombo.addItem ("Plain click", 1);
    metronomeTaalCombo.addItem ("Teentaal (16)", 2);
    metronomeTaalCombo.addItem ("Jhaptaal (10)", 3);
    metronomeTaalCombo.addItem ("Ektaal (12)", 4);
    metronomeTaalCombo.setSelectedId (1, juce::dontSendNotification);
    metronomeTaalCombo.onChange = [this]
    {
        const auto type = taalTypeForComboId (metronomeTaalCombo.getSelectedId());
        metronomeSource.setTaal (type);
        beatIndicator.setTaal (type);
    };

    addAndMakeVisible (metronomeBpmSlider);
    metronomeBpmSlider.setRange (20.0, 300.0, 1.0);
    metronomeBpmSlider.setValue (80.0, juce::dontSendNotification);
    metronomeBpmSlider.onValueChange = [this]
    {
        metronomeSource.setBpm ((float) metronomeBpmSlider.getValue());
    };

    addAndMakeVisible (metronomeBpmLabel);
    metronomeBpmLabel.setText ("Metronome BPM", juce::dontSendNotification);
    metronomeBpmLabel.attachToComponent (&metronomeBpmSlider, true);

    addAndMakeVisible (metronomeStartStopButton);
    metronomeStartStopButton.setButtonText ("Start metronome");
    metronomeStartStopButton.onClick = [this]
    {
        metronomeRunning = ! metronomeRunning;
        metronomeSource.setEnabled (metronomeRunning);
        metronomeStartStopButton.setButtonText (metronomeRunning ? "Stop metronome" : "Start metronome");
    };

    // Push the initial BPM/taal into the source here, on the message
    // thread, while the audio device is still closed - same reasoning as
    // the tanpura gain push above (prepareToPlay() is an audio-device
    // callback, not guaranteed to run on the message thread). The
    // metronome starts disabled (metronomeRunning defaults to false), so
    // this has no audible effect until Start is pressed.
    metronomeSource.setBpm ((float) metronomeBpmSlider.getValue());
    metronomeSource.setTaal (taalTypeForComboId (metronomeTaalCombo.getSelectedId()));
    beatIndicator.setTaal (taalTypeForComboId (metronomeTaalCombo.getSelectedId()));

    addAndMakeVisible (beatIndicator);
```

Also, in the constructor, change the existing `setSize (600, 400);` line to `setSize (600, 500);`. The new controls add roughly 110px of fixed-height chrome (a 30px taal/start-stop row, a 30px BPM slider row, and a 50px beat indicator); without growing the window, `pitchGraph`'s area would shrink from ~310px to ~200px, undermining the melodic-contour pitch graph's readability. Growing the window by the same ~110px (plus a little slack) keeps `pitchGraph`'s share close to its original size.

- [ ] **Step 3: Modify `MainComponent.cpp`'s `prepareToPlay()`**

After the existing `tanpuraSource.prepareToPlay (samplesPerBlockExpected, sampleRate);` line, add:

```cpp
    // Same reasoning as tanpuraSource immediately above: getNextAudioBlock()
    // will call this unconditionally on every path, including the ones
    // where engine.prepare() fails below, so this must always be prepared.
    // It stays silent (disabled) until Start is pressed, so preparing it
    // early has no audible effect.
    metronomeSource.prepareToPlay (samplesPerBlockExpected, sampleRate);
```

- [ ] **Step 4: Modify `MainComponent.cpp`'s `getNextAudioBlock()`**

After the existing `tanpuraSource.getNextAudioBlock (bufferToFill);` line (the last line of the function), add:

```cpp
    // Additive, not overwriting - runs after the tanpura source above,
    // which has already either cleared the buffer (disabled) or written
    // every sample of every channel (enabled). Order matters: this must
    // come after that overwrite, or its contribution would itself be
    // overwritten.
    metronomeSource.addNextAudioBlock (bufferToFill);
```

- [ ] **Step 5: Modify `MainComponent.cpp`'s `releaseResources()`**

After the existing `tanpuraSource.releaseResources();` line, add:

```cpp
    metronomeSource.releaseResources();
```

- [ ] **Step 6: Modify `MainComponent.cpp`'s `resized()`**

Replace the existing body:

```cpp
void MainComponent::resized()
{
    auto area = getLocalBounds();
    statusLabel.setBounds (area.removeFromTop (60));

    // Only the slider is positioned: tanpuraVolumeLabel is attached to it, so
    // JUCE re-places the label itself whenever the slider moves. The left inset
    // is what the attached label is drawn into - an attached label's width is
    // clamped to its owner's x, so a slider at x == 0 would leave no room and
    // the label would collapse to nothing.
    tanpuraVolumeSlider.setBounds (area.removeFromTop (30).withTrimmedLeft (80).withTrimmedRight (10));

    pitchGraph.setBounds (area);
}
```

with:

```cpp
void MainComponent::resized()
{
    auto area = getLocalBounds();
    statusLabel.setBounds (area.removeFromTop (60));

    // Only the slider is positioned: tanpuraVolumeLabel is attached to it, so
    // JUCE re-places the label itself whenever the slider moves. The left inset
    // is what the attached label is drawn into - an attached label's width is
    // clamped to its owner's x, so a slider at x == 0 would leave no room and
    // the label would collapse to nothing.
    tanpuraVolumeSlider.setBounds (area.removeFromTop (30).withTrimmedLeft (80).withTrimmedRight (10));

    auto metronomeControlsRow = area.removeFromTop (30);
    metronomeTaalCombo.setBounds (metronomeControlsRow.removeFromLeft (150).reduced (2));
    metronomeStartStopButton.setBounds (metronomeControlsRow.removeFromRight (150).reduced (2));

    // Same attached-label reasoning as tanpuraVolumeSlider above, with a
    // wider left inset since "Metronome BPM" is a longer label than "Tanpura".
    metronomeBpmSlider.setBounds (area.removeFromTop (30).withTrimmedLeft (140).withTrimmedRight (10));

    beatIndicator.setBounds (area.removeFromTop (50));

    pitchGraph.setBounds (area);
}
```

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
git add src/app/MainComponent.h src/app/MainComponent.cpp
git commit -m "feat: wire MetronomeAudioSource and BeatIndicatorComponent into MainComponent"
```
