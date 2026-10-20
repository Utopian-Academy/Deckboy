# Deckboy example shows

Four small shows to open, run and make your own in **Deckboy 0.99.417 or later**.
The pictures are made live by Deckboy. The music is included in `media/`.

![Programme output from the four example shows](overview.png)

## Start here

1. Extract the entire folder somewhere you can edit, such as Documents.
2. In Deckboy, choose **OPEN** and select `01-start-here.deckboy`.
3. Use **SAVE AS** (`Ctrl+Shift+S`) to make your own copy in the same folder.
   Deckboy saves edits automatically.
4. Select the first cue and press **TAKE** (`Enter`). Open **DASH** (`Ctrl+D`)
   and press **Output on** to open its programme window.
5. Advance with **GO** (`Space`), or select any cue and press **TAKE**.

Keep the `media` folder beside the shows when moving or sharing them. To save a
show elsewhere together with its media, use **BUNDLE**.

## Pick a show

| Show | What to try |
|---|---|
| **01 — Start here** | Five cues: titles, a live Swirl, Video Synth, kaleidoscope and a closing card. Change one setting, then take another cue to see how each keeps its own look. |
| **02 — Layers and titles** | Three playlists form one picture: backdrop, inset and a transparent name strap. Open DASH and try its five named scene buttons. Switch the backdrop while the other layers stay on air. |
| **03 — Effects playground** | Six versions of the same source: reference, kaleidoscope, RGB split plus scanlines, an animated ripple, text mode and geometry motion. Compare the effects and their modulation controls. |
| **04 — One-button show** | A 39-second sequence combines picture, words, an inset and original music. Open DASH, turn output on, then press **Run sequence**. |

Every cue includes a short note in the inspector. The two layered shows also
include named master-tracker rows: choose **Open tracker** in DASH to see which
cue each scene fires on each playlist.

## Explore the layers

In **Layers and titles**, try the scene buttons in order:

1. **All three layers** — live backdrop, inset and Bob Cobb / Can Man.
2. **Change the backdrop** — swap its palette while the layout stays in place.
3. **Change inset and strap** — a kaleidoscope inset and arcade name strap.
4. **Fireside interview** — a new background with the name strap.
5. **Clean backdrop** — the background alone.

The **Clear layer** cues are transparent. Taking one removes that playlist's
picture; taking its first cue brings it back. Inspect the inset's geometry to
change its size and position, or edit the name strap's title and subtitle.

## Run Soft Circuit

The automated show has six scenes:

| Time | Scene |
|---|---|
| 0–8 seconds | Doors: holding look, title and music |
| 8–16 seconds | Opening: new picture and title together |
| 16–24 seconds | Main scene: mirrored synth, lower third and inset |
| 24–28 seconds | Clear the inset |
| 28–36 seconds | Closing title and backdrop |
| 36–39 seconds | Picture fades to black; music fades out |

**Hold sequence** pauses the tracker's progression; the current sources keep
playing. **Run sequence** resumes from the current row. To run the whole show
again, press **Doors**, then **Run sequence**.

Try changing a scene's length in the tracker, replacing a title, or substituting
your own audio. The included music lasts 39 seconds, so adjust the soundtrack
too when changing the show's overall length.

## Make it yours

- Duplicate a cue before changing it to keep a reference.
- Change a Swirl palette, then compare the result with a different effect.
- Bypass the ripple's amount LFO to compare a moving effect with a fixed one.
- In text mode, change the number of columns and the glyph set.
- Use the same scene structure for a talk: holding slide, introduction, speaker,
  closing slide.

The examples and original synthesized music are provided under GPL-3.0, like
Deckboy. See `LICENSE.txt`. The music uses no third-party samples.
