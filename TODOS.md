# TODOS

Deferred, non-blocking items. Not urgent, but shouldn't get silently lost.

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
- Source and license-verify ~12 pre-recorded tanpura samples (one per semitone) — **blocking gate before the tanpura milestone starts**, not just a nice-to-have.
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
