# TODOS

Deferred, non-blocking items. Not urgent, but shouldn't get silently lost.

## From the Alankar Practice Mode final-review fix wave (2026-08-28)

Deferred out of the fix wave that closed 1 Critical + 2 Important + 4 Minor
findings from the whole-branch alankar-practice review (commits
`bcde084..a12fa9e`). Grouped by what future work each belongs to.

**For the next sub-project (Accuracy scoring + session recording), which the
spec commits to reusing `AlankarSummary` verbatim:**

- **`AlankarPracticeEngine::onPitchReading()` attributes frames by delivery
  time, not capture time, and `framesTotal` counts delivered pitch-worker UI
  updates rather than elapsed time.** This wave only corrected the doc
  comments to describe this honestly (`src/practice/AlankarPracticeEngine.h`)
  — the underlying model is unchanged. Before persisting this data, widen
  `onPitchReading(float centsFromSa, uint64_t timestampMs)` (the caller
  already has `update.timestampMs` at the call site) and use it to attribute
  frames to the step that was actually current at capture time, not
  delivery time — a real design question for that sub-project's own
  brainstorming, not a one-line fix.
- **Test-coverage gaps that only show at the whole-plan level, not any one
  task's scoped review:** no test exercises `onBeatElapsed()`/
  `onPitchReading()`'s no-op guards on an already-finished engine, nor
  `getSummary()` on a finished engine — which is precisely the state
  `MainComponent` parks the engine in for the rest of a session, so it's
  production-reachable, not theoretical. There's also no structural
  assertion of the reversal invariant itself (`seq[i] == seq[2N-1-i]` and
  `size() == 2 * ascendingSize`, asserted in a loop over all five pattern
  ids) — the five literal per-pattern tests cover today's data well, but a
  structural test is what would catch a *sixth* pattern (which the spec
  explicitly anticipates as "additive, not a redesign") shipping with a
  mis-built descending half.

**Narrow, accepted residuals (not worth fixing given how rarely they're
reachable):**

- **A device-change-triggered `prepareToPlay()` on `MetronomeAudioSource`
  still arms an unabsorbed reset**, which would silently advance an active
  Alankar practice by one step, the same defect class the Critical finding
  fixed for the two user-reachable paths (pause/resume via the metronome
  button, changing the taal combo mid-practice — both now closed by locking
  those controls while a practice is live). This third path can't be closed
  by disabling UI controls, since a device change isn't a UI action. Matches
  this codebase's existing precedent for device-switch edge cases (no
  device-switch UI exists anywhere in v1).
- **A microsecond-scale start race**: if the metronome was already running
  manually when "Start Alankar Practice" is pressed, a naturally-occurring
  beat could land between reading `getTotalBeatsElapsed()` and arming the
  reset via `setTaal()`, getting absorbed as the "first beat" while the
  reset's own beat gets forwarded — skipping one step. Same shape as an
  already-accepted one-audio-block residual from the Metronome plan.
- **`cancelAlankarPractice()` doesn't reset `alankarAwaitingFirstBeat`** —
  harmless today (the only consumer is gated on a non-null engine, and
  `onClick` always re-arms it before use), but leaves a member stale after
  teardown. Worth a one-line cleanup if that function is touched again.
- **`pitchWorkerUpdate()` still calls `pitchGraph.setTargetBand()`
  unconditionally on every voiced frame**, not just on step change (the
  fix wave's step-change guard only covers `timerCallback()`'s path, per
  the finding's literal scope). Negligible in practice — `addPoint()` two
  lines earlier already triggers a repaint, and JUCE coalesces both into
  one frame — but worth folding into the same guard if that function is
  touched again.
- **Taar Sa lost its implicit "top rail" visual reference** when
  `PitchGraphComponent`'s `kCentsRange` widened from 1200 to 1400 to stop
  clamping the target band at the octave boundary (the actual bug fixed).
  The peak note now floats at 85.7% of half-height with blank space above
  it. Widening the swar-gridline loop from `-1100..1100` to `-1300..1300`
  would restore a reference line at Taar Sa without reintroducing the clamp
  bug.
- **The metronome-controls lock (added to fix the Critical) could
  theoretically persist if the audio device stops without a subsequent
  `prepareToPlay()`** — `releaseResources()` doesn't cancel an active
  practice, so if no more audio blocks ever render, the practice can never
  reach its finish branch to unlock the controls. Narrow, and there's an
  escape hatch: switching `modeCombo` back to "Free practice" always calls
  `cancelAlankarPractice()`, which unlocks unconditionally.
- **Pressing "Start Alankar Practice" again while a practice is already
  live silently discards the in-progress run and restarts from step 0** —
  this is handled *correctly* (all relevant state is properly reset), but
  there's no confirmation and no UI cue that a run was discarded. Ties
  together with a broader observation: the feature currently has three
  logical states (idle / practicing / finished) represented only as
  "engine exists or not" plus scattered booleans, rather than one explicit
  state. An explicit small enum, with UI gated on it, would be a cleaner
  foundation than adding more scattered guards as more edge cases surface.
- **The live "% in tune so far" readout can be up to ~3 seconds stale** at
  the BPM slider's 20 BPM floor, since it now only refreshes on step
  boundaries (fixed to stop 30Hz allocation/repaint churn with no visible
  change most of the time). Acceptable given it's a whole-run running
  aggregate that barely moves per step after the first few, but worth
  knowing if it ever feels sluggish at very slow tempos.

## From the Metronome final-review fix wave (2026-08-25)

Deferred out of the fix wave that closed 3 Important + 2 Minor findings from
the whole-branch metronome review (commits `0c42e4e..f551968`). Two items
the review scoped to a later plan, plus one residual from the amplitude fix
that landed in the fix wave.

- **Metronome is silent (with no explanation) on a device with no microphone
  input.** `MainComponent`'s `setAudioChannels(1, 2)` means that on the path
  the app already anticipates — `getCurrentAudioDevice() == nullptr`, "No
  microphone found" — nothing ever calls `getNextAudioBlock()`, so the
  metronome (despite being independent of pitch calibration in every other
  respect) produces no sound and no error even if the user presses Start.
  In practice JUCE usually still opens the default output device with 0
  input channels rather than returning null, so this may never actually
  trigger — but nobody scoped "what does the metronome need to work"
  separately from "what does the pitch tracker need." Proper fix: fall back
  to `setAudioChannels(0, 2)` when the 1-input attempt yields no device, or
  disable the metronome controls and say so in `statusLabel` on that path.
  **Flagged by the final reviewer as Packaging-plan scope.**
- **`MetronomeClickTests.cpp`'s frequency-accuracy test uses a bounded local
  Goertzel search (`target ± 30Hz`) rather than a global peak search.** It
  currently still catches a grossly-wrong frequency only because the
  sidelobe magnitude happens to be monotonic across that narrow window and
  the argmax pins to the band edge — correct today, but fragile: it depends
  on the 30Hz search radius staying larger in cents than the 30-cent
  tolerance at every target frequency (already thin at 1200Hz, where 30Hz is
  only ~43 cents). Proper fix: add a ratio assertion alongside the existing
  one (the magnitude at the measured peak should exceed the magnitude at
  `2*target` and `0.5*target` by a comfortable factor), making "the peak is
  really here" an assertion instead of an assumption.
- **Residual clipping risk after the Sam-amplitude headroom fix.** The fix
  wave dropped `MetronomeClick`'s amplitudes ~30% (Sam 1.00→0.70, etc.) to
  restore headroom against the tanpura's default gain (0.5), but a tanpura
  gain pushed toward its own max (1.0) can still sum with Sam past full
  scale. The proper fix — a real mixer/limiter stage across the tanpura and
  metronome outputs — was explicitly out of scope for the metronome plan;
  this note exists so the next audio-mixing-touching plan doesn't have to
  rediscover it from scratch.
- **Two comment-wording nits from the fix wave's re-review**, non-blocking:
  `MetronomeAudioSourceTests.cpp`'s new "does the disabled block reset"
  test's comment overclaims a discriminating power the test doesn't
  actually have for the underlying race (a two-thread interleaving,
  correctly undiscriminable single-threaded — the limitation is disclosed
  honestly in the fix report and commit message, just not in the test file
  itself); and `MetronomeAudioSource.cpp`'s surviving "unconditionally
  consumed" comment on the flag-exchange lines now only applies past the
  `enabled` gate, reading oddly directly beneath that branch.

## From the Tanpura final-review fix wave (2026-08-25)

Deferred out of the fix wave that closed the four Important findings from the
whole-branch tanpura review. Each of these is a *proper* fix for something the
fix wave only partially addressed, plus one portability item.

- **Karplus-Strong tuning still has a residual ~±8 cent error — the proper fix
  is a fractional (interpolated) delay.** `KarplusStrongString::pluck()` now
  computes `N = lround(sampleRate/frequencyHz + 0.5)`, which removes the
  *systematic sharp bias* (the in-place filter's true loop delay is `N - 0.5`
  samples, so plain rounding always resonated above the target — up to +15
  cents at realistic tonics) and halves the worst case. But `N` is still an
  integer, so a quantization error of up to roughly ±8 cents remains. This
  matters more than a normal tuning nit: the tanpura is the user's tuning
  reference *and* the UI simultaneously grades their deviation from the exact
  calibrated Sa in cents, so any drone error shows up as the app disagreeing
  with itself. Proper fix: interpolate the read position so the total loop
  delay equals `sampleRate / frequencyHz` exactly — linear interpolation or a
  first-order allpass tuning filter; the codebase already uses
  `juce::LagrangeInterpolator` in `CrepePitchEngine`, so fractional delay is
  not a foreign concept here.
- **End a string's ring on a measured amplitude/RMS floor rather than a fixed
  duration.** The fix wave made the cutoff duration-correct — it is now
  `kMaxRingSeconds = 4.0f` converted to samples at pluck time from the real
  device rate, instead of a `44100 * 4` raw sample count that silently shrank
  to 1.84s at 96kHz. That fixes the sample-rate dependence but not the
  underlying problem: measured t60 is ~8s for a madhya Sa string and ~16s for
  a mandra Sa, both far longer than 4s, so the string is still audible when it
  is cut (measured amplitude at the cut is 0.043–0.100 for the lower strings)
  and the cut is a step discontinuity — a faint click, once per string per
  pluck cycle. Terminating on an actual amplitude/RMS floor would end the ring
  at true silence instead of on an arbitrary clock.
- **`std::rand()` runs on the audio thread inside `pluck()`.** It is called
  once per delay-line slot to fill the noise burst. On MSVC — the only
  platform this currently builds for — `rand()` is TLS-backed and lock-free,
  so this is not a live bug today. On glibc it delegates to `random()`, which
  takes an internal lock: a hard real-time violation the moment this is built
  for Linux, and directly contrary to the plan's own "no locks on the hot
  path" constraint. Replacing it with a `juce::Random` member instance would
  be both allocation- and lock-free, and would close the separately-known
  "unseeded `rand()` makes tests non-deterministic" minor in the same edit.
- **The tanpura drone can bias re-calibration if it stays audible through a
  device restart.** (Recorded here per the tanpura plan's commitment at
  `2026-08-25-tanpura.md:857`, which was never actually carried out.)
  `releaseResources()` does not reset `enabled`, and `prepareToPlay()` sets
  `retunePending = true`, so on an audio-device restart the drone resumes
  seamlessly at the old Sa while a fresh calibration runs — and the mic can
  hear it. Narrow path: only reachable via an audio-device restart, not
  through normal use, and the user can drag the volume slider to zero. Worth
  either muting the drone for the duration of a calibration, or detecting
  mic bleed, once a stop control or device-selector UI exists.

## From RealtimeApp final-review fix-wave re-review (2026-08-25, commit 52324b1)

- **`~PitchWorker`'s message-thread `jassert` documents a contract that isn't
  actually guaranteed by every caller.** `MainComponent::releaseResources()`
  owns and destroys the `PitchWorker`, and JUCE calls `releaseResources()`
  from `prepareToPlay()`/`audioDeviceStopped()`, which run on the audio
  device's own thread, not the message thread. `prepareToPlay()` runs a
  second time (after the app's initial startup) only if the default audio
  device changes while the app is open — no device-switch UI exists in v1,
  so this is currently untriggered in normal use, not unreachable. The
  normal app-close path (`~MainComponent` -> `shutdownAudio()`) does run on
  the message thread and is safe. Two ways to actually close this, not just
  document it: (a) marshal `releaseResources()`'s worker teardown onto the
  message thread (e.g. via `MessageManager::callAsync` + a blocking wait for
  it to complete before `prepareToPlay()` proceeds), or (b) make the
  pending-update handoff itself safe to race against destruction (e.g. a
  shared `std::atomic<bool> alive` flag that `handleAsyncUpdate()` checks
  before touching `listener`, set to false as the first step of
  `~PitchWorker()` under the same lock `latestUpdate` already uses). Worth
  fixing for real once the app grows a device-selector UI, which is exactly
  the change that makes this reachable.

## From PitchPipeline plan final review (2026-08-18, commits f61a18e..55840e9) — for the next plan (real-time audio + GUI wiring)

- **`PitchPipelineUpdate` is under-specified for its stated job as the single
  GUI-facing output.** It drops `PitchFrame::timestampMs` (the exact value
  Task 2 of this plan spent fixing to advance in predictable 64ms steps -
  needed for a live pitch graph's x-axis) and `PitchFrame::confidence`.
  Neither is surfaced anywhere on `PitchPipelineUpdate`. Add both fields and
  populate them in `handleLive()` before wiring the graph.
- **No engine-health signal reaches `PitchPipelineUpdate`.** If the injected
  engine's `prepare()` failed (e.g. missing model file), `processFrame()`
  returns unvoiced forever, calibration silently reports `Timeout` after the
  window closes, and there is no way to distinguish "user was silent" from
  "the model file is missing" without the caller separately polling
  `engine.getStatus()` — exactly the juggling `PitchPipeline` exists to
  remove. Add `PitchEngineStatus engineStatus = engine.getStatus();` to the
  update struct.
- **`PitchPipeline` has no documented threading contract, and
  `restartCalibration()` is a concrete race.** `process()` will run on a
  worker thread; a "Recalibrate" button lives on the JUCE message thread.
  `phase`, `saHz`, and the owned `TonicCalibrator`/`SwarMapper`'s internal
  state are all plain, unsynchronized members. Either document
  single-thread-ownership (all calls, including `restartCalibration()`, must
  be marshalled onto the same thread) or make the relevant state safe to
  call from two threads — a decision the real-time wiring plan needs to make
  explicitly, not discover mid-implementation.
- **No way to recover from a stuck `PitchContinuityFilter` octave-correction
  without discarding a valid Sa.** Per the octave-correction design tension
  already noted below (SwarMapper final review), "sing Sa, pause, sing upper
  Sa" can leave the engine stuck an octave low until `reset()`. Today the
  only escape `PitchPipeline` offers is `restartCalibration()`, which throws
  away `saHz` and forces a full 3-second re-calibration to fix what's really
  just a tracking glitch. Add a `resetTracking()` that calls `engine.reset()`
  + `swarMapper.reset()` while preserving `saHz` and `phase == Live`.
- **Calibration silently discards most inference results at large block
  sizes.** `TonicCalibrator` records at most one confident reading per
  `PitchPipeline::process()` call, since `CrepePitchEngine::processFrame()`
  (after the multi-window-draining fix in this plan) returns only the latest
  of however many windows it internally drained. With the defaults
  (`windowMs=3000`, `minConfidentReadings=10`), calibration needs at least 10
  *calls* inside the window — i.e. the caller must feed blocks no larger than
  ~300ms, regardless of how many CREPE inferences actually ran. Feed
  `PitchPipeline` a single 3000ms block during calibration and it burns ~46
  real inferences, keeps 1, and reports `Timeout`. Not a regression (the old
  single-window engine had the same 1-reading-per-call rate), but now the
  discarded work is real and the ceiling is undocumented. Minimum: document
  the block-size ceiling on `PitchPipeline`'s constructor. Better: give
  `PitchEngine::processFrame()` a way to yield ALL frames from one call (an
  out-vector or callback), which would also fix the pitch-graph frame-drop
  gap above and make transient `InferenceError`s observable instead of being
  silently overwritten by a later window's success within the same call.
- Minor: timestamp arithmetic in `CrepePitchEngine` is `(uint64_t)((double)
  n / 16000.0 * 1000.0)` — empirically exact at every window count checked,
  but not provably exact due to double rounding. `n * 1000ULL / 16000ULL` is
  exact by construction and free; swap it in next time that code is touched.
- Minor: `PitchPipeline`'s `sampleRate` member is stored but never read after
  construction, and there's no check that it actually matches the rate
  `engine.prepare()` was given — a mismatch would silently corrupt
  calibration timing. Either drop the member or spend it on a validation
  `jassert`.
- Minor: no `jassert` guards the invariant that `CalibrationStatus::Success`
  always pairs with a non-nullopt `saHz` (true today per `TonicCalibrator`'s
  own contract, but unasserted — if it ever broke, `phase` would never
  advance and the caller would see `Success` with `saHz == 0` forever,
  silently).
- Minor (test hygiene, not a code defect): the end-to-end `PitchPipeline`
  test's calibration-phase assertion is coupled to real ONNX inference
  reaching its 10th confident reading on exactly the final (16th) chunk —
  passes today, but is brittle against any future change to model behavior,
  timing constants, or hop rate. Worth a comment noting the coupling, or
  restructuring to check calibration success as soon as it happens rather
  than only after the fixed loop count.

## From PitchPipeline plan, Task 2 review (2026-08-18, commit 9117332)

- `CrepePitchEngine::processFrame()`'s resample step truncates the fractional
  output sample every call (`(int)((double) numSamples / resampleSpeedRatio)`)
  and JUCE's `LagrangeInterpolator` doesn't carry the dropped input forward
  across calls — each call gets a fresh `audioFrame` pointer, so the leftover
  audio is silently discarded. At 44.1kHz with a 512-sample block this drops
  ~0.76 samples/call (~0.4% timestamp drift, ~2.4s over a 10-minute session);
  any block size not evenly divisible by `resampleSpeedRatio` drifts similarly.
  All current tests run at 16kHz native rate (ratio 1.0, zero drift), so
  nothing catches this. Pre-dates this plan (inherited from the original
  PitchEngine plan's resampler code) but this plan's timestamp fix makes the
  drift load-bearing for the first time — worth a proper fix (accumulate the
  fractional remainder across calls) plus a 44.1kHz test with irregular block
  sizes before this is trusted as ground truth for anything timing-sensitive
  (e.g. a live pitch graph's x-axis).

## From TonicCalibrator final review (2026-08-18, commits 8fb195f..01ad60f)

- `TonicCalibrator`'s `minConfidentReadings` constructor parameter has no defensive
  guard against `<= 0`. A value of `0` (the natural way to express "no minimum")
  causes an out-of-bounds `operator[]` read on an empty `confidentFrequencies`
  vector. No in-tree caller currently passes anything but the default (10), so
  this isn't reachable today, but a `std::max(1, minConfidentReadingsIn)` clamp
  or a `jassert` in the constructor is one line and worth adding before this
  parameter is ever exposed to real configuration.
- The test proving the ratio-gate-replacement fix only covers the "few confident
  readings among many calls, still correctly Timeout" side. It doesn't cover the
  side that actually motivated the fix: "enough confident readings (>=10) buried
  among many buffering-only nullopt calls, still correctly Success" (the old
  ratio gate would have wrongly reported Timeout here; nothing currently proves
  the new gate reports Success). Worth adding once the real-time plan makes
  this scenario concrete.
- `PitchEngine::processFrame()`'s `nullopt` return is still overloaded between
  two meanings ("unvoiced/low-confidence" vs. "still buffering toward a full
  analysis window") with no way for a caller to distinguish them. TonicCalibrator's
  absolute-minimum-count fix sidesteps needing this distinction, but the next
  plan (real-time audio wiring) may still want it for other reasons (e.g. a live
  UI indicator that says "listening..." during buffering vs. "no pitch detected"
  during genuine silence) — flagging as a real interface gap, not re-opening it
  as a blocker.

## From plan-eng-review (2026-08-18)

- Benchmark CPU vs. GPU (CUDA/DirectML/CoreML) execution provider for CREPE inference throughput once the pitch pipeline exists.
- ~~Source and license-verify ~12 pre-recorded tanpura samples (one per
  semitone) — blocking gate before the tanpura milestone starts.~~
  **No longer applicable.** The tanpura milestone shipped fully synthesized
  (Karplus-Strong, `src/audio/tanpura/`) specifically to avoid this licensing
  gate — the plan's Global Constraint required no sampled audio, and
  `git ls-files` finds no audio assets in the repo. Nothing needs licensing.
- Verify RVC's `rmvpe.onnx` license terms (RVC lineage is GPL-adjacent) — **blocking gate before v1.1 RMVPE work starts**.

## ONNX Runtime dependency (2026-08-18)

`vcpkg install onnxruntime` fails to build from source on this machine due to a
known, unresolved upstream MASM assembler bug in ONNX Runtime's hand-written
AVX kernels (`cvtfp16Avx.asm`, error A2008 on `ymm`/`xmm` operands —
[microsoft/onnxruntime#23166](https://github.com/microsoft/onnxruntime/issues/23166),
still open across ORT 1.20-1.23.x on multiple VS2022 toolchains). `onnxruntime`
was removed from `vcpkg.json` for this reason.

Worked around by vendoring the official prebuilt release instead:
`third_party/onnxruntime-win-x64-1.23.2/` (downloaded via
`gh release download v1.23.2 -R microsoft/onnxruntime -p onnxruntime-win-x64-1.23.2.zip`),
containing `include/onnxruntime_cxx_api.h` and `lib/onnxruntime.{lib,dll}` —
exactly what `CrepePitchEngine` needs. `third_party/` is gitignored (265MB);
each machine needs to re-download it. The next plan (PitchEngine +
CrepePitchEngine) should point CMake at this directory directly
(`target_include_directories`/`target_link_libraries`) rather than
`find_package(onnxruntime CONFIG)` via vcpkg, since vcpkg no longer manages it.

## From PitchEngine final review (2026-08-18, commits 68de234..ea21b1f) — for the next plan (real-time audio wiring)

These are structural, not bugs on the path today's tests cover — but they need
to be design inputs for the next plan (wiring `CrepePitchEngine` behind
`juce::AbstractFifo` + a worker thread), not rediscovered mid-implementation.

- **`processFrame()`'s one-result-per-call shape can't keep up with large blocks.**
  `resampledBuffer` fills from resampled input but only drains one 1024-sample
  window per call. At 44.1kHz with a 4096-sample block (a normal, selectable
  audio buffer size), each call produces ~1486 resampled samples but only
  consumes 1024 — the buffer grows ~462 samples/call, unboundedly, with no
  error signal. Fix direction: either loop internally (`while
  (resampledBuffer.size() >= 1024) { ...keep most recent result... }`) or cap
  buffer size and report an overflow status. Needs a decision on which frame
  to report when a block yields multiple windows.
- **Timestamps lag the audio they describe by up to ~75ms, and the lag varies
  frame to frame.** The analyzed window is drawn from the front of
  `resampledBuffer`, which carries 0-1200 samples of residue in steady state —
  so `timestampMs` claims a time up to 75ms newer than the actual audio being
  analyzed. Will show up as jitter in a live pitch graph. Fix by deriving the
  timestamp from the window's own start position, not `samplesProcessed` at
  call time.
- **`CrepePitchEngine::status` needs to become `std::atomic<PitchEngineStatus>`
  before `processFrame()` moves to a worker thread.** Currently written on
  every inference and read by `getStatus()` (intended for UI polling) with no
  synchronization — harmless single-threaded today, a data race the moment
  the real-time wiring plan lands.
- **`PitchContinuityFilter`'s octave-correction heuristic will fight a real
  Sa→upper-Sa leap** (a staple sargam/aakar exercise, and `SwarMapper` already
  has `OctaveRegister {Mandra, Madhya, Taar}` as a first-class, user-visible
  concept). The filter can't currently distinguish "CREPE glitched for one
  frame" from "the singer deliberately jumped an octave" — both land within
  50 cents of an exact 1200-cent multiple. Worse, unvoiced frames don't reset
  the reference, so "sing Sa, pause, sing upper Sa" is also mis-corrected, and
  because the *corrected* value gets written back as the new reference, the
  engine stays stuck an octave low until `reset()`. **This is a design
  decision, not a bug** — what actually distinguishes a glitch from a leap is
  persistence (a CREPE error lasts one frame and reverts; a real leap holds).
  Needs a decision before the next plan: replace the stateless single-frame
  rule with either (a) a one-frame-outlier check (only correct if the
  following frame returns near the pre-jump reference), or (b) expire the
  reference after ~200ms of unvoiced/silence. Flagging for you rather than
  picking unilaterally, since it changes what "octave accuracy" scoring
  (section 4.3 of the original plan) will actually measure.
- **CMake structure**: pitch-engine sources compile directly into `riyaaz_tests`
  only — there's no `riyaaz_pitchengine` library target the way `riyaaz_swarmap`
  exists for the swar-mapping module. The ONNX include dirs, `.lib`, and
  DLL-copy step are all attached to `riyaaz_tests` alone. The next plan's
  first task should extract a proper library target (propagating ONNX
  include/link `PUBLIC`) before an actual app target needs the same wiring
  duplicated.
- **Test gap**: all 26 current tests either avoid the resampler entirely (feed
  16kHz-native audio) or use a single fixed 1024-sample block size — the
  resampling and variable-block-size buffering paths are effectively
  untested. Cheapest high-value addition: a 44.1kHz test feeding the same
  220Hz sine in irregular block sizes (e.g. cycling 128/512/300/1024) and
  asserting both correct detection and bounded `resampledBuffer` growth.
- Minor: `decodeCrepeOutput`'s `numBins<=0` guard and the output-tensor
  element-count check are both now in place, but neither is covered by a test
  that would catch a *real* ONNX Runtime failure mode (only synthetic/null
  inputs) — acceptable given there's no session-mocking seam, noted for
  awareness.
- Minor: each `CrepePitchEngine` instance owns its own `Ort::Env`; ONNX Runtime
  intends `Env` as a per-process singleton. Harmless with one engine instance
  (today); make it a shared static before a second engine (e.g. an RMVPE A/B
  comparison instance, per the original plan's v1.1 milestone) ever coexists.
- Product note: non-overlapping 1024-sample windows at 16kHz give a 64ms hop
  (15.6 pitch estimates/second) — reference CREPE usage is typically a 10ms
  hop with overlapping windows. Hindustani ornaments (murki, khatka) live in
  the 50-150ms range and will get smeared at 64ms. The non-overlapping choice
  was deliberate for v1 simplicity (the current buffer-erase design can't
  retain window history for overlap), but hop size should be treated as a
  tunable the next plan can revisit, not a fixed constant.

## From SwarMapper final review (2026-08-18, commits a4b4c9a..6615a19)

- Wrap `Swar`, `SwarLabel`, `SwarMapper`, `swarToString`, `registerToString` in `namespace riyaaz` — currently global scope. Cheapest to do now (zero consumers yet); costs a diff across every consumer once the pitch pipeline starts calling into it.
- Add doc comments to `SwarMapper.h` covering: cents are relative to the calibrated tonic; silence/unvoiced frames need an explicit `reset()` call (no NaN/no-pitch guard exists yet — the caller must know this); the class is single-thread-owned, no atomics (relevant once it sits behind the worker-thread/AsyncUpdater pipeline); `swarToString`/`registerToString` return `juce::String` (allocates) — must not be called from the audio thread.
- Add CTest integration (`enable_testing()` + `add_test(...)`) so CI can just run `ctest`.
- Set a default `CMAKE_BUILD_TYPE` for the Ninja single-config generator; pin a `builtin-baseline` in `vcpkg.json`.
- `SwarMapperTests.cpp`'s Mandra test uses `-150.0f`, an exact `.5` rounding tie (stable today under `std::lround`'s away-from-zero behavior, but fragile if that ever changes) — `-145.0f` would test the same path without depending on tie-breaking.
- Decide the NaN/silence contract for `SwarMapper::update()` once `PitchEngine` exists and it's clear whether unvoiced frames get filtered upstream or call `reset()` directly — don't design this speculatively before that decision exists.

## From RealtimeApp final review (2026-08-25)

- **`PitchWorker::run()` drain size is unbounded by the audio block size.** Each
  loop iteration reads *all* currently-ready FIFO samples and hands them to
  `PitchPipeline::process()` in a single call. In steady state that is roughly
  one audio callback, but if the worker thread ever falls behind real time (slow
  ONNX inference, CPU contention, a debugger break, a device glitch), the next
  drain can hand the pipeline many callbacks' worth of audio — up to the whole
  FIFO capacity — as one block. That collapses what would have been many
  `PitchEngine::processFrame()` calls into one, which matters for
  `TonicCalibrator`: its `minConfidentReadings` gate counts *confident engine
  frames*, so a stalled worker can make an otherwise-fine calibration window
  time out. The earlier plan's rule of thumb ("keep blocks under ~300ms so the
  calibrator gets multiple `process()` calls") is therefore no longer the real
  invariant; the real one is **the worker thread keeps up with real time**, and
  nothing currently enforces or detects a violation of it. Documented in
  `PitchWorker.h`. Possible fixes for a later pass, none implemented now: cap
  how many samples one drain processes (looping until the FIFO is empty), or
  read in fixed CREPE-window-sized chunks so engine-frame counts are independent
  of drain timing; optionally surface a "worker fell behind" counter for
  diagnostics.
