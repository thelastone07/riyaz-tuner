# Profiles + Persistent Storage — Design Spec

**Status:** Approved by user, ready for implementation planning.
**Context:** Second of four sub-projects (Alankar practice mode, Profiles + persistent storage, Accuracy scoring + session recording, Analytics view) decomposed from a single large feature request. Alankar practice mode is complete. This sub-project adds a profile picker shown before calibration begins, letting the user reuse a saved Sa instead of recalibrating every launch — and lays the persistent-storage foundation the next two sub-projects build on.

## Purpose

Today, every launch runs a fresh Sa calibration with no memory of who's practicing or what their tonic was last time. This adds: a profile (a name + a saved Sa) picked before anything else happens, the choice to reuse that saved Sa (skipping calibration entirely) or recalibrate fresh, and a small persistent JSON file so profiles survive across launches.

**User-confirmed decisions from design review:**
- The picker always shows on launch — no auto-resume of a "last used" profile. Simple and predictable.
- Creating a new profile asks for the name first, then runs the existing calibration flow under that name.
- v1 supports create + select only — no rename or delete. Avoids building UI for a need that may not materialize.
- Reusing a saved Sa does **not** auto-start the tanpura (unlike a fresh calibration, which does today) — reuse is treated as a deliberately different, quieter path.

## Architecture decision: window-content swap, not an in-`MainComponent` overlay

`riyaaz_app` deliberately does not link JUCE's modal-loop APIs (`JUCE_MODAL_LOOPS_PERMITTED` is scoped `PRIVATE` to `riyaaz_tests` only — see `CMakeLists.txt`), so a genuinely blocking modal picker shown before `MainComponent` exists is not an option without reversing that earlier decision.

Instead: `MainWindow` (in `src/app/Main.cpp`) initially sets its content to a new `ProfilePickerComponent`, not `MainComponent`. Once the picker resolves (a profile name + optionally a known Sa to reuse), its callback calls `setContentOwned` a second time on the same `DocumentWindow`, swapping in a real `MainComponent` — now constructed *with* the resolution already known via two new constructor parameters. This means `MainComponent`'s own internals barely change: it doesn't need to defer `setAudioChannels()` or manage an internal "still showing the picker" state machine. The one new behavior is entirely inside `prepareToPlay()`: conditionally skip calibration based on a constructor-provided value. All the screen-sequencing complexity is isolated to `Main.cpp`, which is currently a small, simple file — the right place to absorb it.

## Component breakdown

### 1. `UserProfile` + `ProfileStore` (pure logic, unit-tested)

```cpp
// src/profile/ProfileStore.h
#pragma once
#include <juce_core/juce_core.h>
#include <vector>

struct UserProfile
{
    juce::String name;
    float savedSaHz;
};

// Reads/writes a JSON array of profiles to a fixed file. The target file is
// dependency-injected (a constructor parameter) so tests use a temp file,
// never the real user-data location.
class ProfileStore
{
public:
    explicit ProfileStore (juce::File storageFileIn);

    // Empty if the file doesn't exist (first launch ever) or is malformed
    // (defensive - a corrupted settings file should degrade to "no
    // profiles", not crash the app). A genuinely corrupted file is an
    // accepted, documented v1 risk: this store always does a full
    // read-modify-write, so a save() after a "silently empty" read of a
    // corrupted file would overwrite it with only the newly-created
    // profile, losing whatever was unreadable. Given the tiny amount of
    // data involved (a handful of name+Hz pairs) and the personal, local
    // nature of this app, no backup/versioning is built for v1.
    std::vector<UserProfile> loadAll() const;

    // Upserts by name: replaces an existing profile with the same name, or
    // appends if none matches. Callers that want to PREVENT an accidental
    // overwrite (the "New Profile" creation flow) must check loadAll() for
    // a name collision themselves before calling this - save() itself
    // always upserts unconditionally.
    void save (const UserProfile& profile);

private:
    juce::File storageFile;
};
```

Storage location (set by the real call site, not by `ProfileStore` itself — the class only knows the `juce::File` it's given): `juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Riyaaz").getChildFile ("profiles.json")` — the standard, correct place for persistent per-user app data on Windows, independent of where the executable lives or what the working directory is.

JSON shape: a flat array, e.g. `[{"name": "Alex", "saHz": 220.0}, {"name": "Sam", "saHz": 196.3}]`. Implemented via `juce::JSON::parse`/`juce::JSON::toString` over a `juce::var` array — no new JUCE module dependency, `juce::JSON` lives in `juce_core`, already linked everywhere.

### 2. `PitchPipeline::startLiveWithKnownSa` (extends the existing, tested `PitchPipeline`)

```cpp
// added to the existing class
// Skips calibration entirely and starts directly in Live phase at a known
// Sa - used when a profile's saved Sa is being reused. Must be called
// before the first process() call (this is not a live re-tuning
// mechanism - see restartCalibration() for changing Sa mid-session).
void startLiveWithKnownSa (float knownSaHz);
```

Implementation sets `phase = PitchPipelinePhase::Live; saHz = knownSaHz;` directly and calls the existing `swarMapper.reset()` (redundant given a fresh `PitchPipeline` already has a fresh `SwarMapper`, but cheap and makes the intent explicit rather than relying on that invariant silently). `TonicCalibrator` is still constructed as a data member (unavoidable without restructuring `PitchPipeline`'s member layout) but is never touched on this path. Purely additive — the existing constructor, `process()`, and `restartCalibration()` are untouched.

Once called, the pipeline's very next `process()` call (on real audio, via the normal `PitchWorker` → `pipeline.process()` path — nothing about worker/audio wiring changes) routes straight to the existing `handleLive()`, so real pitch tracking against the known Sa starts on the first block of audio, with zero calibration frames consumed.

### 3. `ProfilePickerComponent` (UI, build-verified only — matches established precedent for top-level screens)

```cpp
// src/profile/ProfilePickerComponent.h
#pragma once
#include "ProfileStore.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <optional>

class ProfilePickerComponent : public juce::Component
{
public:
    // Fired exactly once, when a choice is made. chosenSaHz is nullopt for
    // "(re)calibrate" (both a brand new profile, and an existing profile's
    // "Recalibrate" action); it holds the saved Sa for "Use saved Sa".
    std::function<void (juce::String profileName, std::optional<float> chosenSaHz)> onResolved;

    explicit ProfilePickerComponent (ProfileStore& storeIn);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    ProfileStore& store;
    // One row per existing profile (a Label showing "name — Sa Hz", plus
    // "Recalibrate" and "Use saved Sa" buttons), built once in the
    // constructor from store.loadAll() - the list does not refresh live,
    // since nothing else can modify the store while this component is
    // showing. A name TextEditor + "Create" button for a new profile.
    // Creating with a name that collides with an existing profile is
    // rejected (an inline message, not a silent overwrite) - checked
    // against store.loadAll() before firing onResolved.
};
```

Loads existing profiles once at construction, renders one row per profile plus a new-profile name field. Not designed to scroll — v1 accepts a practical limit of a handful of profiles fitting the fixed window without scrolling, reasonable for a personal practice app.

### 4. `MainComponent` — minimal constructor change, one new `prepareToPlay()` branch

```cpp
// changed
explicit MainComponent (const juce::String& profileNameIn, std::optional<float> knownSaHzIn);
```

New members: `juce::String activeProfileName;` (set from the constructor param), `std::optional<float> pendingKnownSaHz;` (ditto), `bool suppressNextTanpuraAutoStart = false;`, `ProfileStore profileStore { <the same standard path> };`.

In `prepareToPlay()`, immediately after `pipeline = std::make_unique<PitchPipeline> (engine, sampleRate);`: if `pendingKnownSaHz.has_value()`, call `pipeline->startLiveWithKnownSa (*pendingKnownSaHz)` and set `suppressNextTanpuraAutoStart = true`. Everything else in `prepareToPlay()` — `worker` construction, `worker->start()` — is unchanged and unconditional, exactly as today.

In `pitchWorkerUpdate()`'s existing `justBecameLive` branch: if `suppressNextTanpuraAutoStart` is true, consume it (set false) and skip the existing `tanpuraSource.setSa(...); tanpuraSource.setEnabled(true);` calls. Otherwise (a real calibration just succeeded — whether for a brand-new profile or an existing one's "Recalibrate"), run the tanpura auto-start exactly as today, **and additionally** call `profileStore.save ({ activeProfileName, update.saHz })` — this is what persists a freshly-calibrated Sa for future reuse, for both new and recalibrated profiles alike. The reuse path never calls `save()` (nothing changed, no point rewriting the file).

### 5. `Main.cpp` — the two-stage window content swap

```cpp
// MainWindow's constructor, restructured
explicit MainWindow (const juce::String& name)
    : DocumentWindow (name, ..., DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);

    auto* picker = new ProfilePickerComponent (profileStore);
    picker->onResolved = [this] (juce::String profileName, std::optional<float> knownSaHz)
    {
        // Deferred via callAsync, not called synchronously: onResolved
        // fires from inside one of the picker's own button click handlers
        // (a member function call still executing on that Component).
        // Calling setContentOwned() synchronously here would delete the
        // picker (the previous content) while it is still on the call
        // stack of its own callback - a self-destruction-during-callback
        // hazard this codebase has been careful to avoid elsewhere (see
        // MainComponent's SafePointer/callAsync usage around
        // prepareToPlay()). Deferring one message-loop turn lets the
        // click handler unwind completely first.
        juce::MessageManager::callAsync ([this, profileName, knownSaHz]
        {
            setContentOwned (new MainComponent (profileName, knownSaHz), true);
            centreWithSize (getWidth(), getHeight());
        });
    };
    setContentOwned (picker, true);

    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}
```

`MainWindow` gains a `ProfileStore profileStore { <standard path> };` member, constructed once and passed to the picker by reference (the picker only reads from it; `MainComponent` owns its own separate `ProfileStore` instance pointed at the same file for its own later `save()` calls — no shared mutable state needed, since `ProfileStore` does no in-memory caching and each `loadAll()`/`save()` is a fresh, independent file read/write).

Note `MainWindow` itself has no `SafePointer`/lifetime hazard here the way `MainComponent`'s own `callAsync` uses need one: `MainWindow` owns the picker (via `setContentOwned`) and outlives it deterministically — nothing external can destroy `MainWindow` between the click and the deferred callback firing except full application shutdown, which already tears down the whole `JUCEApplication` (and therefore the pending message) together. A plain `[this]` capture is safe here for that reason; it would not be if some other, external owner could delete `MainWindow` independently.

## Testing requirements

- `ProfileStore`: unit-tested against an injected temp file — round-trips a saved profile through `loadAll()`, upserts an existing name rather than duplicating it, returns an empty list for a missing file and for a malformed one (write garbage bytes and confirm no crash), and confirms a fresh `ProfileStore` pointed at the same file sees a previous instance's `save()`.
- `PitchPipeline::startLiveWithKnownSa`: extends the existing `PitchPipelineTests.cpp` — construct a pipeline, call `startLiveWithKnownSa(220.0f)`, call `process()` once with synthetic audio, and confirm the returned `PitchPipelineUpdate` reports `phase == Live` and `saHz == 220.0f` immediately, with no `Calibrating`-phase update ever observed.
- `ProfilePickerComponent`, the `MainComponent` constructor change, and `Main.cpp`'s window-swap logic: build-verified only (`riyaaz_app` links and launches), matching established precedent for top-level JUCE wiring in this codebase.

## Open items intentionally deferred (not blocking this spec)

- Rename/delete profiles — explicitly out of v1 scope per user decision.
- Auto-resuming the last-used profile — explicitly out of v1 scope; the picker always shows.
- A mid-session "switch profile" affordance inside `MainComponent` — not asked for; the picker is a once-per-launch gate.
- Scrolling/pagination in `ProfilePickerComponent` for a large number of profiles — accepted v1 limitation given this is a personal practice app.
- Backup/versioning for `profiles.json` against corruption — accepted v1 risk given the tiny amount of data and local, personal nature of the app.
- Session recording and analytics — sub-projects 3 and 4, which this sub-project's persistent-storage foundation exists to support, but doesn't itself build.
