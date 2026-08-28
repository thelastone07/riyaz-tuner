# Packaging — Design Spec

**Status:** Approved by user, ready for implementation planning.
**Context:** Last of the three remaining v1 pieces (Tanpura, Metronome, Packaging) identified after the pitch-tracking core shipped. Tanpura and Metronome are both complete, reviewed, and pushed. This spec covers making the repository actually buildable and distributable by someone who is not the original developer.

## Purpose

Today, `git clone`-ing this repository and trying to build it fails immediately: two required dependencies (`third_party/onnxruntime-win-x64-1.23.2/`, `models/crepe/small.onnx`) are gitignored and not in the repo, there is no README or build documentation of any kind, `MainComponent` resolves its model path relative to the current working directory rather than the executable (so even a successfully-built app breaks the moment it's copied or launched from anywhere other than the repo root), and only a Debug build has ever been built or run in this entire project.

The explicit goal, in the user's own words: **"I want this app to be published on git, so anyone can bundle it. I don't necessarily need to give them the bundled software, but there must be instructions on how to do so."** This is not about the user personally shipping a polished installer — it's about making the repository self-sufficient: anyone who clones it can follow documented steps to fetch dependencies, build, and produce a portable, working distribution themselves.

**Explicit scope decisions from user review:**
- Windows-only for v1 (matches everything built so far — MSVC, vcpkg `x64-windows` triplet, a Windows-only vendored ONNX Runtime binary).
- Output format is a portable folder/zip, not a full installer (NSIS/WiX) — no new heavyweight build dependency, matches "instructions so anyone can bundle it" rather than a polished consumer install experience.
- The MSVC runtime redistributable dependency is **documented as a prerequisite**, not statically linked. Investigated during design: JUCE (via vcpkg's default `x64-windows` triplet) is compiled against the dynamic CRT even though it links as a static `.lib`; switching only this project's own code to static-CRT linking would create a runtime-library mismatch against those already-built JUCE libraries. Genuine static-CRT linking would require rebuilding JUCE from scratch against the `x64-windows-static` triplet — real time and failure risk in a dependency this project doesn't control, for a redistributable that's already present on most Windows systems. Not worth it for v1.

## Verified dependency sources

Both looked up directly via the GitHub API during design (not guessed), with exact asset sizes for the fetch script to sanity-check against:

| Dependency | Source | Exact URL | Size (bytes) |
|---|---|---|---|
| ONNX Runtime win-x64 1.23.2 | `microsoft/onnxruntime` GitHub releases | `https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-win-x64-1.23.2.zip` | 78,127,794 |
| CREPE "small" capacity ONNX weights | `yqzhishen/onnxcrepe` GitHub releases, tag `v1.1.0` | `https://github.com/yqzhishen/onnxcrepe/releases/download/v1.1.0/small.onnx` | 6,524,192 |

The ONNX Runtime zip extracts to a top-level `onnxruntime-win-x64-1.23.2/` directory (standard ONNX Runtime release layout) — extracting it directly into `third_party/` reproduces the exact existing path `third_party/onnxruntime-win-x64-1.23.2/` that `CMakeLists.txt` already expects, so no CMake changes are needed to accommodate the fetched copy. The CREPE asset is a single file, saved directly to `models/crepe/small.onnx`.

## Component breakdown

### 1. `ResourcePath` (new, pure logic + unit tests)

The root cause of "a built app breaks when moved": `MainComponent.h:30` currently does `CrepePitchEngine engine { juce::String ("models/crepe/small.onnx") }` — a path resolved relative to the process's current working directory. A real installed/distributed app has no guarantee its CWD is anywhere near where its own executable lives.

Fix: extract a small, pure, injectable-dependency function so it's actually testable without touching the real filesystem or the real executable location:

```cpp
// src/app/ResourcePath.h
#pragma once
#include <juce_core/juce_core.h>

// Resolves relativePath against the directory containing executableFile.
// Pure and injectable (the caller supplies executableFile) specifically so
// this is unit-testable without depending on the real running executable's
// location.
juce::File resolveResourcePath (const juce::File& executableFile, const juce::String& relativePath);
```

`MainComponent`'s real call site supplies the actual running executable:

```cpp
juce::File::getSpecialLocation (juce::File::currentExecutableFile)
```

This is the single change that makes a copied/zipped/moved build actually work — today it silently fails outside `CWD == repo root`, which is exactly the failure mode "anyone can bundle it" runs into immediately.

### 2. CMake: copy the model next to `riyaaz_app`'s build output

`CMakeLists.txt` already has `riyaaz_copy_onnxruntime_dll(target)`, called for both `riyaaz_tests` and `riyaaz_app`, copying `onnxruntime.dll` next to each target's built executable. Add an analogous `riyaaz_copy_crepe_model(target)` that copies `models/crepe/small.onnx` (creating the `models/crepe/` subdirectory) next to a target's build output — called for `riyaaz_app` only. (`riyaaz_tests` deliberately keeps its existing CWD-relative-to-repo-root convention; it's an internal dev/CI convention already documented and working, not something end users touch, and changing it is out of this plan's scope.)

With both this and `ResourcePath` in place, `riyaaz_app`'s build output directory becomes a fully self-contained, movable unit: the exe, `onnxruntime.dll`, and `models/crepe/small.onnx` all sit together, and the exe finds its model relative to itself rather than CWD.

### 3. `scripts/fetch-dependencies.ps1`

A PowerShell script (Windows-only, matching the platform decision) that downloads both dependencies from the verified URLs above into the exact paths `CMakeLists.txt` already expects, and fails loudly rather than leaving a partial/corrupt download in place:

- Download each asset to a temp location.
- Verify the downloaded file's size exactly matches the table above (`(Get-Item $file).Length`) — a cheap, real integrity check; GitHub release assets in this case have no published content hash to check against instead (confirmed via the API: `digest` is `null` on both assets), so exact size match is the strongest available signal.
- On a size mismatch or any download error: print a clear error naming which dependency failed and why, delete the partial file, and exit non-zero. Never leave a corrupt file at the expected final path — a script that "succeeds" while silently leaving a broken 40%-downloaded model file would fail mysteriously and confusingly much later, at model-load time.
- On success: extract the ONNX Runtime zip into `third_party/` (producing `third_party/onnxruntime-win-x64-1.23.2/`), move the CREPE asset to `models/crepe/small.onnx` (creating the directory).
- Skip re-downloading if the destination already exists and already matches the expected size (idempotent — safe to re-run).

### 4. `scripts/package.ps1`

A PowerShell script that produces the distributable artifact:

- Configures and builds `riyaaz_app` in **Release** mode (this project has never built Release before — this script's first real run is also this plan's verification that Release actually builds, not an assumption).
- Creates a clean `dist/Riyaaz-<version>/` directory (version read from `CMakeLists.txt`'s `project(riyaaz VERSION ...)` line, currently `0.1.0`).
- Copies `Riyaaz.exe`, `onnxruntime.dll`, and `models/crepe/small.onnx` (already colocated by the CMake copy step in the Release build output directory) into it.
- Zips the folder to `dist/Riyaaz-<version>-win64.zip`.
- Prints the final output path.

### 5. `BUILDING.md` + minimal `README.md`

`BUILDING.md` (repo root) ties everything together for a stranger cloning the repo: prerequisites (Visual Studio Build Tools with the C++ workload, CMake ≥3.24, Ninja, vcpkg, Git), the note that the target machine needs the Microsoft Visual C++ Redistributable (x64) installed to run the packaged app (linking to Microsoft's official download page), exact vcpkg bootstrap + CMake configure/build commands (the same ones used throughout this project's development: `cmake -B build -S . -G Ninja -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows`), how to run `fetch-dependencies.ps1` first, and how to run `package.ps1` to produce the distributable zip.

`README.md` (repo root, currently does not exist at all) — kept small and orientational: what this app is (a Hindustani classical vocal practice aid — tanpura drone, taal metronome, real-time pitch/swar feedback), and a pointer to `BUILDING.md`. Not a marketing page.

## Data flow / acceptance test

There's no runtime data flow to design here — this is build/deploy tooling. What matters is a concrete, literal end-to-end sequence that proves the whole thing actually works, which becomes this plan's final verification step (not just "each script ran without a nonzero exit code"):

1. Start from a location that does **not** already have `third_party/` or `models/` populated (or delete them first).
2. Run `fetch-dependencies.ps1`. Confirm both dependencies land at the expected paths with the expected sizes.
3. Follow `BUILDING.md`'s documented build commands exactly as written, from a clean `build/` directory.
4. Run `package.ps1`. Confirm `dist/Riyaaz-<version>-win64.zip` is produced.
5. Extract that zip to a directory that is **not** the repo (e.g. Desktop, or a temp directory) and launch `Riyaaz.exe` from there. Confirm the app starts and does **not** show "Could not load the pitch model" — proving the `ResourcePath` fix actually resolves the model correctly outside the repo, which is the specific failure mode this whole plan exists to close.

## Testing requirements

- `ResourcePath`'s pure function gets real unit tests: joining a relative path onto an injected fake executable `File`'s parent directory, confirmed against concrete expected paths. No real filesystem access or real executable location needed — that's the point of injecting `executableFile` as a parameter rather than calling `juce::File::getSpecialLocation` inside the function itself.
- Everything else (the two PowerShell scripts, the CMake copy-step, the documentation) is not meaningfully unit-testable — verified instead by the literal acceptance sequence above, which is itself a required, non-skippable step of the implementation plan.

## Error handling

Beyond `fetch-dependencies.ps1`'s size-verification behavior (above): `package.ps1` should fail clearly and stop (not produce a silently-incomplete zip) if the Release build fails, or if any of the three expected files (exe, dll, model) is missing from the build output directory before zipping — the same "fail loud rather than ship broken" principle.

## Open items intentionally deferred (not blocking this spec)

- Full installer (NSIS/WiX), code signing, app icon, embedded version-info resource — all explicitly out of scope per the user's stated need; a portable zip with documented build instructions is the actual ask.
- Cross-platform (macOS/Linux) packaging — explicitly deferred; would need a second ONNX Runtime binary, different audio-device testing, and is real scope of its own, not foldable into this plan.
- Two items already carried forward from the Metronome plan's final review into "Packaging-plan scope" in `TODOS.md`: the metronome silently producing no sound on a device with no microphone input, and the metronome having no independent volume control. Neither is addressed by this plan — this plan is about making the app *buildable and distributable*, not about further audio/UX features. Left in `TODOS.md` for whichever future work picks them up; noting here only so it's clear this plan was not expected to absorb them.
