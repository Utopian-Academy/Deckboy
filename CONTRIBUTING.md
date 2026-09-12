# Contributing to Deckboy

Thank you for your interest in Deckboy! Every contribution makes the tool
better for the operators, engineers and technicians who depend on it during
live shows.

## Ways to contribute

| Contribution | Where to start |
|---|---|
| **Bug reports** | [Open a bug report](https://github.com/Utopian-Academy/Deckboy/issues/new?template=bug_report.md) — include your OS, hardware, version, and steps to reproduce. Crash logs live next to the executable. |
| **Feature requests** | [Open a feature request](https://github.com/Utopian-Academy/Deckboy/issues/new?template=feature_request.md) — describe the workflow problem, not just the button you want. |
| **Code** | Fork, branch, hack, PR. See [Building](#building) below. |
| **Documentation** | Fix a typo, clarify a paragraph, add a missing section to the [manual](MANUAL.md). |
| **Testing** | Run a pre-release on real hardware and report what happens — DeckLink, NDI, unusual codecs, edge-case resolutions. |
| **Production feedback** | Used Deckboy on a real show? Tell us what worked, what didn't, and what you reached for that wasn't there. |

## Building

Deckboy is a native C/C++ application built with **CMake**.

```bash
# Clone
git clone https://github.com/Utopian-Academy/Deckboy.git
cd Deckboy

# Configure and build (Release)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

See [docs/CODEMAP.md](docs/CODEMAP.md) for the file inventory, data flow and
threading model, and [docs/PACKAGING.md](docs/PACKAGING.md) for creating
installers and portable bundles.

## Pull requests

1. **One logical change per PR.** If you fixed a bug *and* added a feature,
   send two PRs.
2. **Describe what and why**, not just what you changed — the diff shows the
   what; the description should explain the reasoning.
3. **Test on at least one platform.** If you only have Windows, say so — CI
   covers all three.
4. **Don't break the build.** CI runs on every push; a red build blocks merge.

## Code style

- Follow the style already in the file you are editing.
- No trailing whitespace. Newline at end of file.
- Comments explain *why*, not *what*.

## Reporting security issues

If you find a security vulnerability, please **do not** open a public issue.
See [SECURITY.md](SECURITY.md) for responsible disclosure instructions.

## Code of conduct

Be professional, be respectful. This project serves people who work under
pressure in live environments — bring that same focus and mutual respect to
the issue tracker and pull requests.

## License

By contributing, you agree that your contributions will be licensed under the
[GNU General Public License v3.0 or later](LICENSE), the same license as the
project.
