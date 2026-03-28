# Windows Portable Release

Deckboy's current Windows shipping format is a portable app folder, not a
single standalone `.exe`.

## Why

- The Windows build needs sidecar runtime DLLs from `build/windows/Release`.
- Deckboy expects a nearby `data/` directory for themes, fonts, demos, and UI
  assets.
- Media ingest / thumbnails / waveform analysis still require `ffmpeg.exe` and
  `ffprobe.exe`.

## Packaging Shape

- portable folder: `dist/windows/Deckboy/`
- zip artifact: `dist/windows/Deckboy-windows-portable.zip`

Contents include:

- `deckboy-native.exe`
- required SDL / font / compression DLLs from `build/windows/Release`
- repo `data/` directory
- bundled media tools at `tools/ffmpeg/bin/`
- `Launch Deckboy.cmd`
- `README-WINDOWS-PORTABLE.txt`

## Build / Package

1. Build the Windows release target.
2. Run:

```powershell
& 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' -ExecutionPolicy Bypass -File 'C:\Users\user\deckboy\scripts\package_windows_portable.ps1'
```

3. Ship the generated folder or zip.

## Current Recommendation

- For now, keep Windows in the same Deckboy repo as the other platforms.
- Treat Windows as the same product with platform-specific packaging, not as a
  separate GitHub project.
- Add an installer later, after the portable package has been tested on a clean
  Windows machine and `.deckboy` file association behavior is ready.
