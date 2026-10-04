# AGENTS.md — rules for every AI agent working on Deckboy

Codex, Claude or anything else: this file applies to you. Read it all before
the first edit.

1. **Read `CLAUDE.md` first.** It is the architecture map and the house rules
   for this codebase, and it is written for every agent, not one vendor.
2. **If `private-notes/` exists, read `private-notes/AGENT_RULES.md` next.**
   It holds the owner's standing rules on releases, version numbers, public
   text and decisions. Those rules are binding. Where anything conflicts, the
   owner's logged decisions win.

## Engineering rules (the short form; CLAUDE.md has the reasons)

- **Shared working tree.** Other agents may have uncommitted work here. Stage
  files by name, never `git add -A`, never `git stash`, never
  `git pull --rebase --autostash`. Read `git diff --cached --stat` before every
  commit. Before every push, `git log origin/main..HEAD` must show only your
  commits.
- **Every change works on Windows, macOS and Linux.** Check the `#ifdef`
  structure, and build the disabled configurations (`-DENABLE_ASIO=OFF`,
  `-DDECKBOY_INPROC_DECODE=OFF`) before calling something done.
- **No raw pixel sizes in the UI.** Every size, offset and gap goes through
  `uiScaled()` or is derived from a rect that already is. 150% is the Windows
  default on 4K, so a 1x-only check is the minority case.
- **No colour literals in the UI.** Use the theme: `pal.*`,
  `paletteInkOnFill`, `paletteToggleFill`/`paletteToggleInk`,
  `paletteNotice` for toasts and warnings. Toasts take a `ToastKind`, never a
  colour.
- **Look at what you changed, in a dark theme and a saturated one** (Virtual
  Boy, Ganon), at 1.5x. Run `Deckboy --contrast-check <dir>` and open the
  screenshots it writes. A number alone has been wrong before.
- **Run the audits** before claiming a control works: `tools/audit_actions.py`
  (dead or duplicate action ids), `tools/audit_scaled_layout.py`,
  `tools/audit_encoding.py`. Action ids: grep `= <id>;` before allocating.
- **Claim only what you measured.** "All themes pass" means you ran every
  theme on the whole desk, not one control. A 30-second test does not prove
  a drift fix; measure for as long as the fault took to appear.
- **`docs/manual.html` is generated** from `MANUAL.md` by
  `tools/build_manual_page.py`. Edit the manual, run the generator, commit both.
- **Vendored code** lives in `native/extras/upstream/`, byte-identical to its
  source, with provenance and hashes in `UPSTREAM.md`. Verify a third-party
  file against its official release before committing it.
- **Check whether it already exists** before building it. It usually does:
  unwired, or working but hard to find.
- **Design the control with the feature.** A feature nobody can reach, or a
  control that changes nothing, is the most common waste in this project.
