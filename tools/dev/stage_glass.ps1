# Checks the screen sheen at every framing that shows the LCD: each shot is
# taken with the sheen and again with it removed (HALO_STAGE_GLASS=0), and
# compare_shots.py `glass` reports the sheen's peak and the LCD contrast that
# survives under it.
#
# Usage: powershell -File tools\dev\stage_glass.ps1 [-Milestone m4] [-Glass "0.5,0.08,0.12"]
param([string]$Milestone = "glass", [string]$Glass = "", [string]$Shots = "hero,keys,rack,small", [string]$Looks = "paper,dark")

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$py = Join-Path $repo "tools\.venv\Scripts\python.exe"
if (-not (Test-Path $py)) { $py = "C:\Users\ninen\Documents\halo-windows\tools\.venv\Scripts\python.exe" }
$withArgs = @("-Milestone", $Milestone, "-Shots", $Shots, "-Looks", $Looks)
if ($Glass -ne "") { $withArgs += @("-Env", "HALO_STAGE_GLASS=$Glass") }
& powershell -NoProfile -File (Join-Path $PSScriptRoot "stage_shots.ps1") @withArgs | Out-Null
& powershell -NoProfile -File (Join-Path $PSScriptRoot "stage_shots.ps1") -Milestone $Milestone -Shots $Shots -Looks $Looks -Env "HALO_STAGE_GLASS=0" -Suffix bare | Out-Null
$failed = 0
foreach ($look in ($Looks -split ",")) {
    foreach ($shot in ($Shots -split ",")) {
        $a = Join-Path $repo "build\shots\$Milestone\$shot-$look.png"
        $b = Join-Path $repo "build\shots\$Milestone\$shot-$look-bare.png"
        $line = & $py -I (Join-Path $PSScriptRoot "compare_shots.py") glass $a --bare $b
        if ($LASTEXITCODE -ne 0) { $failed++ }
        "{0,-12} {1}" -f "$shot-$look", $line
    }
}
if ($failed -gt 0) { exit 1 }
