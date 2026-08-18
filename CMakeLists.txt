cmake_minimum_required(VERSION 3.24)
project(riyaaz VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(JUCE CONFIG REQUIRED)

set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-win-x64-1.23.2")

add_library(riyaaz_swarmap STATIC
    src/audio/swarmap/SwarMapper.cpp
)
target_include_directories(riyaaz_swarmap PUBLIC src)
target_link_libraries(riyaaz_swarmap PUBLIC juce::juce_core)
target_compile_definitions(riyaaz_swarmap PUBLIC
    JUCE_STANDALONE_APPLICATION=1
)

juce_add_console_app(riyaaz_tests PRODUCT_NAME "Riyaaz Tests")
target_sources(riyaaz_tests PRIVATE
    src/audio/swarmap/SwarMapperTests.cpp
    src/audio/pitchengine/CrepeDecode.cpp
    src/audio/pitchengine/CrepeDecodeTests.cpp
    src/audio/pitchengine/PitchContinuityFilter.cpp
    src/audio/pitchengine/PitchContinuityFilterTests.cpp
    src/audio/pitchengine/CrepePitchEngine.cpp
    src/audio/pitchengine/CrepePitchEngineTests.cpp
    src/audio/calibration/TonicCalibrator.cpp
    src/audio/calibration/TonicCalibratorTests.cpp
    src/TestMain.cpp
)
target_link_libraries(riyaaz_tests PRIVATE riyaaz_swarmap juce::juce_core juce::juce_dsp)

target_include_directories(riyaaz_tests PRIVATE "${ONNXRUNTIME_ROOT}/include")
target_link_libraries(riyaaz_tests PRIVATE "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")

# Copy the runtime DLL next to the built test executable so it can actually launch:
add_custom_command(TARGET riyaaz_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
        "$<TARGET_FILE_DIR:riyaaz_tests>"
)
