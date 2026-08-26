cmake_minimum_required(VERSION 3.24)
project(riyaaz VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(JUCE CONFIG REQUIRED)

set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-win-x64-1.23.2")

# Copies onnxruntime.dll next to a target's built executable. Call once per
# executable target that transitively links riyaaz_pitchengine (it will not
# launch without this - onnxruntime.dll must be discoverable at runtime).
function(riyaaz_copy_onnxruntime_dll target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
            "$<TARGET_FILE_DIR:${target}>"
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
target_link_libraries(riyaaz_pitchengine PUBLIC "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
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
)
target_include_directories(riyaaz_metronome PUBLIC src)
target_link_libraries(riyaaz_metronome PUBLIC juce::juce_core)
target_compile_definitions(riyaaz_metronome PUBLIC
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
    juce::juce_core
    juce::juce_gui_basics
)
riyaaz_copy_onnxruntime_dll(riyaaz_tests)

juce_add_gui_app(riyaaz_app PRODUCT_NAME "Riyaaz")
target_sources(riyaaz_app PRIVATE
    src/app/MainComponent.cpp
    src/app/Main.cpp
    # PitchGraphComponent has no dedicated riyaaz_* library target of its own
    # (it's currently only ever compiled as a loose source, directly into
    # riyaaz_tests above) - MainComponent.h uses it as a member, so its .cpp
    # must be compiled into riyaaz_app too, or the linker can't find its
    # constructor/addPoint()/paint() symbols.
    src/ui/PitchGraphComponent.cpp
)
target_link_libraries(riyaaz_app PRIVATE
    riyaaz_pipeline
    riyaaz_pitchengine
    riyaaz_worker
    riyaaz_swarmap
    riyaaz_tanpura
    juce::juce_audio_utils
    juce::juce_gui_extra
)
riyaaz_copy_onnxruntime_dll(riyaaz_app)
