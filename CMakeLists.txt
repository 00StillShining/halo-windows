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
# Pure logic behind the 3D stage (docs/redesign/riddim-3d-plan.md).
target_sources(HaloCore PRIVATE
    Source/Core/ChunkFile.cpp
    Source/Core/HenvFormat.cpp
    Source/Core/HtexFormat.cpp
    Source/Core/StageCamera.cpp)
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

# The 3D stage's own sources (docs/redesign/riddim-3d-plan.md), kept apart from
# the list above so stage and UI work merge without touching the same lines.
target_sources(Halo PRIVATE
    Source/Stage/Environment.cpp
    Source/Stage/GlProgram.cpp
    Source/Stage/RenderTargets.cpp
    Source/Stage/ShaderLibrary.cpp
    Source/Stage/StageLog.cpp
    Source/Stage/StageStats.cpp)

# The stage's GLSL, embedded; Debug builds can hot-reload it from
# HALO_STAGE_SHADER_DIR. The tone constants are shared with C++ (Core/ToneCurves.h).
juce_add_binary_data(HaloShaders
    HEADER_NAME HaloShaders.h
    NAMESPACE HaloShaders
    SOURCES
        Source/Core/ToneCurveConstants.inc
        Source/Stage/Shaders/backdrop.glsl
        Source/Stage/Shaders/background.frag
        Source/Stage/Shaders/card.frag
        Source/Stage/Shaders/common.glsl
        Source/Stage/Shaders/composite.frag
        Source/Stage/Shaders/depth.frag
        Source/Stage/Shaders/depth.vert
        Source/Stage/Shaders/floor.glsl
        Source/Stage/Shaders/fullscreen.vert
        Source/Stage/Shaders/fxaa.frag
        Source/Stage/Shaders/part.frag
        Source/Stage/Shaders/part.vert
        Source/Stage/Shaders/resolve.frag
        Source/Stage/Shaders/selftest.frag)
target_link_libraries(Halo PRIVATE HaloShaders)

# The stage's own assets (assets/stage), apart from HaloAssets so stage and UI
# changes do not touch the same lines.
juce_add_binary_data(HaloStageAssets
    HEADER_NAME HaloStageAssets.h
    NAMESPACE HaloStageAssets
    SOURCES
        assets/stage/studio.henv)
target_link_libraries(Halo PRIVATE HaloStageAssets)

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
# 3D stage tests (pure HaloCore logic: contract, poses, camera, tone curves...).
target_sources(HaloTests PRIVATE
    Tests/Ep40EntityNamesTests.cpp
    Tests/StagePoseTests.cpp
    Tests/StageCameraTests.cpp
    Tests/ToneCurvesTests.cpp
    Tests/HenvFormatTests.cpp
    Tests/HtexFormatTests.cpp)
target_compile_definitions(HaloTests PRIVATE HALO_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
# The Mac app (its own repo) sits beside this one as the reference spec; when it
# is there, the entity-name test cross-checks the frozen contract against it.
set(HALO_MAC_REFERENCE_DIR "${CMAKE_SOURCE_DIR}/reference/halo-mac" CACHE PATH "Mac app checkout (optional)")
if(EXISTS "${HALO_MAC_REFERENCE_DIR}/Halo/Reality/EP40EntityNames.swift")
    target_compile_definitions(HaloTests PRIVATE HALO_MAC_REFERENCE_DIR="${HALO_MAC_REFERENCE_DIR}")
endif()
target_link_libraries(HaloTests PRIVATE HaloCore doctest::doctest)
# Newer MSVC headers clash with doctest's forward declarations of std types.
target_compile_definitions(HaloTests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
add_test(NAME HaloTests COMMAND HaloTests)
