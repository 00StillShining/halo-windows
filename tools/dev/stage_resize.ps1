# Drags the window edge of a MOCK Halo instance for a few seconds (as a user
# resizing it would) and reports how often the stage reallocated its render
# targets (plan M2: no more than once after the size settles).
#
# Usage: powershell -File tools\dev\stage_resize.ps1 [-Seconds 5] [-Exe <Halo.exe>]
param([int]$Seconds = 5, [string]$Exe = "")

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ($Exe -eq "") { $Exe = Join-Path $repo "build\Stage\Halo_artefacts\Debug\Halo.exe" }
$log = Join-Path $repo "build\shots\resize.log"
Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class HaloResize {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int w, int h, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[void][HaloResize]::SetProcessDPIAware()

$env:HALO_MOCK_MIDI = "1"
$env:HALO_STAGE_LOG = $log
$env:HALO_STAGE_STATS = "1"
$p = Start-Process -FilePath $Exe -WorkingDirectory $repo -PassThru
try {
    $deadline = (Get-Date).AddSeconds(30)
    while ($p.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100; $p.Refresh() }
    [void][HaloResize]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, 16, 0, 1300, 860, 0x0014)
    while (-not ((Test-Path $log) -and (Select-String -Path $log -Pattern "HALO_STAGE_READY" -Quiet)) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
    $before = @(Select-String -Path $log -Pattern "targets: ").Count

    # Drag: grow and shrink the right edge every frame or so.
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
        $t = $sw.Elapsed.TotalSeconds
        $w = 1300 + [int](300 * [Math]::Sin($t * 2.1))
        $h = 860 + [int](120 * [Math]::Sin($t * 1.3))
        [void][HaloResize]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, 16, 0, $w, $h, 0x0014)
        Start-Sleep -Milliseconds 16
    }
    Start-Sleep -Milliseconds 1500
    $lines = @(Select-String -Path $log -Pattern "targets: ")
    "allocations before the drag: $before; during and after: $($lines.Count - $before)"
    $lines | Select-Object -Skip $before | ForEach-Object { "  " + $_.Line }
}
finally {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    Remove-Item Env:HALO_STAGE_LOG, Env:HALO_STAGE_STATS -ErrorAction SilentlyContinue
}
