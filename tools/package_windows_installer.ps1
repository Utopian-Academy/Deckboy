# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
#
# package_windows_installer.ps1 - build the Windows installer end to end:
#   1. stage the portable payload (package_windows.ps1)
#   2. optionally code-sign Deckboy.exe  (DORMANT unless configured)
#   3. compile the Inno Setup installer  (tools\deckboy.iss)
#   4. optionally code-sign the setup.exe (DORMANT unless configured)
#
# CODE SIGNING IS OPTIONAL AND OFF BY DEFAULT. An unsigned installer works
# fine - the first run shows a one-click SmartScreen "More info -> Run anyway".
# That is a deliberate, zero-cost default. Since June 2023 a publicly trusted
# code-signing key must live on hardware or a cloud HSM, so there is no single
# "point at a .pfx" story any more. Rather than bake in one vendor, this reads
# the WHOLE signtool command from an env var:
#
#   $env:DECKBOY_WIN_SIGNTOOL_ARGS = 'sign /fd SHA256 /tr http://timestamp... /td SHA256 <backend-specific flags>'
#
# That one string works for Azure Trusted Signing (the ~$10/mo cloud option),
# a USB-token cert, or a test .pfx (/f cert.pfx /p pass) - whatever the operator
# has. If it is empty, both signing steps are skipped and nothing costs anything.

param(
  [string]$RepoRoot     = (Resolve-Path (Join-Path $PSScriptRoot "..")),
  [string]$Configuration = "Release",
  [string]$FfmpegDir    = "C:\ffmpeg\bin",
  [string]$Iscc         = "",
  # Where the built Deckboy.exe is. Forwarded to package_windows.ps1, which
  # otherwise assumes build\windows\<config> -- true for a local build and not
  # for CI, which builds to build\<config>. Without this the installer could
  # only ever be built on a developer's own machine.
  [string]$BuildDir     = ""
)
$ErrorActionPreference = "Stop"

$Version = (Get-Content (Join-Path $RepoRoot "VERSION") -Raw).Trim()
Write-Host "Building Windows installer for Deckboy $Version"

# Locate ISCC (Inno Setup compiler) if not given.
if (-not $Iscc) {
  $candidates = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe"
  )
  $Iscc = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Iscc -or -not (Test-Path $Iscc)) {
  throw "Inno Setup (ISCC.exe) not found. Install it (winget install JRSoftware.InnoSetup) or pass -Iscc."
}

# signtool, only needed if signing is configured.
$SignArgs = $env:DECKBOY_WIN_SIGNTOOL_ARGS
function Invoke-Sign([string]$file) {
  if (-not $SignArgs) { return }   # dormant: nothing configured, skip silently
  $signtool = Get-Command signtool.exe -ErrorAction SilentlyContinue
  if (-not $signtool) {
    Write-Warning "DECKBOY_WIN_SIGNTOOL_ARGS is set but signtool.exe is not on PATH - skipping signing of $file"
    return
  }
  Write-Host "  signing $([System.IO.Path]::GetFileName($file))"
  # Split the configured args and append the target file.
  $argList = $SignArgs.Trim() -split '\s+'
  & $signtool.Source @argList $file
  if ($LASTEXITCODE -ne 0) { throw "signtool failed on $file" }
}

# 1. Stage the portable payload (this is what the .iss packages).
$StageArgs = @{
  RepoRoot      = $RepoRoot
  Configuration = $Configuration
  FfmpegDir     = $FfmpegDir
}
if ($BuildDir) { $StageArgs.BuildDir = $BuildDir }
& (Join-Path $PSScriptRoot "package_windows.ps1") @StageArgs

# 2. Sign the app binary BEFORE it is packaged, so the INSTALLED Deckboy.exe is
#    signed too, not only the installer.
$StagedExe = Join-Path $RepoRoot ("dist\staging\Deckboy-$Version-windows-x64\Deckboy.exe")
Invoke-Sign $StagedExe

# 3. Compile the installer.
& $Iscc "/DDeckboyVersion=$Version" (Join-Path $PSScriptRoot "deckboy.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compile failed" }
$Setup = Join-Path $RepoRoot ("dist\Deckboy-$Version-windows-x64-setup.exe")

# 4. Sign the installer itself.
Invoke-Sign $Setup

Write-Host ""
Write-Host "Installer: $Setup"
if ($SignArgs) { Write-Host "  (code-signed)" } else { Write-Host "  (unsigned - set DECKBOY_WIN_SIGNTOOL_ARGS to sign)" }
