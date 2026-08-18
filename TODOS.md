# TODOS

Deferred, non-blocking items. Not urgent, but shouldn't get silently lost.

## From plan-eng-review (2026-08-18)

- Benchmark CPU vs. GPU (CUDA/DirectML/CoreML) execution provider for CREPE inference throughput once the pitch pipeline exists.
- Source and license-verify ~12 pre-recorded tanpura samples (one per semitone) — **blocking gate before the tanpura milestone starts**, not just a nice-to-have.
- Verify RVC's `rmvpe.onnx` license terms (RVC lineage is GPL-adjacent) — **blocking gate before v1.1 RMVPE work starts**.

## From SwarMapper final review (2026-08-18, commits a4b4c9a..6615a19)

- Wrap `Swar`, `SwarLabel`, `SwarMapper`, `swarToString`, `registerToString` in `namespace riyaaz` — currently global scope. Cheapest to do now (zero consumers yet); costs a diff across every consumer once the pitch pipeline starts calling into it.
- Add doc comments to `SwarMapper.h` covering: cents are relative to the calibrated tonic; silence/unvoiced frames need an explicit `reset()` call (no NaN/no-pitch guard exists yet — the caller must know this); the class is single-thread-owned, no atomics (relevant once it sits behind the worker-thread/AsyncUpdater pipeline); `swarToString`/`registerToString` return `juce::String` (allocates) — must not be called from the audio thread.
- Add CTest integration (`enable_testing()` + `add_test(...)`) so CI can just run `ctest`.
- Set a default `CMAKE_BUILD_TYPE` for the Ninja single-config generator; pin a `builtin-baseline` in `vcpkg.json`.
- `SwarMapperTests.cpp`'s Mandra test uses `-150.0f`, an exact `.5` rounding tie (stable today under `std::lround`'s away-from-zero behavior, but fragile if that ever changes) — `-145.0f` would test the same path without depending on tie-breaking.
- Decide the NaN/silence contract for `SwarMapper::update()` once `PitchEngine` exists and it's clear whether unvoiced frames get filtered upstream or call `reset()` directly — don't design this speculatively before that decision exists.
