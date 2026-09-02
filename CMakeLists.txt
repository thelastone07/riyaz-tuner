cmake_minimum_required(VERSION 3.24)
project(riyaaz VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(JUCE CONFIG REQUIRED)

# ONNX Runtime is vendored as a prebuilt release under third_party/ (see
# scripts/fetch-onnxruntime.sh, which downloads the right one for the host
# platform) rather than built from source - see TODOS.md's "ONNX Runtime
# dependency" note for why (an unresolved upstream MASM bug blocks building
# it via vcpkg on Windows; the same vendor-a-release approach is simplest to
# keep identical across platforms rather than building from source on some
# and vendoring on others). Each platform branch below picks the matching
# third_party/ subdirectory and the runtime file's platform-specific name.
if (WIN32)
    set(ONNXRUNTIME_DIR_NAME "onnxruntime-win-x64-1.23.2")
    set(ONNXRUNTIME_LINK_LIB "onnxruntime.lib")   # import lib - link against this
    set(ONNXRUNTIME_RUNTIME_FILE "onnxruntime.dll") # actual runtime - must sit next to the .exe
elseif (APPLE)
    set(ONNXRUNTIME_DIR_NAME "onnxruntime-osx-universal2-1.23.2") # one build for both Intel and Apple Silicon
    set(ONNXRUNTIME_LINK_LIB "libonnxruntime.dylib")
    set(ONNXRUNTIME_RUNTIME_FILE "libonnxruntime.dylib")
elseif (UNIX)
    set(ONNXRUNTIME_DIR_NAME "onnxruntime-linux-x64-1.23.2")
    set(ONNXRUNTIME_LINK_LIB "libonnxruntime.so")
    set(ONNXRUNTIME_RUNTIME_FILE "libonnxruntime.so")
else()
    message(FATAL_ERROR "riyaaz: no ONNX Runtime vendoring rule for this platform - add one above (and to scripts/fetch-onnxruntime.sh)")
endif()

set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/${ONNXRUNTIME_DIR_NAME}")

if (NOT EXISTS "${ONNXRUNTIME_ROOT}/lib/${ONNXRUNTIME_LINK_LIB}")
    message(FATAL_ERROR
        "riyaaz: ONNX Runtime not found at ${ONNXRUNTIME_ROOT}. "
        "Run scripts/fetch-onnxruntime.sh first (bash - Git Bash/WSL on Windows, or a plain shell on macOS/Linux).")
endif()

# Copies the ONNX Runtime shared library next to a target's built executable
# (on macOS, that's Riyaaz.app/Contents/MacOS/ - CMake's TARGET_FILE_DIR
# generator expression resolves inside the bundle automatically). Call once
# per executable target that transitively links riyaaz_pitchengine (it will
# not launch without this - the shared library must be discoverable at
# runtime).
function(riyaaz_copy_onnxruntime_dll target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_ROOT}/lib/${ONNXRUNTIME_RUNTIME_FILE}"
            "$<TARGET_FILE_DIR:${target}>"
    )
    if (APPLE)
        # macOS has no equivalent of Windows' "next to the .exe is enough" -
        # a copied-in dylib is only found at load time if the executable's
        # own rpath says to look next to itself. Without this, the app
        # links fine but fails to LAUNCH (dyld: Library not loaded).
        set_target_properties(${target} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@executable_path")
    endif()
endfunction()

# Copies the CREPE pitch model next to a target's built executable, mirroring
# riyaaz_copy_onnxruntime_dll above - CrepePitchEngine's default model path is
# resolved relative to the executable's own directory (see
# MainComponent::resolveCrepeModelPath()), which only works if the model
# actually lives there. Without this, the app only finds the model when its
# working directory happens to be the repo root (e.g. launched from a
# terminal at the repo root) - a desktop shortcut has no such guarantee.
function(riyaaz_copy_crepe_model target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target}>/models/crepe"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/models/crepe/small.onnx"
            "$<TARGET_FILE_DIR:${target}>/models/crepe/small.onnx"
    )
endfunction()

add_library(riyaaz_swarmap STATIC
    src/audio/swarmap/SwarMapper.cpp
)
target_include_directories(riyaaz_swarmap PUBLIC src)
target_link_libraries(riyaaz_swarmap PUBLIC juce::juce_core)
target_compile_definitions(riyaaz_swarmap PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)

add_library(riyaaz_pitchengine STATIC
    src/audio/pitchengine/CrepeDecode.cpp
    src/audio/pitchengine/PitchContinuityFilter.cpp
    src/audio/pitchengine/CrepePitchEngine.cpp
)
target_include_directories(riyaaz_pitchengine PUBLIC src)
target_include_directories(riyaaz_pitchengine PUBLIC "${ONNXRUNTIME_ROOT}/include")
target_link_libraries(riyaaz_pitchengine PUBLIC juce::juce_core juce::juce_dsp)
target_link_libraries(riyaaz_pitchengine PUBLIC "${ONNXRUNTIME_ROOT}/lib/${ONNXRUNTIME_LINK_LIB}")
target_compile_definitions(riyaaz_pitchengine PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)

add_library(riyaaz_calibration STATIC
    src/audio/calibration/TonicCalibrator.cpp
)
target_include_directories(riyaaz_calibration PUBLIC src)
target_link_libraries(riyaaz_calibration PUBLIC juce::juce_core)
target_compile_definitions(riyaaz_calibration PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)

add_library(riyaaz_pipeline STATIC
    src/audio/pipeline/PitchPipeline.cpp
)
target_include_directories(riyaaz_pipeline PUBLIC src)
target_link_libraries(riyaaz_pipeline PUBLIC riyaaz_calibration riyaaz_swarmap juce::juce_core)
target_compile_definitions(riyaaz_pipeline PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)

add_library(riyaaz_worker STATIC
    src/audio/worker/PitchWorker.cpp
)
target_include_directories(riyaaz_worker PUBLIC src)
target_link_libraries(riyaaz_worker PUBLIC riyaaz_pipeline juce::juce_core juce::juce_events)
target_compile_definitions(riyaaz_worker PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)

add_library(riyaaz_tanpura STATIC
    src/audio/tanpura/KarplusStrongString.cpp
    src/audio/tanpura/TanpuraSynth.cpp
    src/audio/tanpura/TanpuraAudioSource.cpp
)
target_include_directories(riyaaz_tanpura PUBLIC src)
target_link_libraries(riyaaz_tanpura PUBLIC juce::juce_core juce::juce_audio_basics)
target_compile_definitions(riyaaz_tanpura PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)

add_library(riyaaz_metronome STATIC
    src/audio/metronome/TaalPattern.cpp
    src/audio/metronome/MetronomeClick.cpp
    src/audio/metronome/MetronomeAudioSource.cpp
)
target_include_directories(riyaaz_metronome PUBLIC src)
target_link_libraries(riyaaz_metronome PUBLIC juce::juce_core juce::juce_audio_basics)
target_compile_definitions(riyaaz_metronome PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)

add_library(riyaaz_practice STATIC
    src/practice/AlankarPattern.cpp
    src/practice/AlankarPracticeEngine.cpp
)
target_include_directories(riyaaz_practice PUBLIC src)
target_link_libraries(riyaaz_practice PUBLIC juce::juce_core riyaaz_swarmap)
target_compile_definitions(riyaaz_practice PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)

add_library(riyaaz_profile STATIC
    src/profile/ProfileStore.cpp
    src/profile/SessionStore.cpp
    src/profile/SessionAnalytics.cpp
)
target_include_directories(riyaaz_profile PUBLIC src)
target_link_libraries(riyaaz_profile PUBLIC juce::juce_core)
target_compile_definitions(riyaaz_profile PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)

juce_add_console_app(riyaaz_tests PRODUCT_NAME "Riyaaz Tests")
target_sources(riyaaz_tests PRIVATE
    src/audio/swarmap/SwarMapperTests.cpp
    src/audio/pitchengine/CrepeDecodeTests.cpp
    src/audio/pitchengine/PitchContinuityFilterTests.cpp
    src/audio/pitchengine/CrepePitchEngineTests.cpp
    src/audio/calibration/TonicCalibratorTests.cpp
    src/audio/pipeline/PitchPipelineTests.cpp
    src/audio/worker/PitchWorkerTests.cpp
    src/audio/tanpura/KarplusStrongStringTests.cpp
    src/audio/tanpura/TanpuraSynthTests.cpp
    src/audio/tanpura/TanpuraAudioSourceTests.cpp
    src/audio/metronome/TaalPatternTests.cpp
    src/audio/metronome/MetronomeClickTests.cpp
    src/audio/metronome/MetronomeAudioSourceTests.cpp
    src/practice/AlankarPatternTests.cpp
    src/practice/AlankarPracticeEngineTests.cpp
    src/profile/ProfileStoreTests.cpp
    src/profile/SessionStoreTests.cpp
    src/profile/SessionAnalyticsTests.cpp
    src/ui/PitchGraphComponent.cpp
    src/ui/PitchGraphPointBufferTests.cpp
    src/TestMain.cpp
)
target_compile_definitions(riyaaz_tests PRIVATE
    # Required for juce::MessageManager::runDispatchLoopUntil(), which
    # PitchWorkerTests.cpp uses to pump the message loop so AsyncUpdater
    # callbacks are actually delivered in the console test runner. JUCE
    # 8.0.7 defaults this to 0 (juce_PlatformDefs.h) unless a target defines
    # it. Scoped PRIVATE to riyaaz_tests only - NOT on riyaaz_worker - so a
    # future riyaaz_app target that links riyaaz_worker does not inherit
    # JUCE's modal dialog/loop APIs (AlertWindow::runModalLoop,
    # FileChooser::browseForFileToOpen, etc.) just by linking it. Do not
    # move this back to riyaaz_worker PUBLIC.
    JUCE_MODAL_LOOPS_PERMITTED=1
)
target_link_libraries(riyaaz_tests PRIVATE
    riyaaz_swarmap
    riyaaz_pitchengine
    riyaaz_calibration
    riyaaz_pipeline
    riyaaz_worker
    riyaaz_tanpura
    riyaaz_metronome
    riyaaz_practice
    riyaaz_profile
    juce::juce_core
    juce::juce_gui_basics
)
riyaaz_copy_onnxruntime_dll(riyaaz_tests)
riyaaz_copy_crepe_model(riyaaz_tests)

# macOS requires an explicit Info.plist entitlement (with a user-facing
# reason string) before it will even show the mic-access permission prompt -
# without this the app would silently get empty/no input, with no dialog to
# explain why. JUCE's CMake API bakes this into the generated Info.plist via
# these two keywords, recognized only on Apple platforms; harmless to omit
# elsewhere, so this list is just left empty (and therefore a no-op) there.
set(RIYAAZ_MIC_PERMISSION_ARGS "")
if (APPLE)
    set(RIYAAZ_MIC_PERMISSION_ARGS
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Riyaaz needs microphone access to track your singing pitch.")
endif()

juce_add_gui_app(riyaaz_app PRODUCT_NAME "Riyaaz"
    ICON_BIG "${CMAKE_SOURCE_DIR}/assets/icon.png"
    ${RIYAAZ_MIC_PERMISSION_ARGS})
target_sources(riyaaz_app PRIVATE
    src/app/MainComponent.cpp
    src/app/Main.cpp
    # PitchGraphComponent has no dedicated riyaaz_* library target of its own
    # (it's currently only ever compiled as a loose source, directly into
    # riyaaz_tests above) - MainComponent.h uses it as a member, so its .cpp
    # must be compiled into riyaaz_app too, or the linker can't find its
    # constructor/addPoint()/paint() symbols.
    src/ui/PitchGraphComponent.cpp
    src/ui/BeatIndicatorComponent.cpp
    src/ui/AnalyticsComponent.cpp
    src/ui/RiyaazLookAndFeel.cpp
    src/ui/SwarChipRowComponent.cpp
    src/profile/ProfilePickerComponent.cpp
)
target_link_libraries(riyaaz_app PRIVATE
    riyaaz_pipeline
    riyaaz_pitchengine
    riyaaz_worker
    riyaaz_swarmap
    riyaaz_tanpura
    riyaaz_metronome
    riyaaz_practice
    riyaaz_profile
    juce::juce_audio_utils
    juce::juce_gui_extra
)
riyaaz_copy_onnxruntime_dll(riyaaz_app)
riyaaz_copy_crepe_model(riyaaz_app)
