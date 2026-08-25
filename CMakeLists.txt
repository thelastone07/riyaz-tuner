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
    # Required for juce::MessageManager::runDispatchLoopUntil(), which
    # PitchWorkerTests.cpp uses to pump the message loop so AsyncUpdater
    # callbacks are actually delivered in the console test runner (see
    # Step 4 investigation in task-1-report.md). JUCE 8.0.7 defaults this to
    # 0 (juce_PlatformDefs.h) unless a target defines it; riyaaz_worker is
    # where juce_events' implementation gets compiled (riyaaz_tests links
    # riyaaz_worker rather than juce::juce_events directly), so the
    # definition has to live here, PUBLIC, so the declaration and the
    # compiled-in implementation stay consistent.
    JUCE_MODAL_LOOPS_PERMITTED=1
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
    src/TestMain.cpp
)
target_link_libraries(riyaaz_tests PRIVATE
    riyaaz_swarmap
    riyaaz_pitchengine
    riyaaz_calibration
    riyaaz_pipeline
    riyaaz_worker
    juce::juce_core
)
riyaaz_copy_onnxruntime_dll(riyaaz_tests)
