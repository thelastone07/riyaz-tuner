# Accuracy Scoring + Session Recording — Design

**Status:** Approved for planning
**Sub-project:** 3 of 4 (Alankar practice mode → Profiles → **Accuracy scoring + session recording** → Analytics view)

## Goal

Record a completed Alankar practice run's accuracy so a later Analytics view (sub-project 4) can show whether the user is improving over time. Two things must happen first: the existing accuracy math in `AlankarPracticeEngine` has a known approximation (frame-count weighting, delivery-time attribution) that should be corrected *before* anything is persisted on top of it, and only fully completed runs should be recorded — never partial/cancelled ones.

## Background

`AlankarPracticeEngine::onPitchReading(float centsFromSa)` currently counts each delivered pitch-worker update as one "frame," crediting it to whichever step is current when it arrives. This was flagged as a known, deliberate approximation during the Alankar final review (see `TODOS.md`) and left as-is because fixing it before persistence exists is cheaper than after. This spec is that fix, plus the new persistence layer.

## Section 1: Accuracy-measurement fix

Two independent problems, one fix:

1. **Frame-count weighting.** Pitch-worker updates arrive at an irregular rate (governed by CREPE inference latency, FIFO coalescing, and message-thread scheduling), not at a fixed interval. Counting "+1 per delivered update" as a proxy for "+1 unit of time" over-weights steps that happened to receive more updates, independent of how long they actually lasted.
2. **Delivery-time attribution.** `onPitchReading()` carries no timestamp today, so a reading is credited to whatever step is current *when it arrives* — not when the underlying audio was actually sung. Inference latency plus the async delivery hop mean the tail of one step's audio routinely lands after the next step has already started, misattributing it.

**Rejected alternative:** re-deriving step timing entirely from elapsed pipeline-time (removing the dependency on the metronome's own beat clock). This was considered and rejected — it would either lose today's "changing BPM mid-practice speeds up the whole pattern" behavior, or require duplicating the BPM-change-at-next-boundary logic the metronome already gets right. Not worth the churn for a personal practice app.

**Chosen fix:** widen both entry points to carry the pipeline's own capture timestamp (`PitchPipelineUpdate::timestampMs`, already plumbed to the call site in `MainComponent::pitchWorkerUpdate()` one line above where `onPitchReading()` is called today), and use it for both time-weighting and correct attribution.

### `AlankarStepResult` / `AlankarSummary` field rename

`framesInTune`/`framesTotal` (int, a count) become `msInTune`/`msTotal` (`uint64_t`, a duration) to make the new units honest. No other field changes; `getSummary()`'s aggregation logic (sum/sum × 100) is structurally identical, just renamed.

```cpp
struct AlankarStepResult
{
    Swar swar = Swar::Sa;
    int octaveOffset = 0;
    uint64_t msInTune = 0;
    uint64_t msTotal = 0; // 0 if no confident pitch update was ever attributed to this step
};

struct AlankarSummary
{
    std::vector<AlankarStepResult> perStep;
    float overallTimeInTunePercent = 0.0f;   // sum(msInTune) / sum(msTotal) * 100, genuinely time-weighted now
    std::vector<std::pair<Swar, float>> perSwarTimeInTunePercent; // worst-to-best
};
```

### `AlankarPracticeEngine` interface changes

```cpp
void onBeatElapsed (uint64_t latestKnownTimestampMs);
void onPitchReading (float centsFromSa, uint64_t timestampMs);
```

New private state:

```cpp
uint64_t currentStepStartTimestampMs = 0; // 0 = "no boundary recorded yet" (still in step 0)
uint64_t lastReadingTimestampMs = 0;       // 0 = "no reading processed yet"
static constexpr uint64_t kMaxReadingGapMs = 250; // caps the weight of a stall/pause; longer than any realistic hop-to-hop gap under normal load
```

`onBeatElapsed`: unchanged step-advancement logic, but when it advances `stepIndex`, it also records `currentStepStartTimestampMs = latestKnownTimestampMs`.

`onPitchReading`:
1. Determine the target step: if `currentStepStartTimestampMs != 0 && timestampMs < currentStepStartTimestampMs`, this reading was captured before the current step began (a late-arriving straggler) — target `stepIndex - 1` instead of `stepIndex`. Otherwise target `stepIndex` (or, if finished, this straggler rule is what lets a completed run still credit the last step's tail readings — see below).
2. If the target index is out of `[0, totalSteps())`, return — nothing valid to attribute to (the very first reading before any step has started, or no eligible late step at the tail of a finished run).
3. Compute the weight: if `lastReadingTimestampMs == 0` (very first reading ever) or `timestampMs <= lastReadingTimestampMs` (non-advancing/out-of-order), weight is 0 (contributes nothing — acceptable given it's at most one sample out of many over a real run). Otherwise weight is `min(timestampMs - lastReadingTimestampMs, kMaxReadingGapMs)`. Update `lastReadingTimestampMs = timestampMs` regardless.
4. Add the weight to the target step's `msTotal`, and to `msInTune` too if within `kInTuneToleranceCents`.

This also fixes a secondary today-bug: `isFinished()` currently makes `onPitchReading()` return immediately, silently dropping the last step's tail readings entirely. Restructuring around "compute target step, bail only if invalid" (rather than "bail if `isFinished()`") means a finished engine can still credit late readings to its last real step (`totalSteps() - 1`).

### Call site changes (`MainComponent.cpp`)

- `pitchWorkerUpdate()`: `alankarEngine->onPitchReading (*update.centsFromSa);` → `alankarEngine->onPitchReading (*update.centsFromSa, update.timestampMs);`
- `timerCallback()`: `alankarEngine->onBeatElapsed();` → `alankarEngine->onBeatElapsed (lastUpdate.timestampMs);` (`lastUpdate` is already updated to the latest `PitchPipelineUpdate` every `process()` call, so by the time `timerCallback()` polls for a new beat, `lastUpdate.timestampMs` is the most recent known pipeline timestamp.)

No change to the metronome, the beat-lock logic (`setAlankarPracticeControlsLocked`), or any UI.

## Section 2: Session data model + storage

A new `SessionStore`, mirroring `ProfileStore`'s established pattern: JSON array in a fixed file, dependency-injected `juce::File` for testability, a standalone standard-location helper.

```cpp
// src/profile/SessionStore.h
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <utility>

struct AlankarSessionRecord
{
    juce::String profileName;
    juce::String patternName;                 // AlankarPattern::name(), e.g. "Alankar 1"
    int startingBpm = 0;                       // BPM slider value at the moment Start was pressed
    juce::int64 completedAtEpochMs = 0;        // juce::Time::getCurrentTime().toMilliseconds()
    float overallTimeInTunePercent = 0.0f;
    std::vector<std::pair<juce::String, float>> perSwarTimeInTunePercent; // swar name (swarToString), worst-to-best
};

// Reads/writes a JSON array of completed Alankar practice sessions to a
// fixed file, shared across all profiles on this machine (each record
// carries its own profileName). Append-only - unlike ProfileStore there is
// nothing to upsert; every completed run is a new, independent record.
class SessionStore
{
public:
    explicit SessionStore (juce::File storageFileIn);

    // Filtered to one profile's records, oldest-first (file order). Empty if
    // the file doesn't exist, is malformed, or the profile has no sessions
    // yet - same "degrade to empty, never crash" contract as ProfileStore.
    std::vector<AlankarSessionRecord> loadAllForProfile (const juce::String& profileName) const;

    void append (const AlankarSessionRecord& record);

private:
    juce::File storageFile;
};

// %APPDATA%\Riyaaz\sessions.json on Windows - alongside profiles.json.
juce::File getStandardSessionStoreFile();
```

**JSON shape** (one object per record, matching `ProfileStore`'s flat-object convention):

```json
[
  {
    "profileName": "Riyaaz",
    "patternName": "Alankar 1",
    "startingBpm": 60,
    "completedAtEpochMs": 1798675200000,
    "overallTimeInTunePercent": 82.5,
    "perSwar": [ { "swar": "S", "percent": 71.0 }, { "swar": "R", "percent": 90.0 } ]
  }
]
```

`append()` always does a full read-modify-write (load all records regardless of profile, add the new one, write the whole array back) — the same tradeoff `ProfileStore::save()` already accepts, justified the same way: tiny amount of data, personal/local app, no backup/versioning needed for v1.

No history cap or pruning in v1 — YAGNI; the Analytics sub-project can revisit if the file ever grows large enough to matter, which is unlikely for personal practice use.

## Section 3: Wiring into `MainComponent`

New members:

```cpp
SessionStore sessionStore { getStandardSessionStoreFile() };
int alankarSessionStartingBpm = 0;
juce::String alankarSessionPatternName;
```

**Capturing session metadata at Start** (`alankarStartButton.onClick`, alongside the existing `alankarEngine = std::make_unique<AlankarPracticeEngine> (...)` construction):

```cpp
alankarSessionStartingBpm = (int) metronomeBpmSlider.getValue();
alankarSessionPatternName = AlankarPattern (kAlankarPatternIds[(size_t) selectedIndex]).name();
```

**Recording on completion** (`timerCallback()`'s existing `alankarEngine->isFinished()` branch, after `summary` is computed): build an `AlankarSessionRecord` from `activeProfileName`, `alankarSessionPatternName`, `alankarSessionStartingBpm`, `juce::Time::getCurrentTime().toMilliseconds()`, and `summary` (converting each `perSwarTimeInTunePercent` entry's `Swar` to a `juce::String` via the existing `swarToString()`), then `sessionStore.append (record)`.

**Scope of "completed runs only" / "Alankar mode only":** `cancelAlankarPractice()` (the path used both when leaving Alankar mode and when calibration is lost mid-run) is untouched — it never calls into `sessionStore`, so a cancelled or interrupted run records nothing. Normal (non-Alankar) mode has no code path into `sessionStore` at all — there is nothing to guard against, since the only call to `sessionStore.append()` anywhere in the codebase lives inside the Alankar-finish branch.

## Testing

- `AlankarPracticeEngineTests.cpp`: update existing tests for the renamed `msInTune`/`msTotal` fields and the new `(float, uint64_t)` / `(uint64_t)` signatures. Add cases for: time-weighting (two readings with a known gap produce a proportional `msTotal`), the gap cap (a large gap contributes at most `kMaxReadingGapMs`), late-straggler attribution (a reading timestamped before the recorded step-start lands in the previous step), and tail-of-finished-run attribution (a late reading after the final `onBeatElapsed` call still lands in the last step, not dropped).
- New `SessionStoreTests.cpp`, mirroring `ProfileStoreTests.cpp`: round-trip a record through `append()`/`loadAllForProfile()`, verify multi-profile filtering, verify missing/malformed-file degrades to empty, verify multiple `append()` calls accumulate rather than overwrite.
- Manual smoke test: run a full Alankar practice to completion in the built app, confirm `%APPDATA%\Riyaaz\sessions.json` gains one new record with sane values.

## Out of scope (deferred to sub-project 4)

Any UI to view session history (the Analytics view itself), pruning/capping session history, and any richer per-swar or per-attempt visualization.
