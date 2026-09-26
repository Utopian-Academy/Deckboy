# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
# This file is part of Deckboy, a cue deck for live events.
# See LICENSE for details.
#
# build_libltc_windows.ps1 -- build libltc as ltc.dll and put it beside the exe.
#
# WHY THIS SCRIPT EXISTS. Every Windows release up to and including
# v0.99.379 shipped with NO libltc at all, so LTC generation, LTC chase and
# timecode cues were dead in the zip while working perfectly on the machine
# that built them. `--self-check` in the released zip says
# "ltc-runtime: missing (LoadLibrary failed (error 126))".
#
# The cause is that vcpkg has no libltc port -- 2761 ports, not one of them --
# so the Windows CI job installed sdl3, sdl3-ttf, rtmidi, spout2 and webview2
# and there was simply nothing that could have produced ltc.dll. macOS and
# Linux CI both `brew install libltc` / `apt install libltc-dev`, so only
# Windows was affected and only Windows had no way to notice.
#
# Whoever built the local development tree had an ltc.dll in it -- put there
# by hand, months ago -- which is why this never showed up in testing. That is
# the "CI green isn't shipped" rule exactly: ask the ARTIFACT, not the tree.
#
# WINDOWS_EXPORT_ALL_SYMBOLS IS THE WHOLE TRICK. libltc's headers carry no
# __declspec(dllexport) -- it is an autotools project that has never targeted
# MSVC -- so a plain shared build produces a DLL that exports nothing and
# LoadLibrary succeeds while every GetProcAddress fails. CMake can generate
# the .def file for a C library instead, and that is what makes this work.
#
# Usage:
#   pwsh tools/build_libltc_windows.ps1 -OutDir build/windows/Release
[CmdletBinding()]
param(
    # Where ltc.dll should end up -- normally next to Deckboy.exe.
    [Parameter(Mandatory = $true)][string]$OutDir,
    # Pinned. An optional dependency that silently changes version between
    # releases is the other half of the problem this script is fixing.
    [string]$Version = "1.3.2",
    [string]$WorkDir = ""
)

$ErrorActionPreference = "Stop"

if (-not $WorkDir) { $WorkDir = Join-Path $env:TEMP "deckboy-libltc" }
$null = New-Item -ItemType Directory -Force -Path $WorkDir
$null = New-Item -ItemType Directory -Force -Path $OutDir

$Tarball = Join-Path $WorkDir "libltc-$Version.tar.gz"
$SrcDir  = Join-Path $WorkDir "libltc-$Version"

if (-not (Test-Path $SrcDir)) {
    $Url = "https://github.com/x42/libltc/releases/download/v$Version/libltc-$Version.tar.gz"
    Write-Host "Fetching $Url"
    # The progress bar makes Invoke-WebRequest an order of magnitude slower on
    # Windows PowerShell and writes to the host from a background runspace,
    # which is noise in a CI log.
    $OldProgress = $ProgressPreference
    $ProgressPreference = "SilentlyContinue"
    try {
        Invoke-WebRequest -Uri $Url -OutFile $Tarball -UseBasicParsing
    } finally {
        $ProgressPreference = $OldProgress
    }
    if (-not (Test-Path $Tarball) -or (Get-Item $Tarball).Length -lt 100000) {
        throw "libltc: the download is missing or far too small: $Tarball"
    }
    # bsdtar ships with Windows 10+ and handles .tar.gz. Its OUTPUT and its
    # EXIT CODE are both checked: a silent failure here produced "did not
    # unpack" with nothing to say why, which is the least useful possible
    # message for the one step that talks to the network.
    # THE SYSTEM tar, BY FULL PATH. Bare `tar` resolves against PATH, and on a
    # developer machine that is usually Git for Windows' GNU tar, which needs
    # a separate gzip binary it cannot find and fails with
    # "tar (child): ... Cannot exec". Windows' own bsdtar decompresses in
    # process. CI happens to get the right one; a human running this by hand
    # from a bash prompt does not, which is exactly the sort of difference
    # that makes a script "work on CI" and nowhere else.
    $SystemTar = Join-Path $env:SystemRoot "System32\tar.exe"
    $TarExe = if (Test-Path $SystemTar) { $SystemTar } else { "tar" }
    $TarOut = & $TarExe -xzf $Tarball -C $WorkDir 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "libltc: tar failed (exit $LASTEXITCODE): $TarOut"
    }
    if (-not (Test-Path $SrcDir)) {
        throw ("libltc $Version unpacked without producing $SrcDir. " +
               "tar said: $TarOut")
    }
}

# A CMakeLists of our own. libltc is autotools and there is nothing to
# configure here: four C files and a header.
$Lists = @'
cmake_minimum_required(VERSION 3.16)
project(ltc C)
add_library(ltc SHARED src/ltc.c src/decoder.c src/encoder.c src/timecode.c)
target_include_directories(ltc PRIVATE src)
# libltc's headers carry no __declspec(dllexport), so a plain Windows shared
# build exports nothing and every GetProcAddress fails after a LoadLibrary
# that succeeded. CMake generates the .def for a C library instead.
set_target_properties(ltc PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
if(MSVC)
  target_compile_definitions(ltc PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
'@
Set-Content -Path (Join-Path $SrcDir "CMakeLists.txt") -Value $Lists -Encoding UTF8

$BuildDir = Join-Path $SrcDir "build-msvc"
cmake -S $SrcDir -B $BuildDir -G "Visual Studio 17 2022" -A x64 | Out-Host
if ($LASTEXITCODE -ne 0) { throw "libltc: cmake configure failed" }
cmake --build $BuildDir --config Release | Out-Host
if ($LASTEXITCODE -ne 0) { throw "libltc: build failed" }

$Dll = Join-Path $BuildDir "Release\ltc.dll"
if (-not (Test-Path $Dll)) { throw "libltc: no ltc.dll at $Dll" }

# EXPORTS OR IT DID NOT WORK. A DLL that exports nothing is the exact failure
# this script's WINDOWS_EXPORT_ALL_SYMBOLS line exists to prevent, and it
# looks identical to success from here -- the file is the right size and
# LoadLibrary is happy. The import library only exists when symbols were
# exported, so its absence is the cheap, reliable tell.
$Lib = Join-Path $BuildDir "Release\ltc.lib"
if (-not (Test-Path $Lib)) {
    throw "libltc: built a DLL that exports nothing (no ltc.lib). " +
          "WINDOWS_EXPORT_ALL_SYMBOLS did not take effect."
}

Copy-Item $Dll -Destination $OutDir -Force
Write-Host ("libltc $Version -> " + (Join-Path $OutDir "ltc.dll"))
