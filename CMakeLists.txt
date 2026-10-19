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
    Source/Core/Chop.cpp
    Source/Core/DisplayReducer.cpp
    Source/Core/DubRack.cpp
    Source/Core/GrabMath.cpp
    Source/Core/MidiEvent.cpp
    Source/Core/SampleCore.cpp
    Source/Core/SysEx.cpp
    Source/Core/TeProtocol.cpp)
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
    Source/App/EditSession.cpp
    Source/App/Library.cpp
    Source/Audio/AuditionPlayer.cpp
    Source/Audio/GrabController.cpp
    Source/Audio/MonitorEngine.cpp
    Source/Audio/SampleIO.cpp
    Source/Audio/SessionRecorder.cpp
    Source/Device/DeviceClient.cpp
    Source/Device/Ep40Backup.cpp
    Source/Device/Ep40Library.cpp
    Source/Device/Ep40MidiConnection.cpp
    Source/Device/Ep40Writer.cpp
    Source/Stage/HaloMesh.cpp
    Source/Stage/DisplayRenderer.cpp
    Source/Stage/StageComponent.cpp
    Source/UI/HaloLookAndFeel.cpp
    Source/UI/StatusBar.cpp
    Source/UI/ModeBar.cpp
    Source/UI/PlayRail.cpp
    Source/UI/MonitorPanel.cpp
    Source/UI/RackPanel.cpp
    Source/UI/WaveformView.cpp
    Source/UI/LibraryPanel.cpp
    Source/UI/EditPanel.cpp
    Source/UI/ChopSurface.cpp
    Source/UI/LoadPanel.cpp
    Source/UI/BackupsPanel.cpp)

target_compile_definitions(Halo PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
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
        juce::juce_audio_formats
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
    Tests/DisplayReducerTests.cpp
    Tests/MonitorDspTests.cpp
    Tests/GrabTests.cpp
    Tests/DubRackTests.cpp
    Tests/SampleTests.cpp
    Tests/SysExTests.cpp
    Tests/TeProtocolTests.cpp)
target_link_libraries(HaloTests PRIVATE HaloCore doctest::doctest)
# Newer MSVC headers clash with doctest's forward declarations of std types.
target_compile_definitions(HaloTests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
add_test(NAME HaloTests COMMAND HaloTests)

# ---------------------------------------------------------------------------
# UI redesign (docs/redesign/plan.md). Kept in its own blocks so it merges
# cleanly with the 3D stage work, which keeps its own blocks too.
# ---------------------------------------------------------------------------
target_sources(HaloCore PRIVATE
    Source/Core/HonestState.cpp)

target_sources(HaloTests PRIVATE
    Tests/HonestStateTests.cpp)

# H3: no file under Source/UI may call an Ep40Writer write method outside the
# write gate (tools/dev/check_write_sites.py).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME check_write_sites
             COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/dev/check_write_sites.py ${CMAKE_SOURCE_DIR})
endif()

# Redesign: theme, embedded OFL fonts (assets/fonts, built by tools/fonts/build_fonts.py).
juce_add_binary_data(HaloFontAssets
    HEADER_NAME HaloFontAssets.h
    NAMESPACE HaloFontAssets
    SOURCES
        assets/fonts/LexendGiga-Light.ttf
        assets/fonts/LexendExa-Medium.ttf
        assets/fonts/LexendExa-SemiBold.ttf
        assets/fonts/InterTight-Light.ttf
        assets/fonts/InterTight-Regular.ttf
        assets/fonts/InterTight-Medium.ttf
        assets/fonts/InterTight-SemiBold.ttf
        assets/fonts/JetBrainsMono-Regular.ttf
        assets/fonts/DepartureMono-Regular.ttf)

# The redesign's design system, shared by the app and the UI test runner.
set(HALO_DESIGN_SOURCES
    Source/UI/Design/Theme.cpp
    Source/UI/Design/Fonts.cpp)
target_sources(Halo PRIVATE ${HALO_DESIGN_SOURCES})
target_link_libraries(Halo PRIVATE HaloFontAssets)

target_sources(HaloTests PRIVATE
    Tests/UiTokensTests.cpp)

# HaloUiTests: doctest with JUCE's GUI modules (fonts, UI state, the write gate).
juce_add_console_app(HaloUiTests PRODUCT_NAME "HaloUiTests")
target_sources(HaloUiTests PRIVATE
    Tests/Ui/UiTestMain.cpp
    Tests/Ui/FontsTests.cpp
    ${HALO_DESIGN_SOURCES})
target_compile_definitions(HaloUiTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    DOCTEST_CONFIG_USE_STD_HEADERS)
target_link_libraries(HaloUiTests PRIVATE
    HaloCore
    HaloFontAssets
    doctest::doctest
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
add_test(NAME HaloUiTests COMMAND HaloUiTests)
