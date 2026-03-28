# Deckboy Version Flow

Deckboy now uses a single source-of-truth version flow across the repo, the app,
and GitHub Actions.

## Source Of Truth

- The current Deckboy version lives in [`VERSION`](../VERSION).
- Format: `MAJOR.MINOR.PATCH`
- Optional prerelease suffixes are allowed: `0.76.0-alpha.1`, `0.76.0-rc.1`

Current example:

```text
0.75.1
```

## What Uses It

- `CMakeLists.txt` reads `VERSION` during configure.
- CMake generates `deckboy_version.hpp` so the native app can report the same value.
- `Deckboy --version` prints the version tag Deckboy was built from.
- `Deckboy --self-check` includes the current version near the top.

## GitHub Flow

Branch CI:

- Pull requests into `deckboy-0.75` build Deckboy on Linux, macOS, and Windows.
- Pushes to `deckboy-0.75` do the same.

Tag CI:

- Pushing a tag like `v0.75.1` triggers the same cross-platform build flow.
- GitHub Actions first verifies that:
  - `VERSION` says `0.75.1`
  - the pushed tag says `v0.75.1`
- If those do not match, the workflow fails immediately.

## Recommended Release Steps

1. Update [`VERSION`](../VERSION).
2. Update the human notes:
   - [`CHANGES.md`](../CHANGES.md)
   - [`README.md`](../README.md)
   - any release-specific docs
3. Commit and push the changes.
4. Create an annotated Git tag:

```bash
git tag -a v0.75.1 -m "Release v0.75.1"
git push origin v0.75.1
```

5. Let GitHub Actions validate the tag and build Linux/macOS/Windows artifacts.

## Current Scope

This is the first pass of the version flow.

Implemented now:

- SemVer source-of-truth file
- native app version reporting
- tag-to-version validation in GitHub Actions
- cross-platform build flow tied to tags

Still future work:

- fully automated GitHub Releases with attached platform packages
- richer per-platform packaging (`.zip`, `.tar.gz`, installer/dmg/AppImage`, etc.)
- embedding commit SHA/build channel metadata alongside the semantic version
