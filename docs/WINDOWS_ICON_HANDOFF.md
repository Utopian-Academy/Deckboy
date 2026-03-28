# Windows Icon Handoff

## Assets

- App icon family:
  - `art/windows/icons/deckboy_app.ico`
  - `art/windows/icons/deckboy_app_16.png`
  - `art/windows/icons/deckboy_app_20.png`
  - `art/windows/icons/deckboy_app_24.png`
  - `art/windows/icons/deckboy_app_32.png`
  - `art/windows/icons/deckboy_app_48.png`
  - `art/windows/icons/deckboy_app_64.png`
  - `art/windows/icons/deckboy_app_128.png`
  - `art/windows/icons/deckboy_app_256.png`
- Project-file icon family:
  - `art/windows/icons/deckboy_project.ico`
  - `art/windows/icons/deckboy_project_16.png`
  - `art/windows/icons/deckboy_project_20.png`
  - `art/windows/icons/deckboy_project_24.png`
  - `art/windows/icons/deckboy_project_32.png`
  - `art/windows/icons/deckboy_project_48.png`
  - `art/windows/icons/deckboy_project_64.png`
  - `art/windows/icons/deckboy_project_128.png`
  - `art/windows/icons/deckboy_project_256.png`
- Approved source reference:
  - `art/windows/icons/approved_reference_2026-03-27.png`

## Current Source Of Truth

- The currently shipped Windows icon family is the clean final PNG / `.ico`
  pack imported on 2026-03-27.
- Treat the files already in `art/windows/icons/` as the source of truth for
  this release pass.
- Do not regenerate the shipped icon family from the older presentation sheet
  unless a new approved workflow is added.

## Ownership

- `deckboy_app.*` is the Windows app / taskbar / Alt+Tab / Start-menu / `.exe`
  icon family.
- `deckboy_project.*` is the deferred `.deckboy` file-association icon family.

## Build Integration

- The Windows app icon is embedded through:
  - `native/platform/windows/deckboy.rc.in`
  - `CMakeLists.txt`
- CMake configures a build-local `deckboy.rc` file and adds it to the
  `deckboy-native` target only when `WIN32` is true.
- The exported `.ico` source path is currently:
  - `art/windows/icons/deckboy_app.ico`

## Deferred File Association

- `.deckboy` registration is not wired in this pass.
- Recommended defaults for a future Windows association / installer pass:
  - extension: `.deckboy`
  - ProgID: `Deckboy.Project`
  - friendly name: `Deckboy Project`
  - default icon: `art/windows/icons/deckboy_project.ico`
