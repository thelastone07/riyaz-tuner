#!/usr/bin/env bash
# One-command build for a fresh machine: fetches ONNX Runtime, bootstraps a
# local vcpkg if none is set up, configures with CMake+Ninja, and builds the
# app. Safe to re-run - every step is idempotent.
#
# Usage: scripts/build.sh [cmake --build target, default: riyaaz_app]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_TARGET="${1:-riyaaz_app}"

require() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "build.sh: missing required tool '$1'." >&2
        echo "$2" >&2
        exit 1
    fi
}

case "$(uname -s)" in
    Darwin)
        require cmake   "Install it with: brew install cmake"
        require ninja   "Install it with: brew install ninja"
        require git     "Install Xcode Command Line Tools: xcode-select --install"
        require curl    "curl ships with macOS - if it's missing, something else is very wrong."
        ;;
    Linux)
        require cmake   "Install it with your package manager, e.g.: sudo apt install cmake"
        require ninja   "Install it with your package manager, e.g.: sudo apt install ninja-build"
        require git     "Install it with your package manager, e.g.: sudo apt install git"
        require curl    "Install it with your package manager, e.g.: sudo apt install curl"
        ;;
    *)
        require cmake "Install CMake and make sure it's on PATH."
        require ninja "Install Ninja and make sure it's on PATH."
        require git   "Install Git and make sure it's on PATH."
        require curl  "Install curl and make sure it's on PATH."
        ;;
esac

# --- vcpkg: use an existing VCPKG_ROOT if set, otherwise bootstrap a local
# checkout under third_party/vcpkg (gitignored, same as everything else
# under third_party/). ---
if [ -z "${VCPKG_ROOT:-}" ]; then
    VCPKG_ROOT="${REPO_ROOT}/third_party/vcpkg"
    if [ ! -d "${VCPKG_ROOT}" ]; then
        echo "build.sh: VCPKG_ROOT not set - bootstrapping a local vcpkg at ${VCPKG_ROOT}"
        mkdir -p "${REPO_ROOT}/third_party"
        git clone --depth 1 https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"
        if [ -f "${VCPKG_ROOT}/bootstrap-vcpkg.sh" ]; then
            "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
        else
            "${VCPKG_ROOT}/bootstrap-vcpkg.bat" -disableMetrics
        fi
    fi
fi
echo "build.sh: using VCPKG_ROOT=${VCPKG_ROOT}"

# --- ONNX Runtime ---
"${SCRIPT_DIR}/fetch-onnxruntime.sh"

# The CREPE pitch model (models/crepe/small.onnx) is committed to the repo
# directly - nothing to fetch for it. This check only fires if that ever
# stops being true (e.g. a shallow/partial clone, or the file is deleted).
if [ ! -f "${REPO_ROOT}/models/crepe/small.onnx" ]; then
    echo ""
    echo "build.sh: WARNING - models/crepe/small.onnx not found."
    echo "It's normally committed to the repo - check your clone is complete."
    echo "The app builds fine without it but won't be able to detect pitch at runtime."
    echo ""
fi

# --- configure + build ---
cd "${REPO_ROOT}"
cmake -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target "${BUILD_TARGET}" --config Debug

echo ""
echo "build.sh: done. Built target: ${BUILD_TARGET}"
case "$(uname -s)" in
    Darwin) echo "Run it with: build/riyaaz_app_artefacts/Debug/Riyaaz.app/Contents/MacOS/Riyaaz" ;;
    Linux)  echo "Run it with: build/riyaaz_app_artefacts/Debug/Riyaaz" ;;
    *)      echo "Run it with: build/riyaaz_app_artefacts/Debug/Riyaaz.exe" ;;
esac
