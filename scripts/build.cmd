@echo off
rem halo build helper for Windows.
rem Usage: scripts\build.cmd [build|test|run|clean] [Debug|Release]
setlocal

set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=build"
set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=Debug"

set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build\%CONFIG%"
rem HALO_BUILD_DIR builds elsewhere (e.g. a branch build beside the daily one).
if defined HALO_BUILD_DIR set "BUILD=%HALO_BUILD_DIR%"

rem Reuse the already-downloaded JUCE/doctest sources in extra build folders.
set "DEPS="
if exist "%ROOT%\build\Debug\_deps\juce-src\CMakeLists.txt" set "DEPS=-DFETCHCONTENT_SOURCE_DIR_JUCE=%ROOT%\build\Debug\_deps\juce-src -DFETCHCONTENT_SOURCE_DIR_DOCTEST=%ROOT%\build\Debug\_deps\doctest-src"

for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo Visual Studio Build Tools with the C++ workload were not found.
    exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if /i "%ACTION%"=="clean" (
    if exist "%BUILD%" rmdir /s /q "%BUILD%"
    exit /b 0
)

if not exist "%BUILD%\build.ninja" (
    cmake -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% %DEPS% || exit /b 1
)

if /i "%ACTION%"=="build" (
    cmake --build "%BUILD%" || exit /b 1
    exit /b 0
)
if /i "%ACTION%"=="test" (
    cmake --build "%BUILD%" --target HaloTests || exit /b 1
    "%BUILD%\HaloTests.exe" || exit /b 1
    rem The redesign's GUI-module tests and the H3 write-site lint, when present.
    cmake --build "%BUILD%" --target HaloUiTests && ctest --test-dir "%BUILD%" -C %CONFIG% --output-on-failure -E "^HaloTests$" || exit /b 1
    exit /b 0
)
if /i "%ACTION%"=="run" (
    cmake --build "%BUILD%" --target Halo || exit /b 1
    start "" "%BUILD%\Halo_artefacts\%CONFIG%\Halo.exe"
    exit /b 0
)

echo unknown action: %ACTION% (build^|test^|run^|clean)
exit /b 1
