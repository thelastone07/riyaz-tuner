cmake_minimum_required(VERSION 3.24)
project(riyaaz VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(JUCE CONFIG REQUIRED)

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
    src/TestMain.cpp
)
target_link_libraries(riyaaz_tests PRIVATE riyaaz_swarmap juce::juce_core)
