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

rem The icon gets a content-hashed name: Windows caches shortcut icons by path,
rem so a new icon under an old name keeps showing the old one.
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$dest = '%DEST%'; $src = '%ROOT%\assets\icon\halo.ico';" ^
  "$hash = (Get-FileHash $src -Algorithm SHA256).Hash.Substring(0, 8).ToLower();" ^
  "$icon = Join-Path $dest ('halo-' + $hash + '.ico');" ^
  "Get-ChildItem $dest -Filter 'halo*.ico' | Where-Object { $_.FullName -ne $icon } | Remove-Item -Force;" ^
  "Copy-Item $src $icon -Force;" ^
  "$s = New-Object -ComObject WScript.Shell;" ^
  "foreach ($dir in @([Environment]::GetFolderPath('Desktop'), (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'))) {" ^
  "  $path = Join-Path $dir 'Halo.lnk'; Remove-Item $path -Force -ErrorAction SilentlyContinue;" ^
  "  $l = $s.CreateShortcut($path);" ^
  "  $l.TargetPath = (Join-Path $dest 'Halo.exe'); $l.WorkingDirectory = $dest;" ^
  "  $l.IconLocation = $icon + ',0'; $l.Description = 'Halo: EP-40 companion'; $l.Save() }" || exit /b 1
ie4uinit.exe -show >nul 2>&1

echo Installed Halo to %DEST% with Desktop and Start menu shortcuts.
exit /b 0
