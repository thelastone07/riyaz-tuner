# Riyaaz

A Hindustani classical vocal practice app: live pitch tracking against a
calibrated Sa, a synthesized tanpura drone, a metronome with traditional
taals, and Alankar (scale pattern) practice with scoring.

The app is cross-platform (Windows, macOS, Linux) via JUCE + CMake + vcpkg.
Windows is the platform this has actually been built and run on; macOS/Linux
support is implemented (see "Cross-platform notes" below) but not yet
verified on real hardware - if you hit a platform-specific build error,
that's the first place to look.

## Setting up on a new machine

**Quickest path (macOS/Linux, or Windows in Git Bash):**

```bash
./scripts/build.sh
```

This bootstraps a local vcpkg if you don't already have one, downloads the
matching prebuilt ONNX Runtime for your platform into `third_party/`,
configures with CMake+Ninja, and builds `riyaaz_app`. It's idempotent - safe
to re-run any time (e.g. after pulling new commits).

The pitch detection model (`models/crepe/small.onnx`) is committed to the
repo directly (it's small - ~6MB), so it's already there once you clone -
nothing extra to fetch for it.

**Manual path**, or if you're on native Windows without Git Bash - see the
platform-specific sections below.

### Windows

Prerequisites:
- Visual Studio 2022 (or the Build Tools) with the "Desktop development with
  C++" workload
- [vcpkg](https://github.com/microsoft/vcpkg), with `VCPKG_ROOT` set (this
  project's dependencies - JUCE - are pulled in via `vcpkg.json`)
- CMake 3.24+ and Ninja (both ship with the Visual Studio C++ workload, or
  install separately)

From the repo root, in a shell with the VS toolchain on `PATH` (e.g. the
"Developer PowerShell for VS 2022"):

```powershell
# ONNX Runtime isn't fetched automatically outside scripts/build.sh - grab it
# once (Git Bash, or WSL, or manually from the GitHub release matching
# CMakeLists.txt's ONNXRUNTIME_DIR_NAME):
bash scripts/fetch-onnxruntime.sh

cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --target riyaaz_app --config Debug
```

Run it:

```powershell
build\riyaaz_app_artefacts\Debug\Riyaaz.exe
```

Or just double-click `Riyaaz.exe` in that folder.

### macOS

Prerequisites (via [Homebrew](https://brew.sh)):

```bash
xcode-select --install   # Command Line Tools - clang, not full Xcode
brew install cmake ninja
```

vcpkg isn't a prerequisite you need to install separately - `scripts/build.sh`
bootstraps its own local copy if `VCPKG_ROOT` isn't already set. Then:

```bash
./scripts/build.sh
```

Run it:

```bash
build/riyaaz_app_artefacts/Debug/Riyaaz.app/Contents/MacOS/Riyaaz
```

Or open `Riyaaz.app` in Finder/`open build/riyaaz_app_artefacts/Debug/Riyaaz.app`.
The first launch will prompt for microphone access (the app's `Info.plist`
carries the required usage-description entitlement) - allow it, or pitch
tracking gets no input.

### Linux

Same as macOS, via your package manager instead of Homebrew (e.g. on
Debian/Ubuntu: `sudo apt install cmake ninja-build build-essential`), then
`./scripts/build.sh`. JUCE's vcpkg port additionally needs some system
libraries on Linux (ALSA, X11 dev packages, etc.) - if `vcpkg install` fails
partway through, the error will name the missing `-dev` package to install.

## Cross-platform notes

Two things in this codebase are inherently platform-specific, both isolated
to small, clearly-marked spots:

- **ONNX Runtime** is vendored as a prebuilt release per platform under
  `third_party/` (gitignored) rather than built from source - see
  `scripts/fetch-onnxruntime.sh` and the platform branch at the top of
  `CMakeLists.txt`. Building it from source hits an unresolved upstream MASM
  bug on Windows (see TODOS.md's "ONNX Runtime dependency" note); vendoring
  everywhere keeps the three platforms' build steps identical instead of
  building from source on some and vendoring on others.
- **`CrepePitchEngine::prepare()`** picks between ONNX Runtime's two model-path
  string types (`wchar_t` on Windows, `char` elsewhere) with a `#if defined
  (_WIN32)` - the one place in the app that branches on OS at the source
  level.

Everything else (JUCE's audio I/O, GUI, and file handling) is already
cross-platform by virtue of going through JUCE rather than any OS API
directly - that's most of why this port didn't require touching much code.

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

```bash
./scripts/build.sh riyaaz_tests        # macOS/Linux/Git Bash
build/riyaaz_tests_artefacts/Debug/riyaaz_tests    # macOS/Linux
```

```powershell
cmake --build build --target riyaaz_tests --config Debug   # Windows (native PowerShell)
build\riyaaz_tests_artefacts\Debug\riyaaz_tests.exe
```
