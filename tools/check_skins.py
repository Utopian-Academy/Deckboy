"""Shoot the desk in each skin, at each UI scale, on a private desktop.

  py -3 tools/check_skins.py OUT_DIR [--themes a,b,c] [--scales 1,1.5,2]
                             [--exe build/windows/Release/Deckboy.exe]

The desk window is 2400x1350 (example_show_runtime). Writes OUT_DIR/<theme>@<scale>.bmp and a contact sheet per scale
(OUT_DIR/sheet@<scale>.png, needs Pillow). A skin is only finished when it has
been LOOKED AT on these, at every scale: a number has been wrong before.
Without --themes it shoots every theme whose theme.txt has a style_ line.
"""
import argparse
from pathlib import Path
import shutil
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from example_show_runtime import Runtime  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def skinned_themes():
    out = []
    for theme in sorted((ROOT / "data" / "themes").iterdir()):
        txt = theme / "theme.txt"
        if txt.exists() and any(line.startswith("style_") for line in txt.read_text(encoding="utf-8", errors="replace").splitlines()):
            out.append(theme.name)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--themes", default="")
    ap.add_argument("--scales", default="1")
    ap.add_argument("--exe", default=str(ROOT / "build" / "windows" / "Release" / "Deckboy.exe"))
    ap.add_argument("--show", default=str(ROOT / "data" / "demos" / "starter-pack" / "02-running-a-show.deckboy"))
    args = ap.parse_args()
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    themes = [t for t in args.themes.split(",") if t] or skinned_themes()
    scales = [s for s in args.scales.split(",") if s]
    shots = {}
    # The runtime copies data/ into the work folder and keeps what is there:
    # start clean, or a theme edited since the last run is shot as it was.
    shutil.rmtree(out / "work", ignore_errors=True)
    # On a copy: SET marks the show changed and the desk saves it.
    (out / "work").mkdir(parents=True)
    show = out / "work" / Path(args.show).name
    shutil.copy2(args.show, show)
    media = Path(args.show).parent / "media"
    if media.is_dir():
        shutil.copytree(media, out / "work" / "media")
    with Runtime(args.exe, out / "work", show) as rt:
        for scale in scales:
            rt.send(f"SET ui_scale {scale}")
            for theme in themes:
                rt.send(f"SET theme {theme}")
                # The bottom bar keeps its colours until it is laid out again:
                # give it a few frames, or each shot shows the last theme's buttons.
                time.sleep(0.8)
                path = rt.snapshot(out / f"{theme}@{scale}.bmp")
                shots.setdefault(scale, []).append((theme, path))
                print("shot", theme, scale, flush=True)
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    for scale, items in shots.items():
        thumbs = []
        for theme, path in items:
            im = Image.open(path).convert("RGB")
            im.thumbnail((800, 600))
            thumbs.append((theme, im))
        tw = max(t.width for _, t in thumbs)
        th = max(t.height for _, t in thumbs) + 28
        cols = 3
        rows = (len(thumbs) + cols - 1) // cols
        sheet = Image.new("RGB", (tw * cols, th * rows), "white")
        draw = ImageDraw.Draw(sheet)
        for i, (theme, im) in enumerate(thumbs):
            x, y = (i % cols) * tw, (i // cols) * th
            sheet.paste(im, (x, y))
            draw.text((x + 6, y + im.height + 6), theme, fill="black")
        sheet.save(out / f"sheet@{scale}.png")


if __name__ == "__main__":
    main()
