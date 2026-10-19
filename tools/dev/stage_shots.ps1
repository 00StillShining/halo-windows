# Captures the 3D stage's canonical shot set (docs/redesign/riddim-3d-plan.md,
# section 8) into build\shots\<Milestone>\<shot>-<look>.png, each with a JSON
# sidecar giving the stage rectangle inside the PNG.
#
# Every shot runs its own MOCK instance (HALO_MOCK_MIDI=1, which never opens the
# EP-40's MIDI ports and may run beside a daily instance), with its own
# HALO_STAGE_LOG; it waits for HALO_STAGE_READY in *that* log, captures *that*
# process by id, and stops only that process.
#
# Usage:
#   powershell -File tools\dev\stage_shots.ps1 -Milestone m0
#   powershell -File tools\dev\stage_shots.ps1 -Milestone m1 -Shots hero,low -Looks paper,dark
#   powershell -File tools\dev\stage_shots.ps1 -Milestone m2 -Shots hero -Env "HALO_STAGE_TONEMAP=agx" -Suffix agx
#   powershell -File tools\dev\stage_shots.ps1 -Milestone m0 -Shots hero -Stats -HoldSeconds 12   (timing; use a Release exe)
#
# Shots: hero rack small top low keys edge hit card mask albedo emissive, or any
# preset name. "small" is the app's minimum window (1180x720) with the rail open:
# the app does not allow the plan's 900x640.
param(
    [string]$Milestone = "scratch",
    [string[]]$Shots = @("hero", "rack", "small", "top", "low", "keys", "edge", "hit", "card", "mask", "albedo"),
    [string[]]$Looks = @("paper"),
    [string]$Exe = "",
    [string]$Pose = "rest",
    [string]$Env = "",   # extra variables, "NAME=value;NAME2=value2"
    [string]$Suffix = "",
    [int]$TimeoutSec = 40,
    [switch]$Stats,
    [int]$HoldSeconds = 0
)

$ErrorActionPreference = "Stop"
# "powershell -File" passes "hero,low" as one string; accept both forms.
$Shots = @($Shots | ForEach-Object { $_ -split "," } | Where-Object { $_ -ne "" })
$Looks = @($Looks | ForEach-Object { $_ -split "," } | Where-Object { $_ -ne "" })
$extra = @{}
foreach ($pair in ($Env -split ";")) { if ($pair -match "^\s*([^=]+)=(.*)$") { $extra[$Matches[1].Trim()] = $Matches[2] } }
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ($Exe -eq "") {
    $Exe = Join-Path $repo "build\Stage\Halo_artefacts\Debug\Halo.exe"
    if (-not (Test-Path $Exe)) { $Exe = Join-Path $repo "build\Debug\Halo_artefacts\Debug\Halo.exe" }
}
$Exe = (Resolve-Path $Exe).Path
$outDir = Join-Path $repo "build\shots\$Milestone"
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class HaloShots {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int w, int h, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[void][HaloShots]::SetProcessDPIAware()

# name -> window size, preset, debug view, extra env, pose override
$defs = @{
    hero     = @{ size = @(1456, 939);  preset = "hero" }
    rack     = @{ size = @(1294, 1447); preset = "hero"; env = @{ HALO_MODE = "5" } }
    small    = @{ size = @(1180, 720);  preset = "hero"; env = @{ HALO_RAIL = "open" } }
    top      = @{ size = @(1456, 939);  preset = "top" }
    low      = @{ size = @(1456, 939);  preset = "low" }
    keys     = @{ size = @(1456, 939);  preset = "keys" }
    edge     = @{ size = @(1456, 939);  preset = "edge" }
    hit      = @{ size = @(1456, 939);  preset = "hero"; pose = "pad7=1.0,group=1" }
    card     = @{ size = @(1456, 939);  preset = "hero"; debug = "5" }
    mask     = @{ size = @(1456, 939);  preset = "hero"; debug = "6" }
    maskrack = @{ size = @(1294, 1447); preset = "hero"; debug = "6"; env = @{ HALO_MODE = "5" } }
    masksmall = @{ size = @(1180, 720); preset = "hero"; debug = "6"; env = @{ HALO_RAIL = "open" } }
    albedo   = @{ size = @(1456, 939);  preset = "top";  debug = "7" }
    emissive = @{ size = @(1456, 939);  preset = "hero"; debug = "8" }
}

function Read-Ready([string]$logPath) {
    if (-not (Test-Path $logPath)) { return @() }
    $text = Get-Content -LiteralPath $logPath -ErrorAction SilentlyContinue
    return @($text | Where-Object { $_ -match "HALO_STAGE_READY" })
}

$managed = @("HALO_MOCK_MIDI", "HALO_STAGE_LOG", "HALO_STAGE_PRESET", "HALO_STAGE_DEBUG", "HALO_STAGE_POSE",
             "HALO_STAGE_LOOK", "HALO_STAGE_STATS", "HALO_MODE", "HALO_RAIL", "HALO_STAGE_CAMERA")

foreach ($look in $Looks) {
    foreach ($shot in $Shots) {
        $def = $defs[$shot]
        if ($null -eq $def) { $def = @{ size = @(1456, 939); preset = $shot } }
        $name = "$shot-$look" + $(if ($Suffix -ne "") { "-$Suffix" } else { "" })
        $logPath = Join-Path $outDir "$name.log"
        Remove-Item -LiteralPath $logPath -ErrorAction SilentlyContinue

        foreach ($k in $managed) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
        foreach ($k in $extra.Keys) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
        $env:HALO_MOCK_MIDI = "1"
        $env:HALO_STAGE_LOG = $logPath
        $env:HALO_STAGE_PRESET = $def.preset
        $env:HALO_STAGE_LOOK = $look
        $env:HALO_STAGE_POSE = $(if ($def.pose) { $def.pose } else { $Pose })
        if ($def.debug) { $env:HALO_STAGE_DEBUG = $def.debug }
        if ($Stats) { $env:HALO_STAGE_STATS = "1" }
        if ($def.env) { foreach ($k in $def.env.Keys) { Set-Item "Env:$k" $def.env[$k] } }
        foreach ($k in $extra.Keys) { Set-Item "Env:$k" $extra[$k] }

        $p = Start-Process -FilePath $Exe -WorkingDirectory $repo -PassThru
        try {
            $deadline = (Get-Date).AddSeconds($TimeoutSec)
            while ($p.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100; $p.Refresh() }
            if ($p.MainWindowHandle -eq 0) { throw "no window for $name" }
            [void][HaloShots]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, 16, 0, $def.size[0], $def.size[1], 0x0014) # NOZORDER|NOACTIVATE

            # Settled once a READY line exists and no newer one arrives for 0.8 s.
            $count = 0
            while ((Get-Date) -lt $deadline) {
                $ready = @(Read-Ready $logPath)
                if ($ready.Count -gt 0 -and $ready.Count -eq $count) { break }
                $count = $ready.Count
                Start-Sleep -Milliseconds 800
            }
            $ready = @(Read-Ready $logPath)
            if ($ready.Count -eq 0) { throw "HALO_STAGE_READY never appeared for $name (see $logPath)" }
            if ($HoldSeconds -gt 0) { Start-Sleep -Seconds $HoldSeconds }

            $png = Join-Path $outDir "$name.png"
            & powershell -NoProfile -File (Join-Path $PSScriptRoot "screenshot.ps1") -ProcessId $p.Id -Out $png | Out-Null

            $r = New-Object HaloShots+RECT
            [void][HaloShots]::GetWindowRect($p.MainWindowHandle, [ref]$r)
            $last = $ready[$ready.Count - 1]
            if ($last -match "stage=(-?\d+),(-?\d+),(\d+),(\d+)") {
                $stage = @{ x = [int]$Matches[1] - $r.Left; y = [int]$Matches[2] - $r.Top; w = [int]$Matches[3]; h = [int]$Matches[4] }
            } else { $stage = $null }
            @{ shot = $shot; look = $look; window = @{ x = $r.Left; y = $r.Top; w = $r.Right - $r.Left; h = $r.Bottom - $r.Top };
               stage = $stage; pose = $env:HALO_STAGE_POSE; preset = $def.preset; debug = $def.debug } |
                ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $outDir "$name.json")
            "saved $png"
        }
        finally {
            if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
            foreach ($k in $managed) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
            foreach ($k in $extra.Keys) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
        }
    }
}
