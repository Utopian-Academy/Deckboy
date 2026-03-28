param(
  [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
  [string]$BuildDir = "",
  [string]$OutputDir = "",
  [string]$ZipPath = "",
  [string]$FfmpegBin = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
  $BuildDir = Join-Path $RepoRoot "build\windows\Release"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
  $OutputDir = Join-Path $RepoRoot "dist\windows\Deckboy"
}
if ([string]::IsNullOrWhiteSpace($ZipPath)) {
  $ZipPath = Join-Path $RepoRoot "dist\windows\Deckboy-windows-portable.zip"
}

function Require-Path([string]$Path, [string]$Label) {
  if (-not (Test-Path -LiteralPath $Path)) {
    throw "$Label not found: $Path"
  }
}

function Ensure-Directory([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path)) {
    New-Item -ItemType Directory -Path $Path | Out-Null
  }
}

function Reset-Directory([string]$Path) {
  if (Test-Path -LiteralPath $Path) {
    Remove-Item -LiteralPath $Path -Recurse -Force
  }
  New-Item -ItemType Directory -Path $Path | Out-Null
}

function Resolve-FfmpegBin([string]$RepoRoot, [string]$RequestedPath) {
  $candidates = @()
  if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
    $candidates += $RequestedPath
  }
  if (-not [string]::IsNullOrWhiteSpace($env:DECKBOY_FFMPEG_DIR)) {
    $candidates += $env:DECKBOY_FFMPEG_DIR
  }
  $candidates += (Join-Path $RepoRoot "tools\ffmpeg\bin")
  $candidates += "C:\ffmpeg\bin"

  foreach ($candidate in $candidates) {
    if ([string]::IsNullOrWhiteSpace($candidate)) {
      continue
    }
    $ffmpeg = Join-Path $candidate "ffmpeg.exe"
    $ffprobe = Join-Path $candidate "ffprobe.exe"
    if ((Test-Path -LiteralPath $ffmpeg) -and (Test-Path -LiteralPath $ffprobe)) {
      return $candidate
    }
  }

  throw "Unable to find ffmpeg.exe and ffprobe.exe. Set -FfmpegBin or DECKBOY_FFMPEG_DIR, or install them to C:\ffmpeg\bin."
}

$repoDataDir = Join-Path $RepoRoot "data"
$launchCmdPath = Join-Path $OutputDir "Launch Deckboy.cmd"
$portableReadmePath = Join-Path $OutputDir "README-WINDOWS-PORTABLE.txt"
$toolsBinDir = Join-Path $OutputDir "tools\ffmpeg\bin"
$resolvedFfmpegBin = Resolve-FfmpegBin -RepoRoot $RepoRoot -RequestedPath $FfmpegBin

Require-Path -Path $BuildDir -Label "Windows Release build directory"
Require-Path -Path (Join-Path $BuildDir "deckboy-native.exe") -Label "Deckboy executable"
Require-Path -Path $repoDataDir -Label "Repo data directory"

Ensure-Directory (Split-Path -Parent $OutputDir)
Ensure-Directory (Split-Path -Parent $ZipPath)
Reset-Directory $OutputDir

Copy-Item -Path (Join-Path $BuildDir "*") -Destination $OutputDir -Recurse -Force
$outputDataDir = Join-Path $OutputDir "data"
if (Test-Path -LiteralPath $outputDataDir) {
  Remove-Item -LiteralPath $outputDataDir -Recurse -Force
}
Ensure-Directory $outputDataDir
Copy-Item -Path (Join-Path $repoDataDir "*") -Destination $outputDataDir -Recurse -Force

Ensure-Directory $toolsBinDir
Copy-Item -LiteralPath (Join-Path $resolvedFfmpegBin "ffmpeg.exe") -Destination (Join-Path $toolsBinDir "ffmpeg.exe") -Force
Copy-Item -LiteralPath (Join-Path $resolvedFfmpegBin "ffprobe.exe") -Destination (Join-Path $toolsBinDir "ffprobe.exe") -Force

Set-Content -LiteralPath $launchCmdPath -Encoding ASCII -Value @'
@echo off
setlocal
set "DECKBOY_ROOT=%~dp0"
pushd "%~dp0"
start "" "%~dp0deckboy-native.exe"
'@

Set-Content -LiteralPath $portableReadmePath -Encoding ASCII -Value @'
Deckboy Windows Portable Package
================================

Run:
  - double-click "Launch Deckboy.cmd"
  - or run "deckboy-native.exe" from this folder

Notes:
  - Keep this folder structure intact.
  - Deckboy expects the bundled data/ directory and ffmpeg tools beside it.
  - ffmpeg.exe and ffprobe.exe are bundled in tools\ffmpeg\bin.
  - No installer or file association is included in this package yet.
'@

if (Test-Path -LiteralPath $ZipPath) {
  Remove-Item -LiteralPath $ZipPath -Force
}
Compress-Archive -Path (Join-Path $OutputDir "*") -DestinationPath $ZipPath -CompressionLevel Optimal

$summary = [pscustomobject]@{
  OutputDir = $OutputDir
  ZipPath = $ZipPath
  FfmpegBin = $resolvedFfmpegBin
}
$summary | Format-List
