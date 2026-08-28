# Alankar Practice Mode — Design Spec

**Status:** Approved by user, ready for implementation planning.
**Context:** First of four sub-projects (Alankar practice mode, Profiles + persistent storage, Accuracy scoring + session recording, Analytics view) decomposed from a single large feature request. Each sub-project depends on the one before it; this one is deliberately self-contained and delivers value with no persistence at all — session recording and analytics are explicitly out of scope here and come later.

## Purpose

A guided practice mode: pick one of a small library of traditional alankar patterns, and the app steps through it note-by-note (paced by the Metronome's beat clock), showing the current target note and how accurately you're singing it — live, on the existing melodic-contour pitch graph. At the end of a completed pattern, it shows a summary: overall time-in-tune percentage and a per-swar breakdown, so you can see which notes you're actually landing versus missing.

Nothing here is persisted to disk — this plan is entirely message-thread logic plus UI, computing everything live and discarding it when you leave the screen or restart. Persistence is sub-project 3 (Accuracy scoring + session recording), which reuses this plan's exact summary data rather than recomputing it differently later.

## Researched grounding

Looked up directly (not assumed) during design:

- Cents-based pitch deviation is the standard metric for singing accuracy — already the currency this app's entire pitch pipeline runs on (`centsFromSa` flows through `PitchPipeline`/`SwarMapper` already). [Cents – Voice Science](https://www.voicescience.org/lexicon/cents/)
- Trained singers hold roughly ±10–20 cents on a sustained note; untrained singers drift 20–50 cents; ~25 cents sits near the threshold of perceptibility to a listener — "functionally in tune." [Automatic Singing Assessment Survey](https://arxiv.org/html/2601.12153v1)
- Comparable existing Hindustani pitch-training apps (SwarShala, "Learn to Sing") score accuracy by **time-in-tune percentage** — how much of a note's duration was held within tolerance — with color-coded live feedback, not a single pass/fail per note. [SwarShala pitch detection](https://www.swarclassical.com/guides/swarshala/topic.php?product=sh&id=1)
- Traditional alankar patterns are drawn from a small, named, fixed library (not free-form composition), and are conventionally sung ascending then descending, with the descending form being the *exact reverse* of the ascending form. Verified directly against two independent sources, which agreed exactly on the first three patterns. [Learn Raaga Basics](http://learnraagabasics.blogspot.com/2013/08/basic-training-alankaar-paltaa.html), [Sur Taal Harmonium](https://surtaalharmonium.com/blogs-alankar/)

**User-confirmed decisions from design review:**
- ±25 cents = "in tune," matching the research-backed perceptibility threshold.
- Note pacing is tied to the Metronome's BPM (auto-started at Plain click) rather than a fixed duration — reuses the existing Metronome module, and speeding up the BPM slider naturally speeds up the whole pattern, matching how real riyaaz practice progresses.

## Verified pattern library (5 patterns)

Ascending sequences only are stored; descending is derived by reversing ascending at runtime — verified by hand-reversing every one of the 5 ascending sequences below and confirming each matches its independently-documented descending form exactly (shown in the "Verification" column). This is a structural property of how alankars are built, not a shortcut being taken on faith.

All swaras are shuddh (natural) — `Swar::Sa/Re/Ga/Ma/Pa/Dha/Ni` from the existing `SwarMapper.h`. Octave offset is 0 (Madhya, same octave as the calibrated Sa) for every note except the final "Sa'" (Taar, one octave up), offset +1.

| Pattern | Ascending sequence (Swar, octaveOffset) | Verification |
|---|---|---|
| Alankar 1 | Sa,Re,Ga,Ma,Pa,Dha,Ni,Sa'(+1) | Reverse = Sa'(+1),Ni,Dha,Pa,Ma,Ga,Re,Sa — matches documented descending exactly. |
| Alankar 2 | Sa,Re,Re,Ga,Ga,Ma,Ma,Pa,Pa,Dha,Dha,Ni,Ni,Sa'(+1) | Reverse matches documented descending exactly. |
| Alankar 3 | Sa,Re,Ga,Re,Ga,Ma,Ga,Ma,Pa,Ma,Pa,Dha,Pa,Dha,Ni,Dha,Ni,Sa'(+1) | Reverse matches documented descending exactly. |
| Alankar 4 | Sa,Re,Ga,Ma,Re,Ga,Ma,Pa,Ga,Ma,Pa,Dha,Ma,Pa,Dha,Ni,Pa,Dha,Ni,Sa'(+1) | Reverse matches documented descending exactly. |
| Alankar 5 | Sa,Re,Ga,Ma,Pa,Re,Ga,Ma,Pa,Dha,Ga,Ma,Pa,Dha,Ni,Ma,Pa,Dha,Ni,Sa'(+1) | Reverse matches documented descending exactly. |

The peak note (Sa') is the last note of the ascending half and the first note of the descending half — it is genuinely held/re-articulated twice in a row in the full sequence (ascending steps followed by reversed-ascending steps, concatenated directly, no de-duplication). This matches the source material's own notation, not an implementation quirk.

## Component breakdown

### 1. `AlankarPattern` (pure logic, unit-tested)

```cpp
// src/practice/AlankarPattern.h
#pragma once
#include "../audio/swarmap/SwarMapper.h"
#include <vector>

enum class AlankarPatternId { Alankar1, Alankar2, Alankar3, Alankar4, Alankar5 };

struct AlankarStep
{
    Swar swar;
    int octaveOffset; // 0 = Madhya (same octave as Sa), +1 = Taar, -1 = Mandra
};

class AlankarPattern
{
public:
    explicit AlankarPattern (AlankarPatternId id);

    juce::String name() const;              // "Alankar 1", "Alankar 2", ...
    const std::vector<AlankarStep>& fullSequence() const; // ascending + reversed-ascending, concatenated

private:
    AlankarPatternId id;
    std::vector<AlankarStep> steps; // built once in the constructor from the verified table above
};
```

### 2. `centsFromSaForSwar` (pure function, unit-tested)

Added to `SwarMapper.h`/`.cpp` alongside the existing `swarToString`/`registerToString` free functions — the forward mapping (swar → expected cents-from-Sa) that `SwarMapper` itself doesn't need (it only maps the other direction) but Alankar practice does:

```cpp
// Standard 12-EDO cents relative to Sa, plus octaveOffset*1200.
float centsFromSaForSwar (Swar swar, int octaveOffset);
```

Exact mapping (shuddh swaras are what the v1 pattern library uses; komal/tivra included so the function is total, not partial, over the whole `Swar` enum): Sa=0, ReKomal=100, Re=200, GaKomal=300, Ga=400, Ma=500, MaTivra=600, Pa=700, DhaKomal=800, Dha=900, NiKomal=1000, Ni=1100 — each plus `octaveOffset * 1200`.

### 3. `AlankarPracticeEngine` (pure logic, unit-tested, externally driven)

```cpp
// src/practice/AlankarPracticeEngine.h
#pragma once
#include "AlankarPattern.h"
#include <optional>
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
    float overallTimeInTunePercent;                  // sum(framesInTune) / sum(framesTotal) across all steps, 0 if no frames at all
    std::vector<std::pair<Swar, float>> perSwarTimeInTunePercent; // aggregated across all steps using that swar (any octave), sorted worst-to-best
};

class AlankarPracticeEngine
{
public:
    explicit AlankarPracticeEngine (AlankarPatternId patternId);

    // Called once per elapsed metronome beat (the caller is responsible for
    // calling this exactly once per real beat boundary - see MainComponent's
    // polling loop in the Integration section). Advances to the next step
    // every kBeatsPerStep beats; marks the practice finished once every step
    // in the pattern's full sequence has had its turn.
    void onBeatElapsed();

    // Called for every confident (voiced) pitch frame while practice is
    // active. Does nothing once isFinished() is true.
    void onPitchReading (float centsFromSa);

    bool isFinished() const;
    int currentStepIndex() const; // valid until isFinished()
    float currentStepTargetCents() const; // for the live graph's target band; valid until isFinished()

    AlankarSummary getSummary() const; // valid at any point, including mid-practice for a live partial readout

private:
    static constexpr int kBeatsPerStep = 1;
    static constexpr float kInTuneToleranceCents = 25.0f;

    AlankarPattern pattern;
    int stepIndex = 0;
    int beatsIntoCurrentStep = 0;
    std::vector<AlankarStepResult> stepResults; // sized to pattern.fullSequence().size() at construction, all zeroed
};
```

### 4. `PitchGraphComponent` extension

```cpp
// added to the existing class
void setTargetBand (std::optional<std::pair<float, float>> centsRange); // nullopt clears it
```

`paint()` draws a semi-transparent highlighted horizontal band spanning the given cents range (converted via the existing `centsToY` lambda) — drawn after the swar gridlines but before the live trace, so the trace is always visible on top of the band, matching the existing draw-order convention (gridlines → Sa line → trace).

### 5. `MainComponent` integration

- New members: a Free/Alankar mode toggle, a pattern `ComboBox` (5 items), a "Start Alankar Practice" `TextButton`, a results `Label`, and `std::unique_ptr<AlankarPracticeEngine> alankarEngine` (null when not practicing).
- **Starting practice**: constructs `alankarEngine` for the selected pattern, calls `metronomeSource.setTaal (TaalType::PlainClick)` and `metronomeSource.setEnabled (true)` (auto-starting the pacing click), and records the metronome's current `getTotalBeatsElapsed()` as a baseline.
- **Beat-driven advancement**: `MainComponent`'s existing `timerCallback()` (already polling at a fixed rate for other reasons) also checks whether `metronomeSource.getTotalBeatsElapsed()` has increased since the last poll while `alankarEngine != nullptr` and not finished; for each beat elapsed since the last poll (looping if more than one beat passed between polls), calls `alankarEngine->onBeatElapsed()` once. When `alankarEngine->isFinished()` becomes true, stops the metronome (`setEnabled(false)`) and displays the final summary.
- **Pitch feed**: `pitchWorkerUpdate()` — while `alankarEngine != nullptr` and not finished and the frame carries `centsFromSa` — calls `alankarEngine->onPitchReading(*update.centsFromSa)`, and updates `pitchGraph.setTargetBand({target-25, target+25})` from `alankarEngine->currentStepTargetCents()`.
- **Live readout**: while practicing, the results `Label` shows the running overall time-in-tune percentage from `alankarEngine->getSummary()` (queried live, not just at the end).
- **Switching back to Free mode**: stops the metronome if it was auto-started for practice, clears `alankarEngine`, and clears the graph's target band (`setTargetBand(std::nullopt)`).

## Testing requirements

- `AlankarPattern`: every one of the 5 patterns' full sequences asserted exactly (not spot-checked), confirming both the ascending half and the derived descending half against the verified table above.
- `centsFromSaForSwar`: every one of the 12 `Swar` values asserted at octaveOffset 0, plus at least one non-zero octaveOffset case, confirming the `*1200` term.
- `AlankarPracticeEngine`: step advancement over `onBeatElapsed()` calls (confirms `kBeatsPerStep` timing and that `isFinished()` becomes true at exactly the right beat count for a given pattern's length); time-in-tune accumulation from synthetic `onPitchReading()` calls (in-tolerance vs out-of-tolerance readings, confirming the ±25 cent gate); the zero-frames-in-a-step edge case (a step that gets `onBeatElapsed()`'d past before any `onPitchReading()` arrives reports 0%, not a crash or a wrong percentage); per-swar aggregation across steps that repeat the same swar in different octaves or positions.
- No test file for the `PitchGraphComponent` extension or the `MainComponent` wiring, matching this codebase's established precedent (paint-only components and top-level JUCE wiring are build-verified, not unit-tested) — the pure logic underneath (`AlankarPattern`, `centsFromSaForSwar`, `AlankarPracticeEngine`) carries the real test coverage.

## Open items intentionally deferred (not blocking this spec)

- Session recording, profiles, and analytics — sub-projects 2–4, explicitly out of scope here.
- Patterns 6/7 (the skip-note ornamentation alankars) and any user-authored custom patterns — the "small built-in library" decision covers only the 5 progressive step-size patterns for v1; the `AlankarPatternId` enum and `AlankarPattern` class are structured so adding more later is additive, not a redesign.
- `kBeatsPerStep = 1` is a fixed constant for v1 (one beat per note) — not user-configurable. Effective speed is already controllable via the Metronome's existing BPM slider, so this isn't a missing feature so much as a deliberately un-exposed second knob.
- Komal/tivra swara variants — `centsFromSaForSwar` supports them (the function is total over the whole enum), but no v1 pattern uses them.
