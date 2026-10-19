cmake_minimum_required(VERSION 3.22)

project(Halo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.12
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(doctest)

# ---------------------------------------------------------------------------
# HaloCore: plain C++20 with no JUCE dependency. Everything here is ported from
# the Mac app's pure-Swift logic and is unit tested on its own.
# ---------------------------------------------------------------------------
add_library(HaloCore STATIC
    Source/Core/DisplayReducer.cpp
    Source/Core/MidiEvent.cpp)
target_include_directories(HaloCore PUBLIC Source)
if(MSVC)
    target_compile_options(HaloCore PRIVATE /W4 /permissive-)
endif()

# ---------------------------------------------------------------------------
# Halo: the native Windows app.
# ---------------------------------------------------------------------------
juce_add_gui_app(Halo
    PRODUCT_NAME "Halo"
    COMPANY_NAME "stillshining"
    VERSION ${PROJECT_VERSION})

target_sources(Halo PRIVATE
    Source/App/Main.cpp
    Source/App/MainComponent.cpp
    Source/App/AppModel.cpp
    Source/Device/Ep40MidiConnection.cpp
    Source/Stage/HaloMesh.cpp
    Source/Stage/DisplayRenderer.cpp
    Source/Stage/StageComponent.cpp
    Source/UI/HaloLookAndFeel.cpp
    Source/UI/StatusBar.cpp
    Source/UI/ModeBar.cpp
    Source/UI/PlayRail.cpp)

target_compile_definitions(Halo PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Halo,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Halo,JUCE_VERSION>")

juce_add_binary_data(HaloAssets
    HEADER_NAME HaloAssets.h
    NAMESPACE HaloAssets
    SOURCES
        assets/EP40.hmesh
        assets/halo_screen_indicators.png)

target_link_libraries(Halo
    PRIVATE
        HaloCore
        HaloAssets
        juce::juce_gui_extra
        juce::juce_opengl
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_executable(HaloTests
    Tests/TestMain.cpp
    Tests/Ep40MidiMappingTests.cpp
    Tests/PadHoldRegistryTests.cpp
    Tests/RingAndLifecycleTests.cpp
    Tests/DisplayReducerTests.cpp)
target_link_libraries(HaloTests PRIVATE HaloCore doctest::doctest)
# Newer MSVC headers clash with doctest's forward declarations of std types.
target_compile_definitions(HaloTests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
add_test(NAME HaloTests COMMAND HaloTests)
