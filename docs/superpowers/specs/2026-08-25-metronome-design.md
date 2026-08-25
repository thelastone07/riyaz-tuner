# Metronome / Taal Engine — Design Spec

**Status:** Approved by user, ready for implementation planning.
**Context:** Third of three remaining v1 pieces (Tanpura, Metronome, Packaging) identified after the pitch-tracking core shipped and was confirmed accurate by the user. Tanpura shipped first (see `2026-08-25-tanpura.md`); this spec covers the metronome/taal practice aid. Packaging is next after this.

## Purpose

A standalone laya (tempo) practice aid: the user picks a taal (or a plain click), sets a tempo, presses start, and hears synthesized clicks that mark the beat cycle — with the traditional sam/clap/khali accent structure audible and visible. It does not depend on pitch calibration or the tanpura; it is usable from app launch.

Out of scope for v1 (unchanged from earlier scoping): tying the metronome to alankar/taan practice scoring, tap-tempo input, saving custom taals, MIDI sync.

## Confirmed taal structures

All beat indices below are 0-indexed. `Sam` = beat 1, always the first beat of the cycle. `Clap` = a vibhag (section) boundary that gets a clap accent. `Khali` = a vibhag boundary that is traditionally silent (a hand-wave, not a clap). `Plain` = every other beat.

| Taal | Beats | Vibhag pattern | Sam | Khali | Clap (other vibhag starts) |
|---|---|---|---|---|---|
| Plain click | 1 | — | every beat treated as `Plain`, no accents, no cycle | — | — |
| Teentaal | 16 | 4+4+4+4 | index 0 | index 8 | indices 4, 12 |
| Jhaptaal | 10 | 2+3+2+3 | index 0 | index 2 | indices 5, 7 |
| Ektaal | 12 | 2+2+2+2+2+2 | index 0 | indices 2, 6 | indices 4, 8, 10 |

User-confirmed during design review, including Ektaal's two khali positions.

**Khali sound (explicit decision):** khali is audible, not silent — a distinct, quieter/duller timbre from a clap. This is a deliberate departure from pure tradition (real khali is a silent hand-wave) made because this is a solo practice aid with no teacher/group to follow visually; losing the audio cue entirely on that beat would make it harder to use, not more authentic in a way that helps. The visual beat indicator still marks khali distinctly too.

## Component breakdown

Four new components, following the codebase's established layering (pure-logic → pure-DSP → JUCE-AudioSource wrapper → UI), the same shape as the Tanpura module's `KarplusStrongString` → `TanpuraSynth` → `TanpuraAudioSource` chain.

### 1. `TaalPattern` (pure logic, no JUCE audio dependency)

```cpp
enum class TaalType { PlainClick, Teentaal, Jhaptaal, Ektaal };
enum class BeatType { Sam, Clap, Khali, Plain };

class TaalPattern
{
public:
    explicit TaalPattern (TaalType type);
    int beatCount() const;
    BeatType classify (int beatIndex) const; // beatIndex must be in [0, beatCount()) - jassert, contract violation otherwise
private:
    TaalType type;
};
```

Stateless besides which taal it was constructed for. The classify() tables are exactly the confirmed table above, hardcoded (no need for a generic vibhag-pattern parser — four taals, fixed forever, is not worth generalizing per YAGNI).

### 2. `MetronomeClick` (pure DSP, no delay line — simpler than `KarplusStrongString`)

```cpp
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
    float decayPerSample = 0.0f;
    int samplesRemaining = 0;
};
```

A damped sine burst — oscillator phase advances each sample, amplitude decays exponentially, `samplesRemaining` counts down to 0. No delay line, no `std::vector`, no allocation of any kind — there is nothing to reserve, unlike `KarplusStrongString`. **Duration and decay rate must be computed from `sampleRateIn` at `trigger()` time, never a baked-in sample-rate constant** — this is the direct lesson from the Tanpura final review's `kMaxRingSamples` finding (a `44100`-baked duration silently became a much shorter real-time window at higher sample rates).

Per-`BeatType` timbre (exact frequencies/durations/amplitudes are the implementer's to tune within these constraints, verified by the pitch test below):
- `Sam`: highest frequency, loudest, slightly longer — the most prominent beat.
- `Clap`: medium frequency, medium volume.
- `Khali`: distinctly lower frequency and/or quieter — audible but clearly different from a clap, never confusable with `Plain`.
- `Plain`: short, soft tick, lowest amplitude of the four.

### 3. `MetronomeAudioSource` (JUCE `AudioSource` wrapper)

```cpp
class MetronomeAudioSource : public juce::AudioSource
{
public:
    void setBpm (float newBpm);
    void setTaal (TaalType newType);
    void setEnabled (bool shouldBeEnabled);
    int getCurrentBeatIndex() const; // for the UI's polling Timer

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override; // required by the AudioSource interface, never called by this app (see Integration below) - implement as clearActiveBufferRegion() followed by addNextAudioBlock(bufferToFill), so if anything ever does call it (e.g. a future MixerAudioSource refactor), it correctly satisfies AudioSource's overwrite convention in terms of the additive primitive, rather than being a dead or incorrect stub

    void addNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill); // the real render path - ADDS into the buffer, never overwrites
private:
    // ...
};
```

State and threading, following the exact lock-free pattern `TanpuraAudioSource` already established:

- `std::atomic<float> bpm` — read fresh at every beat boundary (not continuously mid-beat, so a mid-beat tempo change doesn't warp the beat currently in progress). No "pending" flag needed; unlike a taal change, a tempo change has nothing to reset.
- `std::atomic<TaalType> pendingTaalType` + `std::atomic<bool> taalChangePending` — consumed once (`exchange(false)`) at the top of `addNextAudioBlock()`, exactly like `TanpuraAudioSource`'s `retunePending`. Rebuilds the active `TaalPattern` and resets the beat clock to beat 0 — switching taal mid-cycle to one with fewer beats must never leave `currentBeatIndex` out of range.
- `std::atomic<bool> resetPending` — set whenever `setEnabled(true)` transitions from stopped to running (pressing Start always restarts at beat 0/Sam, standard metronome UX), consumed the same way.
- `std::atomic<bool> enabled` — gates rendering. While disabled, `addNextAudioBlock()` contributes nothing and the beat clock does not advance (so Stop-then-Start always resumes at beat 0, not wherever it left off).
- `std::atomic<int> currentBeatIndexForUi` — written by the audio thread every time a new beat starts; read by the UI's polling `Timer`. Single-writer/single-reader, no tear risk for a plain `int`.

`addNextAudioBlock()` renders sample-by-sample: at each beat boundary, look up `pattern.classify(currentBeatIndex)`, `click.trigger(...)`, publish `currentBeatIndex` to the UI atomic; every sample, `buffer.addSample(channel, index, click.renderNextSample())` for every channel — **`addSample`, never `setSample`** — this is what makes it additive rather than overwriting.

No scratch buffer is needed: because this is a custom method (not the `AudioSource::getNextAudioBlock` override, which by JUCE convention overwrites), each sample can be added directly into the target buffer as it's generated.

### 4. UI: controls + beat indicator

- BPM slider (range 20–300, matches the range from vilambit to fast drut laya), default 80.
- Taal `ComboBox` (Plain / Teentaal / Jhaptaal / Ektaal).
- Start/Stop toggle button.
- `BeatIndicatorComponent` — a row of lights, one per beat in the current cycle (re-derives its own `TaalPattern` from the combo box's current selection, message-thread only — it does not need the audio thread to tell it what pattern is active, only which beat is current). Colored/sized by `BeatType` (matching the four timbres' relative prominence). Driven by a `juce::Timer` at ~30Hz polling `metronomeSource.getCurrentBeatIndex()` and repainting. For `PlainClick` (beat count 1), this degrades to a single pulsing dot rather than a row.

## Integration into `MainComponent`

- New member: `MetronomeAudioSource metronomeSource;`, owned directly like `tanpuraSource` (no sample-rate construction dependency).
- `prepareToPlay()`: call `metronomeSource.prepareToPlay(...)` alongside `tanpuraSource.prepareToPlay(...)`, before the pitch-path setup — same reasoning as the tanpura wiring: `getNextAudioBlock()` will call it unconditionally on every path, including the ones where `engine.prepare()` fails.
- `getNextAudioBlock()`: after the existing `tanpuraSource.getNextAudioBlock(bufferToFill)` call (which overwrites), add exactly one new line: `metronomeSource.addNextAudioBlock(bufferToFill)`. Order matters — it must come after the tanpura overwrite, not before, or its contribution would itself be overwritten.
- `releaseResources()`: call `metronomeSource.releaseResources()` alongside the existing calls, for the same re-entrant-`prepareToPlay`-safety reasons already established.
- No dependency on `worker`/`pipeline`/calibration state anywhere — the metronome is fully independent of the pitch-tracking path, wired in parallel to it, not through it.

## Real-time safety constraints (binding, carry into the implementation plan verbatim)

1. No heap allocation on the audio thread, ever — `MetronomeClick` needs none by construction (no buffers); double-check nothing sneaks one in (e.g. no `std::vector` for the oscillator).
2. No locks (`CriticalSection`, etc.) on the hot path — atomics only, `exchange()` for consume-once semantics, plain loads/stores otherwise.
3. No sampled/recorded audio — every sound is synthesized. (Not a licensing concern for a plain click the way the tanpura was, but consistency with the established project constraint is deliberate.)
4. Any duration or sample-count derived from a sample rate must be computed from the *actual* sample rate passed in at the relevant call (`prepareToPlay`, `trigger`), never a baked-in constant — this is the direct, named lesson from the Tanpura final review's Important #1 and #3 findings.
5. `addNextAudioBlock()` must only ever add into the buffer (`AudioBuffer::addSample`), never overwrite (`setSample`) — the tanpura's overwrite happens first in `MainComponent`, and this component runs after it.

## Testing requirements (binding, carry into the implementation plan verbatim)

Direct lesson from the Tanpura final review's Important #4: **every synthesis component needs at least one test that verifies its actual output (frequency, timing), not just silence-vs-non-silence** — that gap is exactly what let two Important tuning/truncation bugs ship unnoticed through three task reviews last time.

- `TaalPatternTests.cpp`: `beatCount()` and `classify()` for all four taal types, asserting the exact table above (every Sam/Clap/Khali index by name, not just spot-checked).
- `MetronomeClickTests.cpp`: non-silent immediately after `trigger()`; decays to silence within the intended duration at the *actual* sample rate passed to `trigger()` (not just 44100 — run at 44100/48000/96000 per the sample-rate-coverage requirement below); **a frequency-accuracy test** (Goertzel or equivalent, matching the pattern used to fix the Tanpura pitch-test gap) confirming each `BeatType`'s burst is centered near its intended frequency, not just "made noise."
- `MetronomeAudioSourceTests.cpp`: disabled source contributes nothing (prefill the buffer with a known nonzero value and confirm it is *preserved*, proving additive-not-overwrite semantics from day one — the Tanpura review flagged this exact gap as only "incidentally tested" there); taal change resets beat index to 0 at the next block; `setEnabled(true)` resets beat index to 0 (Stop-then-Start always restarts at Sam); BPM change takes effect without needing a re-enable; beat index advances at the correct sample cadence for a controlled synthetic scenario (e.g. 44100Hz @ 80 BPM → 33,075 samples/beat, verified directly); `PlainClick` never accents (`classify()` always returns `Plain`).
- **Sample-rate coverage is mandatory, not optional**: at minimum the click-decay and pitch-accuracy tests must run at 44100, 48000, and 96000 Hz. This is what would have caught both sample-rate-dependent Tanpura bugs, and the metronome's click-duration logic is structurally the same kind of code.

## Open items intentionally deferred (not blocking this spec)

- Tap-tempo, custom taal editing, MIDI sync — out of scope for v1, noted above.
- Whether the beat indicator should also show while a taal cycle is running which vibhag it's in (beyond per-beat classification) — the per-beat coloring already conveys this implicitly (vibhag starts are `Clap`/`Khali`/`Sam`); no separate vibhag-grouping visual is planned unless it turns out to read poorly in practice.
