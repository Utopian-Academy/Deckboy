"""Package the verified example shows, guide, preview and original soundtrack."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PACK = ROOT / "data/demos/starter-pack"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--extract-to", type=Path,
                        help="Also create a ready-to-open folder beside the archive")
    args = parser.parse_args()
    checks = ROOT / "build/examples/check"
    report = json.loads((checks / "report.json").read_text(encoding="utf8"))
    manifest = json.loads((PACK / "manifest.json").read_text(encoding="utf8"))
    assert {s["file"] for s in report["shows"]} == {s["file"] for s in manifest["shows"]}
    for show in report["shows"]:
        assert sha(PACK / show["file"]) == show["sha256"], "Recheck changed show: " + show["file"]
    assert sha(PACK / "media/soft-circuit.wav") == report["audio_sha256"], "Recheck changed audio"
    shutil.copyfile(ROOT / "LICENSE", PACK / "LICENSE.txt")
    shutil.copyfile(checks / "overview.png", PACK / "overview.png")
    files = ["00-START-HERE.html", "README.md", "LICENSE.txt", "overview.png",
             "manifest.json", "media/soft-circuit.wav"]
    files += [s["file"] for s in manifest["shows"]]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(files):
            archive.write(PACK / name, "Deckboy-Example-Shows/" + name)
    with zipfile.ZipFile(args.output) as archive:
        assert archive.testzip() is None
        assert len(archive.namelist()) == len(files)
        for name in files:
            assert archive.read("Deckboy-Example-Shows/" + name) == (PACK / name).read_bytes()
    if args.extract_to:
        # Preserve a user's existing edits: a new folder is required.
        args.extract_to.mkdir(parents=True, exist_ok=False)
        for name in files:
            target = args.extract_to / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(PACK / name, target)
        print("Extracted folder:", args.extract_to.resolve())
    print(json.dumps({"zip": str(args.output.resolve()), "bytes": args.output.stat().st_size,
                      "files": len(files), "sha256": sha(args.output)}, indent=2))


if __name__ == "__main__":
    main()
