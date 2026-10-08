"""Every file in a Mac bundle must run on the macOS the bundle claims.

WHY THIS EXISTS. v0.99.405's Info.plist claimed macOS 11. Deckboy itself was
built for 13, and its 32 bundled libraries -- Homebrew's, built for the build
machine -- for 14 (15 for some on Intel). On Ventura the bundled ffmpeg and
ffprobe could not start: libavdevice hard-linked camera features that only
exist from macOS 14. The desk opened with no icons and could import nothing,
and no check anywhere had looked.

This reads each Mach-O's own minimum (LC_BUILD_VERSION / LC_VERSION_MIN_MACOSX)
under Contents/MacOS and Contents/Frameworks and FAILS if any is newer than
LSMinimumSystemVersion. Built for the claimed minimum, anything newer is linked
weakly, so a file's minimum is the honest test of whether it loads. It also
lists every strong import of a symbol from a short watch list of APIs newer
than the claim, as evidence.

Pure Python, no Xcode: it runs on the Mac runner and on any machine.

    python tools/audit_macos_minos.py path/to/Deckboy.app
"""

import os
import plistlib
import struct
import sys

LC_SYMTAB, LC_VERSION_MIN_MACOSX, LC_BUILD_VERSION = 0x2, 0x24, 0x32
N_WEAK_REF = 0x40

# Symbols that took a bundle down before, with the macOS that introduced them.
# Evidence, not the gate: the gate is each file's minimum.
WATCH = {
    "_AVCaptureDeviceTypeContinuityCamera": (14, 0),
    "_AVCaptureDeviceTypeExternal": (14, 0),
    "_AVCaptureDeviceTypeMicrophone": (14, 0),
    "_AVCaptureDeviceTypeDeskViewCamera": (13, 0),
}


def parse_version(text):
    parts = [int(p) for p in str(text).split(".") if p.isdigit()]
    return tuple((parts + [0, 0])[:2])


def inspect(path):
    data = open(path, "rb").read()
    if len(data) < 32:
        return None
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic == 0xbebafeca:
        return {"fat": True}
    if magic != 0xfeedfacf:
        return None
    ncmds = struct.unpack_from("<I", data, 16)[0]
    off, minos, strong = 32, None, []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        if cmd == LC_BUILD_VERSION:
            v = struct.unpack_from("<I", data, off + 12)[0]
            minos = (v >> 16, (v >> 8) & 0xff)
        elif cmd == LC_VERSION_MIN_MACOSX:
            v = struct.unpack_from("<I", data, off + 8)[0]
            minos = (v >> 16, (v >> 8) & 0xff)
        elif cmd == LC_SYMTAB:
            symoff, nsyms, stroff, _ = struct.unpack_from("<IIII", data, off + 8)
            for i in range(nsyms):
                strx, ntype, _, ndesc, _ = struct.unpack_from("<IBBHQ", data, symoff + i * 16)
                if ntype & 0x0e or not ntype & 0x01:
                    continue
                end = data.index(b"\0", stroff + strx)
                name = data[stroff + strx:end].decode(errors="replace")
                if name in WATCH and not ndesc & N_WEAK_REF:
                    strong.append(name)
        off += size
    return {"minos": minos, "strong": strong}


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    app = sys.argv[1]
    info = plistlib.load(open(os.path.join(app, "Contents", "Info.plist"), "rb"))
    claim = parse_version(info.get("LSMinimumSystemVersion", "0"))
    print("audit: every file runs on the macOS the bundle claims (%d.%d)" % claim)
    files, problems = 0, []
    for sub in ("MacOS", "Frameworks"):
        d = os.path.join(app, "Contents", sub)
        for name in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            p = os.path.join(d, name)
            if not os.path.isfile(p) or os.path.islink(p):
                continue
            r = inspect(p)
            if not r:
                continue
            files += 1
            if r.get("fat"):
                problems.append("%s/%s: universal binary -- check each slice" % (sub, name))
                continue
            if r["minos"] is None:
                problems.append("%s/%s: no minimum macOS recorded" % (sub, name))
            elif r["minos"] > claim:
                problems.append("%s/%s: built for macOS %d.%d, newer than the claimed %d.%d"
                                % ((sub, name) + r["minos"] + claim))
            for sym in r["strong"]:
                if WATCH[sym] > claim:
                    problems.append("%s/%s: hard-links %s (macOS %d.%d)" % ((sub, name, sym) + WATCH[sym]))
    print("  %d files checked" % files)
    for p in problems:
        print("  FAIL " + p)
    print("clean" if not problems else "%d problems" % len(problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
