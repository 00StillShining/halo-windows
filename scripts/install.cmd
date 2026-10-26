@echo off
rem Installs Halo for everyday use: a Release build copied to
rem %LOCALAPPDATA%\Programs\Halo, plus a "Halo" shortcut on the desktop and in
rem the Start menu with the Halo icon. Re-run it after pulling or changing code.
rem Close Halo first (the running exe can't be replaced).
setlocal

set "ROOT=%~dp0.."
set "HALO_BUILD_DIR=%ROOT%\build\Release"
set "DEST=%LOCALAPPDATA%\Programs\Halo"

tasklist /FI "IMAGENAME eq Halo.exe" 2>nul | find /I "Halo.exe" >nul
if not errorlevel 1 (
    echo Halo is running. Close it, then run this again.
    exit /b 1
)

call "%~dp0build.cmd" build Release || exit /b 1

if not exist "%DEST%" mkdir "%DEST%"
copy /Y "%HALO_BUILD_DIR%\Halo_artefacts\Release\Halo.exe" "%DEST%\Halo.exe" >nul || exit /b 1
copy /Y "%ROOT%\assets\icon\halo.ico" "%DEST%\halo.ico" >nul || exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$s = New-Object -ComObject WScript.Shell;" ^
  "foreach ($dir in @([Environment]::GetFolderPath('Desktop'), (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'))) {" ^
  "  $l = $s.CreateShortcut((Join-Path $dir 'Halo.lnk'));" ^
  "  $l.TargetPath = '%DEST%\Halo.exe'; $l.WorkingDirectory = '%DEST%';" ^
  "  $l.IconLocation = '%DEST%\halo.ico,0'; $l.Description = 'Halo: EP-40 companion'; $l.Save() }" || exit /b 1

echo Installed Halo to %DEST% with Desktop and Start menu shortcuts.
exit /b 0
