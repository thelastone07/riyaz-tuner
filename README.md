# Riyaaz

A Hindustani classical vocal practice app: live pitch tracking against a
calibrated Sa, a synthesized tanpura drone, a metronome with traditional
taals, and Alankar (scale pattern) practice with scoring.

## Prerequisites

- Windows, Visual Studio 2022 (or the Build Tools) with the "Desktop
  development with C++" workload
- [vcpkg](https://github.com/microsoft/vcpkg), with `VCPKG_ROOT` set (this
  project's dependencies - JUCE - are pulled in via `vcpkg.json`)
- CMake 3.24+ and Ninja (both ship with the Visual Studio C++ workload, or
  install separately)

## Build

From the repo root, in a shell with the VS toolchain on `PATH` (e.g. the
"Developer PowerShell for VS 2022"):

```powershell
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --target riyaaz_app --config Debug
```

The first configure step will take a while - vcpkg builds JUCE from source.

## Run

```powershell
build\riyaaz_app_artefacts\Debug\Riyaaz.exe
```

Or just double-click `Riyaaz.exe` in that folder. Run it from the repo root
(or somewhere `models/crepe/small.onnx` resolves relative to the working
directory) - the app loads that ONNX pitch model at startup, and won't be
able to detect pitch without it.

A microphone is required. On first launch, Windows may prompt for
microphone permission.

## Using the app

1. **Pick or create a profile.** Choose an existing profile ("Use saved Sa"
   to reuse its last calibrated Sa, or "Recalibrate" to redo it), or create a
   new one.
2. **Calibrate.** Sing a steady, comfortable note for a few seconds - this
   becomes your Sa (tonic reference).
3. **Practice.** Sing along and watch the pitch graph track your voice
   against Sa. Turn on the tanpura drone (▶ next to the TANPURA slider) and
   the metronome (▶ next to the taal dropdown) as you like.
4. **Alankar practice** (optional): switch the mode dropdown to "Alankar
   practice", pick a pattern, and press "Start Alankar Practice". Adjust
   tempo any time via the BPM slider; pause/resume mid-run or loop the
   pattern via the controls next to the pattern picker. You'll get a
   per-swar in-tune score when a run finishes.
5. **Session History** shows past Alankar practice sessions and your
   improvement trend over time.
6. Press the **⌂** button (top-left) at any point to go back and recalibrate
   or switch profiles.

## Run the tests

```powershell
cmake --build build --target riyaaz_tests --config Debug
build\riyaaz_tests_artefacts\Debug\riyaaz_tests.exe
```
