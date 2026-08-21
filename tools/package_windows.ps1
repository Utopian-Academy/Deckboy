# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
#
# package_windows.ps1 — Build a self-contained Windows zip release.
#
# What this script bundles:
#   - Deckboy.exe and every .dll already co-located by CMake in build/Release
#     (SDL3, SDL3_ttf, freetype, libpng16, brotli*, bz2, zlib, SpoutLibrary,
#     WebView2Loader, ltc, and the libav* DLLs for in-process decode:
#     avcodec/avformat/avutil/avfilter/swscale/swresample)
#   - FFmpeg license/copyright notice for the bundled libav* DLLs
#   - ffmpeg.exe + ffprobe.exe from C:\ffmpeg\bin (override with -FfmpegDir)
#   - MSVC C++ runtime DLLs (app-local, so the target machine doesn't need
#     the Visual C++ Redistributable installed)
#   - data/ — fonts, themes, UI pack, default project
#   - LICENSE, README.txt
#
# What this script does NOT bundle (licensing / driver reasons):
#   - NDI runtime (operator installs NDI Tools — free, signed by Vizrt)
#   - Blackmagic Desktop Video (operator installs from blackmagicdesign.com)
#   - WebView2 Runtime (operator installs from Microsoft, preinstalled on W11)
#
# Deckboy already prompts the operator the first time they try to use a
# feature whose runtime isn't installed, so no setup-time install is needed.
#
# Usage (from repo root):
#   .\tools\package_windows.ps1
#   .\tools\package_windows.ps1 -FfmpegDir "D:\ffmpeg\bin" -Configuration Release
#
# Output: dist\Deckboy-<VERSION>-windows-x64.zip

[CmdletBinding()]
param(
    [string]$RepoRoot      = (Resolve-Path (Join-Path $PSScriptRoot "..")),
    [string]$Configuration = "Release",
    [string]$BuildDir      = "",
    [string]$FfmpegDir     = "C:\ffmpeg\bin",
    [string]$OutputDir     = ""
)

$ErrorActionPreference = "Stop"

if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot ("build\windows\" + $Configuration)
}
if (-not $OutputDir) {
    $OutputDir = Join-Path $RepoRoot "dist"
}

# --- Sanity checks ----------------------------------------------------------
$Version = (Get-Content (Join-Path $RepoRoot "VERSION") -Raw).Trim()
if (-not $Version) {
    throw "VERSION file is empty"
}
$Exe = Join-Path $BuildDir "Deckboy.exe"
if (-not (Test-Path $Exe)) {
    throw "Deckboy.exe not found at $Exe. Build first: cmake --build build\windows --config $Configuration"
}
if (-not (Test-Path (Join-Path $FfmpegDir "ffmpeg.exe"))) {
    throw "ffmpeg.exe not found in $FfmpegDir. Pass -FfmpegDir <path> if installed elsewhere."
}
if (-not (Test-Path (Join-Path $FfmpegDir "ffprobe.exe"))) {
    throw "ffprobe.exe not found in $FfmpegDir."
}

# --- Stage layout -----------------------------------------------------------
$StageName = "Deckboy-$Version-windows-x64"
$StageDir  = Join-Path $OutputDir "staging\$StageName"
$ZipPath   = Join-Path $OutputDir "$StageName.zip"

if (Test-Path $StageDir) {
    Remove-Item $StageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

Write-Host "Staging Deckboy $Version -> $StageDir"

# --- Copy app binaries (exe + every .dll CMake placed next to it) -----------
Copy-Item $Exe -Destination $StageDir
Get-ChildItem $BuildDir -Filter *.dll | ForEach-Object {
    Copy-Item $_.FullName -Destination $StageDir
}
# Bundled companion apps (terrarium easter egg)
$TerrariumExe = Join-Path $BuildDir "terrarium.exe"
if (Test-Path $TerrariumExe) {
    Copy-Item $TerrariumExe -Destination $StageDir
}

# --- Copy ffmpeg / ffprobe --------------------------------------------------
Copy-Item (Join-Path $FfmpegDir "ffmpeg.exe")  -Destination $StageDir
Copy-Item (Join-Path $FfmpegDir "ffprobe.exe") -Destination $StageDir

# --- Copy MSVC C++ runtime DLLs (app-local) ---------------------------------
# Microsoft permits app-local deployment of the MSVC runtime DLLs, which is
# what lets the target machine run Deckboy without installing the Visual C++
# Redistributable. We pick the highest-numbered redist directory installed
# alongside the VS 2022 toolchain so a freshly updated VS still works.
$RedistRoot = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC"
if (Test-Path $RedistRoot) {
    $CrtDir = Get-ChildItem $RedistRoot -Directory |
              Where-Object { Test-Path (Join-Path $_.FullName "x64\Microsoft.VC143.CRT") } |
              Sort-Object Name -Descending |
              Select-Object -First 1
    if ($CrtDir) {
        $CrtPath = Join-Path $CrtDir.FullName "x64\Microsoft.VC143.CRT"
        Get-ChildItem $CrtPath -Filter *.dll | ForEach-Object {
            Copy-Item $_.FullName -Destination $StageDir
        }
        Write-Host "  + MSVC runtime: $($CrtDir.Name)"
    } else {
        Write-Warning "VS 2022 redist root exists but no VC143.CRT/x64 dir found under $RedistRoot. The zip may require the Visual C++ Redistributable on the target machine."
    }
} else {
    Write-Warning "VS 2022 redist directory not found at $RedistRoot. The zip may require the Visual C++ Redistributable on the target machine."
}

# --- Copy data + license ----------------------------------------------------
$DataSrc = Join-Path $RepoRoot "data"
if (Test-Path $DataSrc) {
    Copy-Item $DataSrc -Destination $StageDir -Recurse
    # data/last_project.txt is per-machine state, not shipping content: it holds
    # the absolute path of the last show opened on the BUILD machine. Releases
    # up to v0.80.1 carried the packager's own local path into every download,
    # which leaks a local directory layout and leaves a fresh install offering
    # "open previous show" for a file the user has never had.
    #
    # data/default.deckboy is the same class of mistake and was still shipping:
    # it is gitignored scratch state (the auto-loaded startup show), so whatever
    # the packager last had open goes out in the zip. On this build machine that
    # was a CAMERA cue naming a specific webcam model — releases were carrying
    # the packager's hardware inventory, and a fresh install's first TAKE would
    # open a webcam. Both files are per-machine state; neither ships.
    foreach ($StaleName in @("last_project.txt", "default.deckboy")) {
        $StaleState = Join-Path (Join-Path $StageDir "data") $StaleName
        if (Test-Path $StaleState) {
            Remove-Item $StaleState -Force
            Write-Host "  - stripped data\$StaleName (build-machine state)"
        }
    }

    # data/_converted/ holds the operator OWN transcoded media -- datamosh
    # preps, proxies, format conversions. It is gitignored, but .gitignore does
    # not apply to a file copy, so without this the zip ships whatever personal
    # footage is on the packaging machine. One real prep here was 1.7 GB, which
    # is how this was noticed.
    $ConvertedDir = Join-Path (Join-Path $StageDir "data") "_converted"
    if (Test-Path $ConvertedDir) {
        $ConvertedMB = [math]::Round(((Get-ChildItem $ConvertedDir -Recurse -File | Measure-Object Length -Sum).Sum) / 1MB)
        Remove-Item $ConvertedDir -Recurse -Force
        Write-Host "  - stripped data\_converted (operator media, $ConvertedMB MB)"
    }
}
$LicenseSrc = Join-Path $RepoRoot "LICENSE"
if (Test-Path $LicenseSrc) {
    Copy-Item $LicenseSrc -Destination $StageDir
}
# FFmpeg license notice for the bundled libav* DLLs (in-process decode).
# vcpkg installs the port's consolidated copyright file; carrying it in the
# zip satisfies the LGPL/GPL notice requirement for redistribution.
# Derived, not hardcoded to one machine: VCPKG_ROOT if set, else the usual
# locations. This was a literal path under one developer home directory, which
# both put a username in the repo and silently skipped the licence notice on
# every other machine.
$VcpkgRoot = $env:VCPKG_ROOT
if (-not $VcpkgRoot) {
    foreach ($candidate in @("$env:USERPROFILE\vcpkg", "C:\vcpkg", "C:\dev\vcpkg")) {
        if (Test-Path (Join-Path $candidate "vcpkg.exe")) { $VcpkgRoot = $candidate; break }
    }
}
$FfmpegCopyright = if ($VcpkgRoot) {
    Join-Path $VcpkgRoot "installed\x64-windows\share\ffmpeg\copyright"
} else { "" }
if ($FfmpegCopyright -and (Test-Path $FfmpegCopyright)) {
    Copy-Item $FfmpegCopyright -Destination (Join-Path $StageDir "LICENSE-ffmpeg.txt")
} else {
    Write-Warning "FFmpeg copyright file not found at $FfmpegCopyright; zip ships libav DLLs without their license notice."
}

# --- README ------------------------------------------------------------------
$ReadmeBody = @"
Deckboy $Version (Windows x64)
==============================

Unzip anywhere, double-click Deckboy.exe. No install required.

The MSVC C++ runtime DLLs are bundled, so the target machine does NOT need
the Visual C++ Redistributable.

Optional features that need separate vendor installs:
  * NDI input/output    -> NDI Tools  (free, signed by Vizrt)
                           https://ndi.video/tools/
  * DeckLink output     -> Blackmagic Desktop Video
                           https://www.blackmagicdesign.com/support/family/capture-and-playback
  * Browser cues        -> Microsoft WebView2 Runtime
                           https://developer.microsoft.com/en-us/microsoft-edge/webview2/

Deckboy will pop a prompt the first time you try to enable any of these,
with a button to the official vendor download page. You do not need to
install them up front.

Run-as: Deckboy.exe resolves its data root from the executable's folder,
so leaving the zip contents intact (Deckboy.exe and data\ side by side) is
all that's needed.

Reporting issues / source code:
  https://github.com/Utopian-Academy/Deckboy
"@
$ReadmeBody | Set-Content -Path (Join-Path $StageDir "README.txt") -Encoding UTF8

# --- Zip --------------------------------------------------------------------
if (Test-Path $ZipPath) {
    Remove-Item $ZipPath -Force
}
Write-Host "Compressing -> $ZipPath"
# Passing $StageDir (not $StageDir\*) keeps the top-level folder name inside
# the archive, so unzipping always produces a Deckboy-<VERSION>-windows-x64
# directory rather than spilling files into the operator's current folder.
Compress-Archive -Path $StageDir -DestinationPath $ZipPath -CompressionLevel Optimal

$SizeMb = "{0:N1}" -f ((Get-Item $ZipPath).Length / 1MB)
Write-Host ""
Write-Host "Packaged: $ZipPath  ($SizeMb MB)"
Write-Host "Staging dir kept at: $StageDir"
