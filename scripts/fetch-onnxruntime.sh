#!/usr/bin/env bash
# Downloads the prebuilt ONNX Runtime release for the host platform into
# third_party/ (gitignored - see TODOS.md's "ONNX Runtime dependency" note
# for why it's vendored as a release rather than built from source). Safe to
# re-run: skips the download if the target directory already exists.
#
# The directory names this script produces MUST match the
# ONNXRUNTIME_DIR_NAME values in CMakeLists.txt's platform branch - if you
# bump ONNXRUNTIME_VERSION here, bump the version string in every
# ONNXRUNTIME_DIR_NAME in CMakeLists.txt too.
set -euo pipefail

ONNXRUNTIME_VERSION="1.23.2"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
THIRD_PARTY_DIR="${REPO_ROOT}/third_party"

uname_s="$(uname -s)"
uname_m="$(uname -m)"

case "${uname_s}" in
    Darwin)
        # One universal2 build covers both Apple Silicon and Intel - no need
        # to branch on uname_m here.
        ARCHIVE_BASENAME="onnxruntime-osx-universal2-${ONNXRUNTIME_VERSION}"
        ARCHIVE_EXT="tgz"
        ;;
    Linux)
        case "${uname_m}" in
            x86_64)  ARCHIVE_BASENAME="onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}" ;;
            aarch64) ARCHIVE_BASENAME="onnxruntime-linux-aarch64-${ONNXRUNTIME_VERSION}" ;;
            *)
                echo "fetch-onnxruntime.sh: no known ONNX Runtime release for Linux/${uname_m}." >&2
                echo "Check https://github.com/microsoft/onnxruntime/releases/tag/v${ONNXRUNTIME_VERSION} for a matching asset," >&2
                echo "then extract it into third_party/ and add a case for ${uname_m} here." >&2
                exit 1
                ;;
        esac
        ARCHIVE_EXT="tgz"
        ;;
    MINGW*|MSYS*|CYGWIN*)
        # Git Bash / MSYS on Windows.
        ARCHIVE_BASENAME="onnxruntime-win-x64-${ONNXRUNTIME_VERSION}"
        ARCHIVE_EXT="zip"
        ;;
    *)
        echo "fetch-onnxruntime.sh: unrecognized platform '${uname_s}'." >&2
        exit 1
        ;;
esac

TARGET_DIR="${THIRD_PARTY_DIR}/${ARCHIVE_BASENAME}"

if [ -d "${TARGET_DIR}" ]; then
    echo "fetch-onnxruntime.sh: ${TARGET_DIR} already exists - nothing to do."
    echo "(delete it first if you want to re-download)"
    exit 0
fi

mkdir -p "${THIRD_PARTY_DIR}"
ARCHIVE_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/${ARCHIVE_BASENAME}.${ARCHIVE_EXT}"
ARCHIVE_PATH="${THIRD_PARTY_DIR}/${ARCHIVE_BASENAME}.${ARCHIVE_EXT}"

echo "fetch-onnxruntime.sh: downloading ${ARCHIVE_URL}"
curl -fL --progress-bar -o "${ARCHIVE_PATH}" "${ARCHIVE_URL}"

echo "fetch-onnxruntime.sh: extracting into ${THIRD_PARTY_DIR}"
if [ "${ARCHIVE_EXT}" = "tgz" ]; then
    tar -xzf "${ARCHIVE_PATH}" -C "${THIRD_PARTY_DIR}"
else
    if command -v unzip >/dev/null 2>&1; then
        unzip -q "${ARCHIVE_PATH}" -d "${THIRD_PARTY_DIR}"
    else
        # bsdtar (bundled with Git for Windows) also unpacks zip archives -
        # falls back to it when `unzip` itself isn't on PATH.
        tar -xf "${ARCHIVE_PATH}" -C "${THIRD_PARTY_DIR}"
    fi
fi

rm -f "${ARCHIVE_PATH}"

if [ ! -d "${TARGET_DIR}" ]; then
    echo "fetch-onnxruntime.sh: extraction did not produce the expected ${TARGET_DIR}." >&2
    echo "The archive layout may have changed upstream - check ${THIRD_PARTY_DIR} manually." >&2
    exit 1
fi

echo "fetch-onnxruntime.sh: done - ${TARGET_DIR}"
