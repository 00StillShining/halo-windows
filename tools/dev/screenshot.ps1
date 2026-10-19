# Captures only the running Halo window to a PNG via PrintWindow with
# PW_RENDERFULLCONTENT, so the OpenGL stage is included and other windows
# on top of it are not.
# Usage: powershell -File tools\dev\screenshot.ps1 -Out build\shot.png
param([string]$Out = "build\halo-shot.png", [string]$Process = "Halo", [int]$ProcessId = 0)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class HaloWin {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[void][HaloWin]::SetProcessDPIAware()

$proc = if ($ProcessId -ne 0) { Get-Process -Id $ProcessId -ErrorAction Stop }
        else { Get-Process -Name $Process -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1 }
$r = New-Object HaloWin+RECT
[void][HaloWin]::GetWindowRect($proc.MainWindowHandle, [ref]$r)
$w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[void][HaloWin]::PrintWindow($proc.MainWindowHandle, $hdc, 2) # PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc)
$bmp.Save((Resolve-Path -LiteralPath (Split-Path $Out -Parent)).Path + "\" + (Split-Path $Out -Leaf), [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved $Out ($w x $h)"
