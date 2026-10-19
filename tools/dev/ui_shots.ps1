# UI screenshot harness (docs/redesign/plan.md section 6.1).
#
# For every shot in the matrix it starts its OWN MOCK instance of the branch
# build inside a sandboxed data root, waits for HALO_UI_READY in that
# instance's log, captures the window by process id, then stops that process.
# It never opens the real EP-40 (HALO_MOCK_MIDI=1), never targets a running
# Halo by name, and never sets HALO_DEVTEST / HALO_AUTOMONITOR / HALO_AUTORECORD.
#
# Examples:
#   powershell -File tools\dev\ui_shots.ps1 -Milestone r0
#   powershell -File tools\dev\ui_shots.ps1 -Milestone r5 -Modes 1 -Sizes "1440,900;1180,720" -EnvList "HALO_RAIL=closed" -Suffix closed
#   powershell -File tools\dev\ui_shots.ps1 -Milestone r2a -Gallery -Scales "1,1.25,1.5"
param(
    [string]   $Milestone = "dev",
    [string[]] $Sizes = @("1440,900", "1180,720", "1180,660"),
    [string[]] $Modes = @("1", "2", "3", "4", "5", "6"),
    [string[]] $Scales = @("1"),
    [hashtable]$Env = @{},
    [string]   $EnvList = "",
    [string]   $Suffix = "",
    [switch]   $Gallery,
    [string[]] $GalleryPages = @("1", "2"),
    [string]   $Exe = "build\Redesign\Halo_artefacts\Debug\Halo.exe",
    [int]      $TimeoutSec = 40,
    [int]      $SettleMs = 900,
    [int]      $StageSettleMs = 2500
)

$ErrorActionPreference = "Stop"

# "powershell -File" hands lists over as one string: accept "1,2,3" and
# "1440,900;1180,720" as well as real arrays. EnvList is "K=V;K2=V2".
$Sizes = @($Sizes | ForEach-Object { $_ -split ";" } | Where-Object { $_ })
$Scales = @($Scales | ForEach-Object { $_ -split "[,;]" } | Where-Object { $_ })
$Modes = @($Modes | ForEach-Object { $_ -split "[,;]" } | Where-Object { $_ })
$GalleryPages = @($GalleryPages | ForEach-Object { $_ -split "[,;]" } | Where-Object { $_ })
foreach ($pair in ($EnvList -split ";" | Where-Object { $_ })) {
    $k, $v = $pair -split "=", 2
    $Env[$k.Trim()] = $v
}
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $root

foreach ($bad in "HALO_DEVTEST", "HALO_AUTOMONITOR", "HALO_AUTORECORD") {
    if ([Environment]::GetEnvironmentVariable($bad)) { throw "ui_shots refuses to run with $bad set" }
    if ($Env.ContainsKey($bad)) { throw "ui_shots refuses to set $bad" }
}
if (-not (Test-Path $Exe)) { throw "no build at $Exe (set HALO_BUILD_DIR=build\Redesign and run scripts\build.cmd build)" }

$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$sandbox = Join-Path $root "build\shots\sandbox\$runId"
$outDir = Join-Path $root "build\shots\ui\$Milestone"
$logDir = Join-Path $sandbox "logs"
New-Item -ItemType Directory -Force $outDir, $logDir | Out-Null

# The owner's real data folders: nothing may appear there during a run.
$watched = @(
    (Join-Path ([Environment]::GetFolderPath("MyMusic")) "Halo"),
    (Join-Path ([Environment]::GetFolderPath("MyDocuments")) "Halo"),
    (Join-Path ([Environment]::GetFolderPath("ApplicationData")) "Halo")
)
function Snapshot {
    $files = @{}
    foreach ($w in $watched) {
        if (Test-Path $w) {
            Get-ChildItem $w -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object { $files[$_.FullName] = $_.LastWriteTimeUtc.Ticks }
        }
    }
    return $files
}
$before = Snapshot

if ($Gallery) { $Modes = $GalleryPages }
$shots = 0
foreach ($size in $Sizes) {
    foreach ($scale in $Scales) {
        foreach ($mode in $Modes) {
            $w, $h = $size.Split(",")
            $name = if ($Gallery) { "gallery$mode" } else { "m$mode" }
            $name += "-${w}x$h"
            if ($scale -ne "1") { $name += "-s" + ($scale -replace "\.", "") }
            if ($Suffix) { $name += "-$Suffix" }
            $log = Join-Path $logDir "$name.log"

            $psi = New-Object System.Diagnostics.ProcessStartInfo
            $psi.FileName = (Resolve-Path $Exe).Path
            $psi.UseShellExecute = $false
            $psi.WorkingDirectory = $root
            foreach ($bad in "HALO_DEVTEST", "HALO_AUTOMONITOR", "HALO_AUTORECORD") { [void]$psi.EnvironmentVariables.Remove($bad) }
            $psi.EnvironmentVariables["HALO_MOCK_MIDI"] = "1"
            $psi.EnvironmentVariables["HALO_UI_SHOTS"] = "1"
            $psi.EnvironmentVariables["HALO_DATA_ROOT"] = $sandbox
            $psi.EnvironmentVariables["HALO_UI_LOG"] = $log
            $psi.EnvironmentVariables["HALO_WINDOW_SIZE"] = "$w,$h"
            if ($scale -ne "1") { $psi.EnvironmentVariables["HALO_UI_SCALE"] = $scale }
            if ($Gallery) { $psi.EnvironmentVariables["HALO_UI_GALLERY"] = "$mode" }
            else { $psi.EnvironmentVariables["HALO_MODE"] = "$mode" }
            foreach ($k in $Env.Keys) { $psi.EnvironmentVariables[$k] = [string]$Env[$k] }

            $p = [System.Diagnostics.Process]::Start($psi)
            try {
                $deadline = (Get-Date).AddSeconds($TimeoutSec)
                while (-not ((Test-Path $log) -and (Select-String -Path $log -Pattern "HALO_UI_READY" -Quiet))) {
                    if ((Get-Date) -gt $deadline) { throw "$name : no HALO_UI_READY within $TimeoutSec s" }
                    if ($p.HasExited) { throw "$name : Halo exited early (code $($p.ExitCode))" }
                    Start-Sleep -Milliseconds 200
                }
                # The stage needs a few frames (HALO_STAGE_READY arrives with the 3D work).
                $settle = if ($mode -eq "1" -and -not $Gallery) { $StageSettleMs } else { $SettleMs }
                Start-Sleep -Milliseconds $settle
                $p.Refresh()
                $out = Join-Path $outDir "$name.png"
                & powershell -NoProfile -File (Join-Path $PSScriptRoot "screenshot.ps1") -ProcessId $p.Id -Out $out | Out-Host
                $shots++
            }
            finally {
                if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
                $p.WaitForExit(5000) | Out-Null
            }
        }
    }
}

$after = Snapshot
# New files are a sandbox leak. A modified file may just be the owner's daily
# Halo running alongside (its protocol trace and settings), so it's reported only.
$created = @($after.Keys | Where-Object { -not $before.ContainsKey($_) })
$modified = @($after.Keys | Where-Object { $before.ContainsKey($_) -and $before[$_] -ne $after[$_] })
if ($modified.Count -gt 0) {
    Write-Warning "files modified in the owner's Halo folders during the run (a daily Halo may be running):"
    $modified | ForEach-Object { Write-Warning "  $_" }
}
if ($created.Count -gt 0) {
    Write-Warning "NEW files in the owner's Halo folders during the run (sandbox leak):"
    $created | ForEach-Object { Write-Warning "  $_" }
    exit 2
}
"ui_shots: $shots shot(s) in $outDir; sandbox $sandbox; no new files in Music\Halo, Documents\Halo or AppData\Halo"
