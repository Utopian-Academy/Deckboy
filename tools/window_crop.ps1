# Print the ffmpeg crop for a window's CLIENT area, in physical screen pixels.
#
#   powershell -File tools/window_crop.ps1 -ProcessName Deckboy
#   -> 1760:990:36:74
#
# WHY THIS EXISTS
#
# A demo GIF was published to the README and the project site with slivers of
# the operator's desktop around the edge of every frame -- wallpaper, icons,
# whatever else was on screen. Nobody noticed until it was live.
#
# The capture had cropped the DESKTOP to where the window had been PUT:
#
#     SetWindowPos(hwnd, 30, 30, 1760, 1020)
#     ffmpeg ... -vf "crop=1760:1020:30:30"
#
# which looks obviously correct and is wrong twice over:
#
#   - SetWindowPos sizes the OUTER window. The title bar and the borders are
#     inside that rectangle and the application draws in none of it.
#   - On a scaled display (150% here) a pixel the window manager was given is
#     not a pixel a desktop capture measures. The two coordinate spaces differ
#     by the scale factor.
#
# Between them the crop was adrift on every side, and the desktop showed
# through the gap.
#
# So: ask the window instead of computing. GetClientRect gives the drawable
# size, ClientToScreen puts its origin in screen space, and SetProcessDPIAware
# first makes both physical pixels -- the same units a desktop capture uses.
# Nothing is derived from a screenshot and there is no arithmetic to get wrong.
#
# USE IT LIKE THIS
#
#   $crop = powershell -File tools/window_crop.ps1 -ProcessName Deckboy
#   ffmpeg -f lavfi -i ddagrab=output_idx=0:framerate=30 `
#          -vf "hwdownload,format=bgra,crop=$crop" -t 20 out.mp4
#
# ddagrab rather than gdigrab: Deckboy renders through D3D11, and GDI cannot
# read GPU-composited content -- gdigrab returns pure black, convincingly, at a
# plausible file size.
#
# AND THEN LOOK AT IT. This tool removes one cause of one leak. It does not
# tell you what was on the screen behind a dialog, or in a playlist, or in a
# file name. Check the frames before anything is published.

param(
    [string]$ProcessName = "Deckboy",
    [int]$Index = 0
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class DeckboyWindowCrop {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@

[DeckboyWindowCrop]::SetProcessDPIAware() | Out-Null

$candidates = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
                Where-Object { $_.MainWindowHandle -ne 0 })
if ($candidates.Count -eq 0) {
    Write-Error "No '$ProcessName' process with a window. Is it running?"
    exit 1
}
if ($Index -ge $candidates.Count) {
    Write-Error "Asked for window $Index but only $($candidates.Count) found."
    exit 1
}

$handle = $candidates[$Index].MainWindowHandle

$rect = New-Object DeckboyWindowCrop+RECT
if (-not [DeckboyWindowCrop]::GetClientRect($handle, [ref]$rect)) {
    Write-Error "GetClientRect failed."
    exit 1
}

$origin = New-Object DeckboyWindowCrop+POINT
$origin.X = 0
$origin.Y = 0
if (-not [DeckboyWindowCrop]::ClientToScreen($handle, [ref]$origin)) {
    Write-Error "ClientToScreen failed."
    exit 1
}

$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top

# yuv420p needs even dimensions; trim rather than pad, so the crop can only
# ever be INSIDE the client area. A crop that rounds outward is the bug.
if ($width % 2) { $width -= 1 }
if ($height % 2) { $height -= 1 }

if ($width -le 0 -or $height -le 0) {
    Write-Error "Client area is $width x $height -- is the window minimised?"
    exit 1
}

Write-Output ("{0}:{1}:{2}:{3}" -f $width, $height, $origin.X, $origin.Y)
