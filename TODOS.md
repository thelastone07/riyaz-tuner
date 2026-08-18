# TODOS

Deferred, non-blocking items. Not urgent, but shouldn't get silently lost.

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

## From SwarMapper final review (2026-08-18, commits a4b4c9a..6615a19)

- Wrap `Swar`, `SwarLabel`, `SwarMapper`, `swarToString`, `registerToString` in `namespace riyaaz` — currently global scope. Cheapest to do now (zero consumers yet); costs a diff across every consumer once the pitch pipeline starts calling into it.
- Add doc comments to `SwarMapper.h` covering: cents are relative to the calibrated tonic; silence/unvoiced frames need an explicit `reset()` call (no NaN/no-pitch guard exists yet — the caller must know this); the class is single-thread-owned, no atomics (relevant once it sits behind the worker-thread/AsyncUpdater pipeline); `swarToString`/`registerToString` return `juce::String` (allocates) — must not be called from the audio thread.
- Add CTest integration (`enable_testing()` + `add_test(...)`) so CI can just run `ctest`.
- Set a default `CMAKE_BUILD_TYPE` for the Ninja single-config generator; pin a `builtin-baseline` in `vcpkg.json`.
- `SwarMapperTests.cpp`'s Mandra test uses `-150.0f`, an exact `.5` rounding tie (stable today under `std::lround`'s away-from-zero behavior, but fragile if that ever changes) — `-145.0f` would test the same path without depending on tie-breaking.
- Decide the NaN/silence contract for `SwarMapper::update()` once `PitchEngine` exists and it's clear whether unvoiced frames get filtered upstream or call `reset()` directly — don't design this speculatively before that decision exists.
