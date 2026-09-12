# CHANGES - Incremental Updates (March-September 2026)

## 2026-09-12 - v0.99.346 (a presenter view, and notes that come with the deck)

**Presenter view.** A second screen showing the operator what the audience
cannot see: the slide that is up, the one that is next, the notes for the one
that is up, and the time. It is an **output type**, so it goes on its own output
with its own display — programme to the projector, presenter to the laptop —
and it inherits the display picker, fullscreen, arming and health reporting that
every output already has.

It is meant to be more use than the one in a slide deck: show or hide the
previous slide, the next slide, the notes, the clock and the timers
independently; three layouts; and your own background, ink and accent colours,
because a presenter screen is often somebody else's laptop in somebody else's
room.

**Note builds.** A cue's notes split on a line of `---`, and the presenter
advances through the parts *without changing the slide* — so a long note is read
at the speaker's pace instead of arriving all at once. Optionally the ordinary
NEXT action spends the remaining builds before it advances the cue, which is how
a slide clicker behaves in every other deck a presenter has used.

**Notes that came with the deck.** A PDF imported beside a `.pdfpc` file — the
open convention the LaTeX and Beamer world already uses — picks up its per-slide
notes automatically, and `.notes.txt` / `.notes.md` work the same way. `### 1`
starts slide one's notes; `---` inside them splits the builds.

**Corrections to the platform notes.** Spout *output* works on Windows and was
being described as a scaffold; Spout *input* and Syphon genuinely are not built.
And macOS and Linux decode on the CPU because the in-process decoder's hardware
path is D3D11VA, not because those platforms cannot — VideoToolbox and VAAPI are
now on the roadmap where they belong.

## 2026-09-12 - v0.99.345 (the monitor and the preview follow the scale)

The program monitor's labels — PROGRAM, the LIVE badge, the output resolution
line and the two VJ deck captions — were fixed pixel boxes under type that
scales, and so were the preview thumbnail's captions. At 150% the empty
inspector's three lines ("No cue selected" / "Drop media here" / "Press A to
take cue") were spaced 20 pixels apart while each was taller than that, so they
overlapped. The colour-tag row and the cue id row keep the labels they were
missing.

## 2026-09-12 - v0.99.344 (rows that know what they are)

**The inspector's last unlabelled controls have labels.** Notes, cue id and goto
target were a value box and a button with no question anywhere — you had to know
what they were. Loop and hold were half-width pills each carrying their own
question ("loop: off"), so the eye read the whole pill to learn the subject and
read it again for the answer. End action was a full-width pill reading
"end: inherit  [X cycle]", with the keyboard hint spliced into the value. All of
them are now the same label-left, control-right row as everything else, and the
five hand-written copies of the editable-row shape go through the one helper.

**The timeline header fits its own text.** The header grew with the UI scale while
the cue name and the clock under it stayed at fixed offsets, so at 150% they ran
together. Both rows are now sized from the faces that draw them.

## 2026-09-12 - v0.99.343 (a clock, a readable meter, and a licence GitHub can read)

**An optional wall clock.** Off by default — the toolbar belongs to the show —
and cycled from one control in Settings → System → Appearance: off, 24-hour,
12-hour, or a round analogue face with a second hand. It sits at the right-hand
end of the toolbar and stays put, because everything else on that end comes and
goes with the state of the show and a clock you have to find again is not doing
its job.

**The VU meter reads.** Every measurement in it was a fixed pixel while the
numbers in it scaled, so at 150% "-12" and "-48" ran out of their column and were
cut off rather than fitted. And the L and R sat at a fixed inset instead of being
centred on the bars they name. The column is now as wide as the widest reading it
can show, and each channel letter is centred on its own bar.

**GitHub can read the licence.** The LICENSE file held the nineteen-line *notice*
you put at the top of a source file, not the licence itself — so GitHub's
detector matched nothing and the project showed no licence at all. It is now the
full GPL-3.0 text, with a licence section in the README naming the copyright.

**Housekeeping on the public repository.** Six unreferenced splash images (about
15 MB, including one still carrying the filename its generator gave it) are gone,
and a Reddit keyword-scanning script that was never part of the product has moved
out of the public tree.

**The landing page said two things that were not so.** It advertised conversion
"using NVENC" — one of three GPU encoders Deckboy tries, and absent on every AMD,
Intel and Apple machine — and claimed "No installer", while the README offers a
Windows installer with a Start Menu entry and an uninstaller.

## 2026-09-12 - v0.99.342 (pages that finish their own sentences)

**The keyboard shortcuts page reads.** Its modal, its rows and its key column
were all fixed pixel sizes while the type scaled with the desktop, so at 150% the
rows nearly touched and half the descriptions were cut off — "Skip to next /
pr...", "Blackout - insta...", "Desk, then clea...". A page of instructions that
cannot finish its own sentences is worse than no page. It is now sized from the
font, and the key column is as wide as the widest key measured in the face
actually in use.

**The cue summary panel fits its own contents.** The block under SELECTED CUE was
laid out at fixed offsets from the top of the panel while the heading above it
scaled, so at 150% the cue's name was drawn where the heading had grown to and
the two collided — and the panel's own height was a constant, so the last two
lines fell outside it. Both the size and the layout now come from the same three
measured row heights.

**A Stream Deck can put the shortcuts page on screen.** `SHORTCUTS SHOW|HIDE|
TOGGLE`, alongside the dashboard's. It was reachable only from the keyboard,
which is the one input an operator driving Deckboy from a control surface does
not have their hands on.

## 2026-09-12 - v0.99.341 (a menu you cannot see through)

**The SOURCE menu is opaque and follows the theme.** It filled with a hardcoded
dark green at 96% opacity, so it ignored the colourway entirely — a dark green
box on a light theme — and whatever sat behind it read straight through: with the
menu open over the timeline you could read "The timeline can be scrubbed" through
the list of source types. It now uses the same chrome roles as every other panel,
at full opacity, and its rows and padding scale with the interface.

## 2026-09-12 - v0.99.340 (the cue list scales with the desktop)

**Cue rows follow the UI scale.** The row was a fixed 80 pixels tall while the
fonts grew with the desktop, so at 150% three lines of larger text were being
asked to fit in a row sized for smaller ones: the kind label came out as "PA..."
for Pattern, the name and the metadata ran together, and the per-cue action icons
sat on top of the cue's own name. Every offset in the row now scales, and the
kind label runs to the edge of its column instead of a fixed width — "Window
Source" and "Lower Third" were always longer than the space budgeted for them.

**Nothing behind a modal talks over it.** Hover tips were suppressed for the
modals that existed when that check was written, so the dashboard and the
shortcuts page — both added later — had the timeline underneath them still
offering "Click to seek", painted over the top by the tip layer.

**The dashboard's own hint fits.** "Ctrl+D or Esc to close" was in a fixed-width
box and came out as "Ctrl+D or Esc..." — an instruction ellipsized into one you
cannot follow.

## 2026-09-12 - v0.99.339 (one question per row, everywhere)

**Every settings card and every inspector row now has the same shape**: the
question on the left, the control that answers it on the right, one per row. It
reads as a sentence, and it puts every control on a page at the same place, which
is most of what "consistent" means when you open a panel you have not looked at
in a month.

Before this the same page mixed four shapes — full-width pills whose own label
carried the question ("PAUSE BEGIN OFF", "HYPERDECK ON", "tag: none [K cycle]"),
bare buttons whose meaning came from position, controls on the *left* with a
sentence of explanation to their right, and grids of chips that wrapped
differently depending on the platform and the window width.

**Switched-on controls are no longer the hardest to read.** A lit control used to
invert — a dark fill with light ink — which measured worst on the page precisely
where it mattered most. State now shows as the brightness of the fill, and the
ink stays dark on light in both states. That rule now holds for the settings
toggles, the inspector rows, the dropdowns and the cue's colour-tag swatch, whose
ink is picked against the swatch rather than against the theme.

**The stage timer has its own controls.** It was being drawn through the generic
numeric row, so RESET appeared as two buttons that both reset, framing the phrase
"back to start", and the nudges showed their own description where a value goes.
A timer is an instrument you drive while looking at the stage, so it now has a
large clock readout that turns amber and red at this cue's own thresholds, START
and RESET as real buttons, and four direct nudges (−1 min, −10 s, +10 s, +1 min).

## 2026-09-12 - v0.99.338 (held cues come up, and the transitions are the ones you asked for)

**A cue set to HOLD now appears.** Every pattern cue is a hold by default, and a
hold has no timeline — it sits at position zero for as long as it is up. The
fade-in was read against that position, so it evaluated to zero for the whole
time the cue was on, and the cue was composited at nothing: a test pattern taken
to a live output showed black, with the frame present and correct behind it and
nothing anywhere to say why. A held cue's fade now runs on the clock from the
moment it was taken — it fades in once, over the length asked for, and stays.
The same was true of a held cue's audio fade, and is fixed with it.

**A transition reaches every armed output, not just the first.** Each output
keeps its own copy of the outgoing picture, but they shared one note of which
picture had been uploaded — so the second output skipped the upload into its own
texture and drew one that had never been filled. A recording made while a window
output was live was black for the whole transition.

**"push-left" is a push.** Transition names were matched without separators, so
the hyphen anyone would actually type — and that the interface's own labels
suggest — matched nothing and fell through to a dissolve. Separators are now
ignored, so `push-left`, `push_left`, `pushleft` and `Push Left` are one style,
`dissolve` works as well as `crossfade`, and a name that really is not a style is
reported over the wire instead of quietly becoming one.

## 2026-09-12 - v0.99.337 (a long playlist runs at full rate again)

**The frame rate no longer falls away as cues are added.** A show with sixteen
cues had dropped to 23fps, and a real slide deck is sixty-two. The cause was in
how a label that does not fit its box is drawn a little smaller: it resized the
shared font and put it back afterwards, and each of those calls throws away
every glyph that face had already rasterised -- so one label that overflowed
made every other label in the frame render again from its outlines. The cost
grew with the number of cues because the cue list is where the labels are.

A shrunken label now picks a smaller copy of the same face, kept alongside the
original, and nothing anybody else is drawing with is touched. Measurements are
asked once and remembered, and a label that already fits no longer goes to the
ellipsizer to be told so. Sixty-four cues at a 1.5x desktop now hold 60fps,
where sixteen could not.

Two things were fixed along the way. Truncating a label cut it one **byte** at
a time, which could land in the middle of a character in any language that
needs more than one of them -- it now cuts on character boundaries. And it
measured after every cut, thirty times for a long name; it finds the cut by
halving instead, in about six.

## 2026-09-11 - v0.99.336 (the cyphers say what they mean)

**ROT13, Atbash and Morse now read the way they should.** The four cyphers
transform the English rather than translating it, and each label was being put
through that transform twice on its way to the screen. Two of them are their own
inverse, so the desk came up in plain English while the setting said otherwise;
Morse came up as a row of slashes. The fourteen translated languages were never
affected -- a translated word is not itself an English word, so the second pass
did nothing to them.

## 2026-09-11 - v0.99.335 (slide decks in their own process, tally from the switcher, nineteen languages)

**Importing a slide deck no longer borrows the show's graphics.** On Windows,
Deckboy renders PDF pages with the same engine Edge uses, and that engine wants
a graphics device of its own. It now gets one, in a separate process that does
the render and then goes away — so a slide import touches nothing the show is
using, and a deck that the renderer cannot make sense of costs an import rather
than an evening. This is the shape the Linux side has always had, and macOS
keeps rendering through CoreGraphics exactly as before.

The import itself is unchanged from the operator's side: drop a PDF on the
deck, watch the page counter climb, and get one still cue per page at full 4K
width. The counter is fed by the renderer as each page lands, so a sixty-page
deck reports real progress the whole way through instead of arriving all at
once at the end.

**Going to air is the cue.** On a switched show the operator's hands are on the
switcher, not on the playout machine -- so Deckboy can now watch the switcher
and roll when it is put to air. Two ways, both off until asked for:

Deckboy connects to an ATEM directly and watches its program bus. Set the
switcher's address and pick which input Deckboy is -- from a dropdown of the
switcher's own source names, read off the ATEM itself, so it says what the
panel in the room says. It is a read-only connection: Deckboy never sends the
switcher a command, because a playout machine that can cut the show is one that
will eventually cut the show by accident.

Or, with no switcher at all: an NDI receiver reports back to whatever it is
watching, so a Deckboy output that a receiver has put on program knows it is
live and rolls on that alone.

Either way, coming back off air does what you chose -- load the next cue,
pause, stop, clear, or nothing.

**Deckboy answers as a deck.** HyperDeck emulation now speaks on the port the
protocol is actually spoken on, so ATEM Software Control, Companion's HyperDeck
module and anything else that talks to a deck can find it. The settings page
shows the address to type in, worked out towards the switcher rather than
guessed, and the emulation can be switched off for a rig that would rather
Deckboy stayed quiet.

**NMC in and out, where you can reach them.** Deckboy has been able to sync
transport with another machine in both directions for a long time -- receiving
someone else's play, pause, stop and locate, or sending its own -- but the
direction, the port, the target and the source filter were read from
environment variables at launch. That meant a launcher script, and it meant the
settings could not travel with the show. They are now a card on the Network
page and part of the project file, and the page offers the field the chosen
direction actually uses rather than both. A machine already started with the
old environment variables keeps working exactly as it did.

**Deckboy reads in nineteen languages.** English, fourteen translations, and
four cyphers. A translation is a file of english-and-its-equivalent, and a line
nobody has done yet falls through to the English -- so an unfinished language
is a partly translated desk rather than one with gaps in it. The trade's own
vocabulary stays in English where the trade keeps it there, because an operator
who learned this desk in one country has to be able to work it in another.

A cypher is not a translation but a transform of the English, so it needs no
word list and covers every label in the program: ROT13, Atbash, 1337 and Morse.
Among the translations are Cuban Spanish, Klingon written the way Klingon is
actually written, and Lumeni, which was invented for this and has a grammar.

**A matte and a bug belong to the screen.** A house frame is a property of the
output, not of what is playing on it: a 2.39 letterbox or a station logo has to
survive every cut, every clear and every panic. Each output now carries its own
matte -- 16:9 through 2.39:1, with bars that can dim rather than mask -- and its
own still overlay. Both are composited into the output's picture before
anything is taken off it, so the recording, the stream, NDI and the program
monitor show the same frame instead of four near-misses.

**The desk is the size the desktop says.** Windows hands a DPI-aware program
real physical pixels, so on a display scaled to 125% or 150% Deckboy was
drawing at 1:1 -- not blurry, just two thirds the size of everything else on
that screen. UI scale now follows the desktop unless you tell it otherwise, and
the setting says what it is actually doing rather than what is stored. A show
saved before this carries its own scale and is unchanged.

**Crash reports say where.** When something does go wrong, the report Deckboy
leaves behind now names the module and offset for every frame on the stack, and
writes the essentials first — so the file is readable on its own, on any
machine, without needing the one it was written on.

## 2026-09-10 - v0.99.334 (10-bit playback, a live coder that keeps up, motion mosh)

**10-bit video plays on the GPU.** HEVC Main 10 — and VP9 Profile 2, and 10-bit
AV1 — decode to a P010 surface, which the compositor could not read, so every
frame was pulled off the GPU at full resolution, converted on the CPU and
pushed back up. Those surfaces are wrapped directly now, the same way 8-bit
already was. A 4K60 10-bit clip went from 39.6fps to the full 60, and a 720p
10-bit x265 rip that was quietly paying a download per frame now pays nothing.
The extra depth survives too: the old path converted 10-bit down to 8-bit on
its way through and threw the rest away.

**The live coder keeps up.** The code source evaluated its expression on one
core, so a 4K raster meant 8.3 million pixels running a prelude and three
channel programs single file. The frame splits across cores now, as the effect
stack always has. It also stopped computing `r` and `a` for every pixel whether
or not the expression reads them — an expression like `sin(x*12+t)` was paying
for a square root and an arctangent it never looked at. Together: 8.8x at 4K
and 8.3x at 1080p, 1.2fps to 10.4 and 4.8fps to 39.5, with the picture
byte-for-byte identical.

**Motion mosh — datamosh as an effect, live, on any cue.** The datamosh Deckboy
already had is the codec trick: withhold the keyframe at a cut and the decoder
drags the old picture around by the new shot's motion. It gives the real thing
and it costs a transcode, a wait, and a file — a camera or an NDI feed cannot
be moshed at all.

This computes the same look per frame. The vectors the codec would have
supplied are estimated by matching blocks between one picture and the next; the
picture that refuses to be replaced is held by the effect itself; and the
missing keyframe becomes **refresh**, a knob, rather than an accident of the
encode. **hold** is how long the old picture survives and **block size** runs
from the hard square edges people picture when they say datamosh through to
something that flows. It runs on anything a cue can be, takes an LFO on every
parameter, and writes nothing to disk. 4.6ms at 1080p.

**Command is the shortcut key on macOS.** Every shortcut tested for Control
alone, so Cmd+S, Cmd+O, Cmd+Z and Cmd-click did nothing at all on a Mac.
Control still works alongside it, so nothing anyone had in their fingers has
changed.

**Ctrl+, opens preferences**, and closes them again — Cmd+, on macOS. The bare
comma is still the previous-cue transport key.

**A warning you cannot read is not a warning.** A message raised by a control
inside a menu or the settings panel was drawn underneath it. The update card
suffered worst: its download button explains every refusal by toast, and the
card sits inside the settings panel, so from the outside the button did nothing.

**The update says what is blocking it.** "Stop playback and disarm outputs
first" named two conditions and never said which one held — and a paused deck
counts as playing, while disarming acts on the focused output when any armed
output blocks. It names the one in the way now.

**Intel Macs get an Intel build.** The updater asked for the Apple Silicon disk
image on every Mac, though releases have carried both since v0.99.307.

**The recording folder is in System settings**, not only in Video Outputs — the
tab about display routing, and not where anyone looks for where recordings go.

**The live coder calls itself a source**, in the playlist and the inspector, as
it already did in the SOURCE menu.

**The recent-shows list stays on your machine.** It holds one absolute path per
show opened here, and was not among the files the packagers strip, so a release
build would have carried the packager's own show library.

**SCALEMODE is listed in HELP ALL**, so a control surface asking Deckboy what it
can do is told about it.


## 2026-09-09 - v0.99.333 (bug fixes: display locking, capture cards, timer, hover tips, pocket mode)

**The output window locks to the correct display on macOS.** When an output
window was shown and put to fullscreen in the same event-loop tick, macOS had
not committed the window to the target display yet — so fullscreen landed on the
control monitor instead, covering it and trapping keyboard focus there. The
fullscreen step is now deferred by one tick, giving the compositor time to
assign the window to the right screen first.

**Capture cards work on macOS.** A camera or capture cue pinned itself to
1280x720 in nv12 — a mode the built-in camera lists and a capture card does not.
A Blackmagic or an ATEM Mini enumerates at whatever its input signal happens to
be, usually 1920x1080, so the request was rejected outright and the cue opened
to nothing. Only the frame rate is pinned now, to the 15 and 30 the hardware
actually offers, which is what avoids the 29.97 refusal; the resolution and
pixel format are left to the device, and the scale filter takes the picture to
whatever size the cue reads.

**Timer durations above five minutes are now reachable.** The duration +/−
arrows were moving in 30-second steps, so setting a 30-minute timer took
sixty clicks. Steps are now five minutes — the nudge buttons on the same panel
still move one minute at a time for live adjustment during a running talk.

**Hover tips no longer show over modal panels.** Tips from buttons behind the
settings panel or the startup dialog were appearing on top of them.

**Pocket 3 mode persists when opening a new show.** Opening a show while Pocket
3 was active reset the interface scale and touch mode to their defaults rather
than restoring the layout the show was saved with.

## 2026-09-07 - v0.99.332 (each show named once on the startup screen)

**The startup screen shows your most recent show in its own place.** The show
you were last working on has its own OPEN PREVIOUS button at the top of the
splash screen. It was also appearing in the recent list below, taking up one of
its five slots. The list now shows five other shows only — every slot is a
different destination, and the show you already have a button for is never
listed twice.


## 2026-09-07 - v0.99.331 (recent shows on the splash, settings pages that line up)

**The startup screen lists your recent shows.** Up to five of them, newest
first, each with the folder it lives in - so a house show, a rehearsal file and
last night's gig are one click apart instead of a trip through the file picker.
Click a row, or press its number. Deckboy remembers the last eight, and a show
on a drive that is not plugged in right now keeps its place in the list until
it is back.

**The settings pages line up.** Every card and section is sized from the rows it
actually draws: the Audio page reads as one column, the Video Outputs page
scrolls when there is more below, and the Encoder page's format chips are
legible on every theme with the selected one clearly marked. All of it scales
with the UI Scale setting, and the three platforms render the same layout -
each naming the hardware it actually has.

## 2026-09-07 - v0.99.330 (packaging release)

No functional changes. v0.99.329's build is distributed across all platforms
so the in-app updater finds a newer version on each.


## 2026-09-07 - v0.99.329 (a clean exit on Linux)

**Deckboy closes cleanly on Linux.** Quitting returns straight to the desktop
and reports a proper exit, so anything waiting on Deckboy to finish - the
updater above all - gets the handover it expects.

## 2026-09-07 - v0.99.328 (packaging release)

No functional changes. v0.99.327's build is distributed across all platforms
so the installer-appears fix has a newer version to install.


## 2026-09-07 - v0.99.327 (the Windows installer arrives on screen)

**The update's installer opens in front of you**, with its window where you can
read it and click it, so an update is something you watch happen rather than
something you wait on.

**An update always finds Deckboy already closed.** The helper waits for the app
to stand down before it replaces anything, so the new build is written to a
folder nothing is holding open.

## 2026-09-07 - v0.99.326 (packaging release)

No functional changes. v0.99.325's build is distributed across all platforms
so the automatic-install path can be proven end to end.


## 2026-09-07 - v0.99.325 (one-button updates on Windows, macOS and Linux)

**macOS and Linux install the update themselves.** Press the button and the
update is fetched, unpacked over the app and reopened for you - no disk image
to drag, no AppImage to place by hand. All three platforms now do the same four
things: download, install, relaunch, tidy up.

**And they tidy up after themselves.** Once a Windows update has succeeded its
~94MB setup file is removed, so updating regularly costs no disk. An installer
you downloaded yourself is left alone.

## 2026-09-07 - v0.99.324 (INSTALL & RESTART, start to finish)

**The update restarts Deckboy for you.** The installer hands straight back to
the new build when it finishes, with nothing to notice and nothing to tick, so
the button does the whole job its label describes.

## 2026-09-07 - v0.99.323 (packaging release)

No functional changes. v0.99.322's build is distributed across all platforms
so the update restart can be verified end to end.


## 2026-09-07 - v0.99.322 (INSTALL & RESTART brings Deckboy back)

**One button takes you from "there is a new version" to the new version
running.** The updater stands clear of Deckboy, waits for it to close, runs the
installer, and starts the build it just installed.

**And it leaves a record.** A `relaunch.log` beside the download says what the
update did and when, so an unattended machine can be asked afterwards.

## 2026-09-07 - v0.99.321 (VJ mode fits a laptop, and geometry answers for itself)

**VJ mode fits a 1470-wide desk.** Its bar spans the programme and inspector
columns instead of squeezing into one, so VJ MODE, both deck labels, a
crossfader worth dragging, the blend mode, TAP and the tempo all sit there in
full. The playlists take a fifth more room, the inspector sits at the small end
of its range while you are mixing, and the monitor row keeps enough width to
show the A and B previews.

**The hecklers turn up.** With the previews restored, all three faces are back
on a laptop: one over deck A, one over the programme, one over deck B, each
with its own opinion.

**The mascot's tips read clearly** over the darkened programme monitor, in ink
chosen for that backdrop.

**SCALEMODE, on the wire.** `SCALEMODE fit|fill|stretch|unscaled` sets how a cue
maps to the output from Companion, a surface or a script, and a bare
`SCALEMODE` answers with the current one - so a mapping that was a click in the
inspector is now something a show can be told to do.

## 2026-09-07 - v0.99.320 (the mascot speaks up, and the crossfader keeps its width)

**The mascot's tips read clearly.** The face appears over a deliberately
darkened programme monitor, and its line is now drawn in ink meant for that
backdrop.

**VJ mode keeps its crossfader.** In a narrower window the tempo, TAP and blend
controls step aside until the fader has a width worth dragging - and none of
them shortens its label to fit, because half a word reads as broken rather than
as shorthand.

## 2026-09-07 - v0.99.319 (INSTALL & RESTART restarts)

**Installing an update brings Deckboy back.** The button waits for the
installer to finish and starts the new build itself, so an update is one press
from the settings page to the app running again.

On macOS and Linux the same button reads **INSTALL**, because a .dmg or an
AppImage is placed by hand there: a label should not promise what the platform
cannot do.

## 2026-09-06 - v0.99.318 (browser cues on every platform, and a pointer of our own)

**Browser cues are green on Windows, macOS and Linux.** A page renders, takes
clicks, typing and keys, and goes to air like any other cue - including
`BROWSER KEY Enter` on Linux, with the browser entry in the source menu on
macOS. A page that cannot be reached shows a warning card naming the address
rather than going dark, and a cue with no address of its own opens the Video
Jockey page.

**Deckboy has its own pointer.** Hot pink at the tip running to cyan at the
tail, with a dark outline and a light inner edge so it stays findable on a black
desk and in a white dialog alike. It grows with the UI scale and costs nothing
per frame. Settings > Appearance turns it off.

**Labels shrink to fit rather than shorten.** Where a button is narrow for its
word, the type steps down until the whole word fits, everywhere in the app.

**Frame Count, a new test pattern**, with an emoji variant. Test Clock answers
"are these two feeds in sync"; Frame Count answers "did every frame arrive, and
how far behind is the far end" - the whole field steps colour every tick, so a
dropped or repeated frame shows from across a room.

**Window sources speak one language on all three platforms.** `desktop`,
`screen:N`, `window:<id>`, `title:<text>` and `region:X,Y,W,H` mean the same
thing everywhere, the window picker appears on every platform, and a source a
machine cannot capture says so plainly.

**Colour controls apply as you turn them** - brightness, contrast, saturation,
hue and the chroma key reach the output the moment they change, on stills,
patterns and live sources as well as playing video.

**Steadier under the hood.** Recording keeps producing a live picture
throughout, a browser cue taken on an unfocused deck goes live on that deck, and
the control port answers control clients only.

## 2026-09-06 - v0.99.317 (browser cues on macOS, and one window source everywhere)

**Browser cues run on macOS.** A page renders, takes clicks and typing, and goes
to air like any other cue - the same `BROWSER CLICK`, `TYPE`, `KEY`, `SCROLL`
and `URL` verbs as everywhere else. A cue with no address of its own opens the
Video Jockey page, and a page that cannot be reached shows a warning card naming
the address.

**Window sources speak one language on all three platforms.** `desktop`,
`screen:N`, `window:<id>`, `title:<text>` and `region:X,Y,W,H` mean the same
thing on Windows, macOS and Linux, so a show carries between machines. The
window picker appears on every platform when a window source is added, and a
source a machine cannot capture says so plainly.

**Frame Count, a new test pattern.** Test Clock answers "are these two feeds in
sync"; Frame Count answers "did every frame arrive, and how far behind is the
far end". The whole field steps colour on every tick, so a dropped or repeated
frame is visible from across a room, and photographing the source and the output
together makes the two numbers the latency. There is an emoji variant, because
there should be.

**Colour controls apply as you turn them.** Brightness, contrast, saturation,
hue and the chroma key update on the output the moment they change, on stills
and patterns and live sources as well as playing video.

## 2026-09-06 - v0.99.316 (browser cues you can drive, and a desk that explains itself)

**Browser cues take input.** Clicking the programme monitor clicks the page, so
a cookie wall or a consent dialog can be dismissed from the desk, and the wheel
scrolls it. `BROWSER SCROLL`, `TOP`, `BOTTOM`, `CLICK`, `BACK`, `FORWARD`,
`RELOAD` and `URL` do the same from Companion or a network client. Scrollbars
are hidden by default - a cue is a picture an audience sees, and the page moves
by scrolling it rather than by dragging furniture down its edge.

**Text fields are text fields.** Every inline editor in the app has a caret,
arrow keys, Home and End, word jumps with Ctrl, and clipboard paste, copy and
cut. Long values scroll under the caret, so a URL can be pasted in and a typo
fixed in the middle of it.

**Hover a control and it tells you what it does.** Tips across the toolbar, the
bottom bar, the playlist's loop and shuffle buttons and the per-cue toggles,
with a short dwell so they appear when you pause rather than as you sweep past.
The startup mascot reads along and offers the next thing worth knowing about
whatever the pointer is resting on. Settings > Appearance turns them off.

**Dashboard buttons are chosen from a menu.** Thirty-four ready-made actions -
transport, show, audio, decks, VJ, picture, output - each with its own label and
glyph, and a free-text option for anything else.

**Importing thousands of cues stays responsive.** A large import reads its media
a few files at a time behind a progress readout, and a dropped folder is walked
in the background, so the window keeps drawing and answering while it works.

**Normalise the whole playlist at once.** `AUDIONORM ALL` measures every
file-backed cue in the deck and matches them to the same loudness target, and
the drawn waveform grows with the trim so the change is visible. Gain, pan, mono
and normalise apply across both decks when the VJ split has both playlists on
screen.

**SAVE saves.** It writes back over the show that is open; SAVE AS is a separate
button for making a new file.

**The cue list keeps its commands.** Narrowing the playlist column keeps the
per-cue toggles: the still yields first, then the buttons narrow, and the
picture and the commands sit together at the default width. Audio cues draw
their own waveform where the still would be, and other cue kinds show their type
icon, so a mixed list reads as varied rather than gappy.

**Three characters mind the VJ panes.** With nothing loaded, the A, programme
and B panes each get their own commentator.

## 2026-09-06 - v0.99.315 (Pocket and touch mode, legible at any scale)

**The bottom bar and toolbar say what their buttons do at touch scale.**
IMPORT, BLACKOUT, NEW, OPEN, SAVE and BUNDLE keep their full words at every UI
scale, because the buttons are now sized from the same font that draws them.

**Where a row genuinely will not fit, it wraps.** Ten buttons at double size do
not fit across a small screen - the situation Pocket mode is for - so a group
takes the second row it already had and keeps full-width labels, and a button
left alone on a row gets the width it is owed. At 1x nothing moves.

## 2026-09-06 - v0.99.314 (exact at every raster width)

**ST 2110 output is exact at every width**, odd rasters included, so an unusual
picture size converts as cleanly as a standard one.

This closes a read-through of the least-travelled parts of Deckboy, ranked by
how often each had ever been touched: the PTP client, the NMOS JSON and HTTP
parsers, the caption formats, the motion field, the code editor's cursor
arithmetic, the socket helpers, the library loader and the worker pool. All
cleared.

## 2026-09-06 - v0.99.313 (files and links open through the platform itself)

**"Show in explorer" and the dependency links hand the path straight to the
system's own opener**, as an argument rather than as text to be interpreted, on
every platform. They also report honestly: if a machine has no file manager to
call, Deckboy says so instead of assuming it worked.

## 2026-09-06 - v0.99.312 (the overlay bin on the wire, and precise MMC)

**The cue overlay bin is drivable.** `OVERLAY PUSH <n>`, `POP` and `CLEAR` take
cues in and out of the overlay bin from Companion, a surface or a script, and
the no-argument `OVERLAY` still toggles the timecode burn-in. One verb, one
branch, both meanings.

**MMC LOCATE is precise.** A locate is accepted only when it carries a whole
timecode, so a rig's transport moves to the frame it asked for and to nothing
else.

**Three commands answer with what they did.** STILLDUR, LOWERALPHA and
`OVERLAY PUSH` each say which part of the request they could not act on -
missing argument, unreadable number, no cue selected, or a cue the command does
not apply to - rather than a bare OK.

## 2026-09-06 - v0.99.311 (the picture as a surface in space)

**The displacement mesh.** A cue can be drawn as a landscape of itself: the quad
becomes a grid, every vertex is pushed out of the plane by the brightness of the
picture at that point, and the surface turns under a viewpoint. Real geometry
through the same call the perspective warp uses - no new dependency, no shader,
no depth buffer. `MESH ON`, then height, tilt, yaw, spin and grid; off by
default, so a show that never arms it is unchanged.

## 2026-09-06 - v0.99.310 (the dashboard responds, and clips look right)

**Dashboard buttons fire from the page.** Every tile - including "add a button"
- takes a click wherever it sits, the panel takes the click before anything
underneath it, and a click off the panel closes the page.

**Clip thumbnails are the right shape.** A 16:9 frame is drawn as a 16:9 frame,
filling its slot with no letterboxing and no stretch.

## 2026-09-06 - v0.99.309 (you can see your clips)

**The playlist shows each clip's own picture.** A list of filenames is a list you
have to read; a still turns it into one you can scan, which is what it is for
during a show. Every video and still cue draws its own frame at the head of its
row.

It never blocks: a row draws from the cache, and the first row that finds itself
without a picture asks for one. So a thousand-cue playlist runs one decode at a
time and fills in as you look at it, rather than launching a thousand at once.

**Creatures live throughout the shell, not just under the playlist.** A theme's
animals find every gap the layout leaves them - the empty part of the playlist
under the last cue, and the empty part of the inspector under its last open
section - and the cast is dealt round those gaps rather than duplicated into
each, so a theme that asks for two moths still gets two moths. Each group is
bounded by its own gap, which is what keeps them off the controls, and a spider
hangs its thread from the top of its own gap.

## 2026-09-05 - v0.99.308 (a dashboard, six new effects, and every setting reachable)

**Six new effects.** *Slit scan* holds one frame and refreshes a moving band of
it, so a single picture ends up containing many different moments - move in
front of it and you smear across the frame. *Ferrofluid* reads brightness as a
magnetic field and the lit parts stand up in spikes. *Shatter* breaks the
picture into shards that slide and turn on their own centres. *Edge ignite*
finds the outlines and sets them alight, dull red through to white at the
hardest edges. *Relight* treats brightness as height and lights the picture from
the side with a lamp that walks around it. *Depth split* reads brightness as
nearness and lets the two eyes disagree about it.

All six run inside a 60fps frame at 1080p and are covered by the offline sweep,
the animation check and the preview sweep.

**Five more settings, with controls.** The ST 2110 network interface, the PTP
domain, whether a recording is remuxed to an ordinary MP4 when the take stops,
how many ASIO outputs to open, and the MSC device id the rig addresses us by -
each is now set where it belongs. Everything a show stores can be set from the
app.

**The dashboard has a page.** Ctrl+D opens a grid of your own buttons: each one
runs any command the network protocol understands, so anything the app can do
can go on a tile - and it is the SAME slot a Companion button fires with
`DASH <n>`, so the desk and the screen stay one dashboard instead of two that
drift apart.

Modular: the grid flows to the window, and adding or removing a button reflows
the rest. Customisable: a pencil on each tile sets its label, command and glyph
on one line, and a chip cycles its colour - drawn from the theme, so a dashboard
looks like the colourway it is sitting in. Animated: every tile drifts on its
own phase and springs when it is fired, so the page breathes rather than sitting
there.

`DASH SHOW` puts it on screen from a surface.

## 2026-09-04 - v0.99.307 (streaming, stream telemetry, and audio cues you can watch)

**Streaming holds a stream.** SRT, RTMP and UDP each deliver continuously for as
long as the far end is listening - measured after this release, thirty seconds
of SRT arrives as thirty seconds, 900 packets at exactly 30fps, with audio and
video within a frame of each other. Take a cue mid-stream and the connection
carries on; pull the far end away and Deckboy reconnects on its own.

**You can see what a stream is doing.** A live readout of the delivered frame
rate, uptime, frames sent and frames dropped, and a stream whose far end stops
accepting frames says so. One-click destinations for YouTube and Twitch set the
ingest URL, keyframe interval and bitrate together, since they are only correct
together.

**Audio cues can choose what they show.** An audio cue can be a waveform, an
oscilloscope, a Lissajous figure (left against right, so a polarity-flipped
cable is obvious), a third-octave spectrum, big peak/RMS meters, or a plain name
card. Set it in the cue inspector's AUDIO section, or over the network with
`AUDIOVIS <mode>` - so it can live on a Companion button. The programme monitor
and the outputs draw that picture through one and the same function, so what you
check is what the audience sees.

**A cue that should have sound says what it is doing.** Where a device, a file
or a decode has nothing to give, Deckboy names the reason - because silence is
the one fault an operator cannot see: the cue racks, the clock runs, the
waveform is drawn, and nothing on screen looks wrong.

**A trimmed clip looks trimmed.** Green and red end caps and a
"TRIM 00:30.0 - 01:30.0" readout across the bar, with the ruler underneath
counting in the SOURCE's own time rather than restarting at zero.

**In and out points apply to a cue that is already on air.** Moving the out
point applies with no interruption to the picture at all; moving the in point
re-seats the decode and keeps the same frame on screen. Pull the out point
behind the playhead and the cue loops immediately.

**Effect parameters say what they are setting.** Parameters that map onto a real
quantity read as one - "100 cols", "symbols", "green" - and the rest read as a
percentage. `FX PARAM` answers the same way. Every parameter is still stored
0-1 underneath, which is what lets any of them be handed to an LFO.

**A still looks like itself in the timeline**, drawn in its own aspect on the
tiled path every other cue kind already used.

**The slide renderer has its own animation.** Converting a deck shows the work:
sheets drawn from a hopper, swept by a scan bar, landing on a stack that grows
with the pages as they land.

**The startup face talks, and does something.** Its line is typed out with a
cursor rather than swapped in whole, and it winks - one eye, on its own, every
few seconds.

**macOS builds open on Sonoma and up.** Both architectures are published: an
Intel bundle (macOS 13 and up, which Apple Silicon also runs) and a native Apple
Silicon one (macOS 14 and up).

**An isolated test run stays isolated.** `DECKBOY_STATE_DIR` moves everything
Deckboy writes into a scratch directory, the pointer to your real show included,
so a test run and a real show never meet.

`HELP ALL` counts itself correctly, and `tools/audit_remote_help.py` checks that
number along with the names.


## 2026-09-03 - v0.99.306 (a readable About page, and a mascot you can choose)

**The About page reads clearly on every theme**, dark ones included.

**The mascot can be set to None** - no character art on the splash or the About
masthead - and the choice takes effect the moment you make it rather than at the
next launch. Whichever character you pick is the one you get.

**`SET` applies what it sets.** `SET theme`, `SET ui scale` and `SET mascot`
change the app in front of you as well as in the show file.


## 2026-09-03 - v0.99.305 (the dashboard: your own buttons)

**Build your own buttons.** A dashboard slot is a label, a glyph and any
command Deckboy accepts — so a button can panic the show, switch the blend to
*ember*, arm an output, rename the act, anything you can type:

    DASH SET 1 Panic | PANIC | (warning sign)
    DASH SET 2 Ember | VJ BLEND ember | (fire)
    DASH SET 3 Act Two | SET title Act Two | (clapper)

    DASH LIST       what you have built
    DASH 2          fire slot 2
    DASH CLEAR 3

**And it is the same list from Companion.** Because a slot IS a command, a
surface button bound to `DASH 2` does exactly what pressing it on screen will —
there is no second mechanism to fall out of step. The glyph can be any symbol
or emoji Deckboy can draw, which is all of them.

Slots are saved with the show. A slot that tries to fire the dashboard is
refused, because that is a loop that would run during a show rather than while
it was being written.


## 2026-09-03 - v0.99.304 (every project setting, over the wire)

**`GET` and `SET`.** All 86 named project settings can be read and changed from
a control surface or a script:

    GET                     list every key
    GET <key>               what it is
    SET <key> <value>       change it

Most of these had no command of any kind before, which put a show's finer
settings out of reach of a surface.

A value that gets clamped reports back what it actually became, not what was
asked for: `SET ptp_domain 9999` answers `ptp_domain = 127`. A key that does
not exist is refused rather than quietly ignored.


## 2026-09-03 - v0.99.303 (all six builds, everywhere)

The v0.99.302 Spout input, published for macOS, Linux and Windows builds
without Spout as well. No behaviour change.


## 2026-09-03 - v0.99.302 (Spout input works)

**A Syphon/Spout source cue receives a picture.** Spout senders on this machine
can be taken as a cue and mixed like any other source — Resolume, OBS, another
Deckboy.

`SPOUTCUE <sender name>` adds one, or leave the name off to take whatever is
sending.

**Stopping a live cue stops the capture**, on Spout, NDI and DeckLink alike, so
a stopped cue costs nothing until it is taken again.


## 2026-09-03 - v0.99.301 (a source says what this machine can do)

Adding a **Syphon/Spout source** cue tells you straight away whether this
platform can receive one, and the answer comes from the capture backend itself
rather than from a placeholder that could just as easily be a signal. Spout
receive on Windows and Syphon receive on macOS are on the way.

Sources that already work — window, camera — are unaffected and say nothing
extra.


## 2026-09-03 - v0.99.3 (an output can be driven from a surface)

**`OUTPUT`** — arm an output, route it to Spout, name the sender, and read the
whole thing back:

    OUTPUT              what the focused output is doing
    OUTPUT LIST         every output and whether it is on
    OUTPUT ON|OFF|TOGGLE
    OUTPUT SPOUT ON|OFF|TOGGLE | SPOUT NAME <sender name>
    OUTPUT <n>          focus that output

Spout now has what NDI and DeckLink beside it have always had: a command, so
routing an output to Spout is something a surface or a script can do. The
output's own enable is on the wire too.

**Settings opens on the tab you asked for**, System included.


## 2026-09-03 - v0.99.2 (NDI input works)

**An NDI source cue receives a picture.** It is taken straight from the NDI
runtime, the way a DeckLink input is captured.

Add one with `NDICUE <source name>`, or from the cue menu. The name is matched
loosely, so a show can say "Test Pattern" without knowing which machine will be
sending it on the day, and a cue whose source is not up yet keeps looking —
the other machine is often still booting.

**Stopping a live input stops the capture**, on NDI and DeckLink alike.


## 2026-09-03 - v0.99.1 (rendered slides live with the show)

Slides rendered from a PowerPoint or Keynote go in a `<show>_media` folder
beside the show file. They are not a cache — every cue points at one — so they
belong with the file that references them: copying the show folder copies the
whole show, and deleting it takes the renders with it.

An unsaved show has nowhere of its own yet and uses the state folder until it is
saved. Saving leaves renders that already exist where the cues expect them.


## 2026-09-03 - v0.99.0 (text mode, at full brightness)

**Coloured character art now carries tone with the density of the character and
colour with the ink.** A cell keeps its hue exactly — the channel ratios are
untouched — and the character is drawn at the brightness that hue can reach.
Measured on an ordinary frame, the average lit pixel reads 195 out of 255 where
it used to read 49: a scene rendered as type is legible as a scene.

The phosphor inks — green, amber, cyan and the palette modes — are unchanged.


## 2026-09-03 - v0.98.1 (a dismissed suggestion stays dismissed)

Closing the "HAP would help this show" suggestion keeps it closed, across
launches, as it always said it would.


## 2026-09-03 - v0.98.0 (ten blend modes, faders you can reach the ends of)

### Ten ways to mix two decks

**dissolve, add, screen, multiply, lighten, darken, subtract, undercut,
infiltrate** and **ember**. Only dissolve fades the outgoing deck away; the
rest leave it at full and bring the incoming one in over it, which is what
makes each look like itself.

*lighten* and *darken* pick the brighter or darker deck channel by channel.
*undercut* is subtract in reverse — the outgoing deck eats light out of the
incoming one. *infiltrate* lets the new deck appear only where the old one is
dark, so it grows out of the shadows; *ember* is its opposite and burns in
through the highlights.

`VJ BLEND` with no argument cycles, and a name it does not know is refused
rather than quietly becoming dissolve.

*multiply* rides the fader like the rest of them, all the way up from zero.

### Faders you can reach the ends of

The deck opacity faders can be **dragged**, not just clicked — and 0 and 100
have a landing zone at each end instead of being one pixel wide. The crossfader
gets the same, so full A and full B are reachable without taking aim. The middle
of the rail is as fine as it ever was.


## 2026-09-02 - v0.97.0 (NDI output verified, NDI input tells the truth)

**NDI output is correct** — checked frame by frame against a receiver rather
than by eye: 3840x2160, right way up, colour bars in the right order.

**An NDI source cue says what it can do** the moment you add it, rather than at
showtime: on this build NDI input arrives through ffmpeg's `libndi_newtek`
device, which ffmpeg no longer carries, and the cue tells you so instead of
staying blank.

`NDICUE <source name>` adds an NDI input from a control surface — the last live
source kind that had no command of its own.


## 2026-09-02 - v0.96.0 (Spout output, the right way up and the right colour)

**Spout output arrives correctly in a receiver**, right way up and with red and
blue where they belong — verified against the test card in Resolume with no
correction of any kind on the receiving side. A Deckboy feed drops into
Resolume, OBS or another Deckboy and is usable as it lands.

**The send is considerably cheaper**, going straight from the capture buffer
rather than through a texture of its own: about 66MB per frame of copying saved
at 4K.


## 2026-09-02 - v0.95.2 (a converter that fails now says why)

When a presentation cannot be converted, Deckboy shows what the converter itself
said — "PowerPoint: Exception from HRESULT: 0x80CB4002" — because the tool that
failed is the one that knows why.

Importing two slide decks at once keeps the progress card up until both have
finished, and says how many are running.


## 2026-09-02 - v0.95.1 (the video synth reaches the text mode controls)

The **text mode** row in a video synth cue's settings reads the effect, so the
character-grid rows underneath it appear and answer on the one kind of cue that
has no other way to reach them.


## 2026-09-02 - v0.95.0 (one text mode)

**Text mode is the effect, on any cue.** It used to be two switches over one
renderer — a video synth cue had its own character grid, everything else
carried the TEXT MODE effect — so every control had to work through both. There
is one way in now, and every control works.

**Your shows open exactly as they were.** A cue saved with the old switch gains
the effect on loading, carrying the settings it already had, and a show saved by
this build still opens in an older one.

**Two more character sets everywhere.** **Sprite sheet** and **font (type
anything)** were reachable only from a video synth cue; they can be chosen on
any cue now, and every existing show keeps the set it had.

**Turning text mode off and on again keeps your settings**, all of them.


## 2026-09-02 - v0.94.1 (type your own text into TEXT MODE)

**The glyphs and phrases rows keep what you type**, on an ordinary clip as
well as on a video synth cue.

**Picking a font uses that font** — for plain letters as well as for the
characters the built-in 5x7 face cannot draw. Choose a typeface for its stars
and you keep it for its letters too.


## 2026-09-02 - v0.94.0 (import a PowerPoint or a Keynote)

### Drop the deck in

`.pptx`, `.ppt`, `.pps`, `.key` and `.odp` import directly now. Deckboy asks
whatever owns the format to export a PDF — PowerPoint on Windows, Keynote for
`.key` on macOS, LibreOffice anywhere it is installed — and then renders the
pages the way it already renders a PDF: one still per slide, 3840 wide, each
one holding until you take the next.

**The format's own application does the export, so nothing is reinterpreted.**
Fonts stay the fonts, every box stays where it was put, and images keep their
resolution. Measured on a test-pattern deck, the finest hatching in the card
comes through fully resolved rather than averaged to grey. Where LibreOffice
is the only converter available it is named when the import finishes, because
it substitutes fonts it does not have and that is worth a look before you go
on air.

What a PDF cannot carry, and so neither can this: builds, transitions, and
video or audio embedded in a slide. For a deck that genuinely animates, take
it live with a window-capture source instead.

### Something to watch while it works

A hundred-slide deck takes about half a minute to convert and render. Deckboy
shows you the friend from the startup screen while it does, counting the slides
off with a bar that moves — so the wait has a face on it.

### Smaller things

Messages that tell you something you need to act on stay up long enough to
read, and stand out from the ones that just confirm what you pressed.

Importing media from a drive that has been unplugged, or a folder that has been
renamed, says so.


## 2026-09-02 - v0.93.1 (the picture-following wobble follows the picture)

**flow** mode follows the parts of the picture that have something to follow.
Where the picture has an edge the flow follows it; where it has nothing, the
characters keep to the wave — which is what makes the turn read as one surface.
**hue** mode is weighted the same way, by how much colour a cell actually has.

**Wobble modes are named over the wire**: `flow` and `hue` are the modes, `luma`
and `colour` reach the same two, and a name that is not a mode is refused rather
than quietly meaning `drift`.

A character set typed by hand says when a preset replaces it, the way the
character-set row already did.

With a glitch running, the area around a turned character keeps the background's
own colour.


## 2026-09-02 - v0.93.0 (text that turns, and a show that keeps a spare)

### The wobble turns

Text mode's wobble is a rotation now. Each character sits on its own small card
turning about two axes at once, drawn through the same perspective a camera
would give it: the edges converge, the near side comes forward and the far side
falls away. Large glyphs read as objects with a front and a back rather than as
a picture being waved.

The turn travels across the grid as a wave, so a run of neighbouring characters
moves as one surface and the light sweeps over it — and the light is real: a
character turned edge-on to you goes dark and comes back to full as it faces
you again. That is the cue that makes a turn read as a turn.

There is one knob. At zero nothing happens and nothing is spent.

### The picture stays behind the text

The space around a turning character is the background, whatever size the
characters are and however far they have turned, so the eye stays on the type
rather than being pulled through to the source.

### Presets are a starting point, not a mode

The preset row has a **none** position, so the built-in character sets are
always one step away. Choosing a built-in set while a preset is loaded takes you
to that set and says so, and the row tells you when a custom set is in force.
Whichever row you reach for is the row that answers.

The controls that override one another — preset, font and custom glyphs — sit
together directly under the set they override, in the order they apply.

### A show keeps a spare

Deckboy writes a `.bak` beside a show before it first overwrites it, holding
the show as it was when Deckboy opened it. Saves were already all-or-nothing;
this adds somewhere to go back to.

### Reading the deck back

`ASCII STATUS` answers with the whole TEXT MODE section on one line — ink, set,
preset, columns, chaos, wobble and typeface — so a control surface can show
what a cue is actually doing rather than only tell it what to do.


## 2026-09-01 - v0.92.1 (the glitch takes the picture's colour)

The marks that climb out of the characters are drawn in the clip's own colour,
**picture** ink included, so a glitch reads as damage to the image rather than
as confetti sitting on top of it.

A third of the marks are drawn with one colour channel pulled down, which is the
fringing that separates real digital corruption from decoration. It scales the
cell's own colour rather than adding brightness, so a glitch on a dark part of
the picture stays dark.

Crash reports name the thread that faulted and whether it was the one that owns
the drawing.


## 2026-09-01 - v0.92.0 (curated sets, a typeface of your choosing, and wobble)

### Thirty-two curated sets

A **preset** row in TEXT MODE, cycling through sets of symbols and emoji: dots,
stars, sparkles, music, hearts, flowers, arrows, geometry, blocks, box drawing,
circles, weather, zodiac, chess & cards, runes, greek, braille, currency, maths,
dice, faces — and a dozen emoji sets from faces and nature through show,
party, creatures, space and fruit.

Picking one fills the custom glyphs field and leaves it editable, so a preset is
a starting point rather than a mode. Each is ordered light to heavy, which is
the order text mode maps brightness onto.

### Choose the typeface

A **font** row picks the face the characters are drawn with; click it again for
automatic. A chosen font is tried first and the system fonts still cover
anything it lacks, so picking something for its stars does not cost you the
letters it has not got.

A character no font on the machine can draw is left out of the set rather than
drawn as an empty box.

### Wobble

Each character rocks as though it were a card being tilted — a squash on one
axis, a stretch on the other, and a shear between them. Every cell has its own
phase, so the grid breathes rather than sliding about as one sheet.

**wobble by** decides what aims each tilt:

- **drift** — its own position. Time only.
- **flow** — the picture's luma gradient, so characters lean the way the image
  does and an edge combs the grid along itself.
- **hue** — the cell's colour, so the picture steers the tilt by what it is
  rather than by where its edges are.

It costs nothing at zero.


## 2026-09-01 - v0.91.0 (it can tell you there is a new one)

An **UPDATES** card in the System settings. It asks GitHub whether there is a
newer release, tells you, and offers to fetch it — and that is all it does on
its own.

- **Off until you switch it on.** This is the only connection Deckboy opens
  outward by itself, and a machine on a venue network should do nothing nobody
  asked for. CHECK NOW works whether or not the startup check is on.
- **It never installs anything by itself.** Checking is a check. Downloading is
  a button. Installing is another button, and it restarts.
- **It refuses while anything is live.** Not the download and not the install:
  an update is a restart, and a restart mid-show is the worst thing this
  program could do to you.
- **It tidies up.** A finished installer is deleted at the next start, so a
  90MB file does not sit in your state folder forever.

The download is checked against the size the release reports, so a connection
that drops halfway leaves nothing to run rather than a broken installer.

`deckboy --check-update` prints the answer and exits, and
`UPDATE check|download|install|status` drives it from a surface.


## 2026-09-01 - v0.90.0 (type anything, including emoji)

### Any character you can type

Anything you type or paste into the custom glyphs field is drawn, rendered
through a font on the machine: stars, notes, flowers, arrows, box drawing,
dingbats, scripts — whatever the system has a face for. Each character is
looked up across several fonts, so a symbol missing from one is found in
another, and it is rasterised once per size and cached.

Mixing is just typing: `A♪★b✿` is four ordinary letters and symbols in one set,
ordered darkest to brightest like any other glyph set.

### Emoji

They work, in colour, wherever the platform has a colour emoji font — Windows
and macOS have one as standard, and most Linux installs can add one. A colour
emoji keeps its own colours and ignores the ink setting, which is the whole
point of drawing one.

### Also

The glyph set list gains **font (type anything)**, which forces the font path
even for plain letters. You rarely need it: typing a character no built-in set
has switches to the font on its own.


## 2026-09-01 - v0.89.5 (picture ink means the picture)

Text mode's **picture** ink draws each cell in the clip's own colour. Measured
on one held frame, 19.5% of the source's lit pixels are desaturated and 20.1% of
the glyphs drawn from it are — so ordinary footage keeps the colour it came in
with.

The quantised, sixteen-colour look is what the **palette** ink is for, and that
is unchanged.


## 2026-08-31 - v0.89.4 (fold the inspector down to the effects)

Collapsing the inspector's sections keeps everything where it belongs: EFFECTS
and TEXT MODE continue from wherever the sections above them finished, folded or
not, so you can shut the top of the panel and work on the effects.

`SECTION playback|metadata|geometry|key|effects|timer|tone|text` over the
control protocol folds a section from a surface.


## 2026-08-31 - v0.89.3 (the text mode section works on any cue, and a chaos knob)

### The controls work on a clip

Every control in the TEXT MODE section — columns, glyph set, shuffle, ink, the
glitch amounts, the custom glyphs and the phrases — works on any cue carrying
the effect: a clip, a still, a camera, as well as a video synth cue.

### CHAOS

A new row in the TEXT MODE section. At 0 each cell draws the glyph its
brightness asks for, so the picture reads. At 1 it draws any glyph in the set,
so the whole alphabet appears at once and the grid becomes texture. In between
is the interesting part.

This is how you get every mark in **music & sparkle** on screen: ranked strictly
by ink, a flat area of picture picks one mark and the rest never appear. Turn
chaos up and they all do. It works on custom glyph sets too, picking within what
you typed rather than the table behind it, and it is hashed from the cell
position rather than the frame, so it is texture rather than flicker.

Fewer columns means bigger cells, which is where the notes become legible rather
than reading as dots.

### Also

`ASCII INK`, `ASCII SET`, `ASCII SHUFFLE`, `ASCII COLS <n>` and
`ASCII CHAOS <0..1>` over the control protocol, so a surface can drive the
character grid — and the same commands work on any cue carrying the effect.


## 2026-08-31 - v0.89.2 (the text mode controls drive the picture)

Columns, glyph set and ink in the TEXT MODE section change what you see, on any
cue carrying the effect — which is also how you reach **music & sparkle**: cycle
the GLYPHS row.

One mapping between the effect's four parameters and the grid, shared by the
renderer and the inspector, so what a row says and what the frame shows are the
same thing.


## 2026-08-31 - v0.89.1 (text mode fills the frame)

The character grid reaches every edge of the raster. Cell edges land on
proportional boundaries rather than a fixed cell size, so the last column and the
last row finish exactly on the frame edge at any column count and any output
size. The glitch marks follow the same grid, so they stay in their cells out to
the edge.


## 2026-08-31 - v0.89.0 (marks to draw with, devices that come back)

### A marks alphabet for text mode

A new glyph set, **music & sparkle**: dots, rings, an arc, a tilde, plusses,
crosses, diamonds, a star, and quarter, quaver, beamed and double-beamed notes.
Cycle to it on the GLYPHS row like any other set, or reach it from the text mode
effect's glyph-set parameter — so an LFO can sweep the alphabet along with
everything else.

The custom glyph field understands pasted characters. Type or paste a row of
marks and they map to the ones that are drawn, with several spellings each — a
star arrives as a different character depending where it was copied from, and
they all land on the star. Custom sets can mix ordinary letters and marks
freely, and the existing SHUFFLE seed re-maps which mark carries which
brightness, so one set gives many different hands.

### Devices you named, kept

A deck remembers the audio interface you chose, even when it is not there yet.
Start the machine before the rack is powered on and the deck says so — "not
found, on default" — rather than quietly forgetting what you asked for. When the
interface appears, the deck moves back to it on its own; when one is unplugged
mid-show, the deck moves to the system default.

The same for a control surface. The chosen MIDI port is saved with the show, so
it survives a restart, and a port that is not present is reported by name.
Unplug the surface and Deckboy says so; plug it back in and it reconnects
itself.

**`deckboy --devices`** prints the audio devices, displays, MIDI ports and
render drivers this machine offers, with each device's real rate and channel
count and each display's real refresh and scaling — and it spells the names the
way a show file needs them.

### Audio cues can be trimmed

In and out points on an audio cue, in the inspector and over the control
protocol, the same as a clip. A music bed can start eight bars in.

### Steadier

- The show file is written beside itself and renamed into place, so it is never
  half-written on disk.
- Opening a show leaves it untouched.
- Streams and capture inputs get GEOMETRY, KEY and EFFECTS in the inspector.
- A DeckLink input has a full inspector, and names the card it is watching.
- Timecode readouts roll over correctly at the minute.
- `IN`, `OUT` and `MIDI` over the control protocol report what they actually
  did, with the reason when they could not.


## 2026-08-29 - v0.88.0 (text mode everywhere, and a language to write in)

### Text mode is an effect, so it works on anything

The character grid is no longer part of the video synth. Put **TEXT MODE** on
a clip, a capture card, a camera, a browser cue or a still, and it draws as
characters — the same grid, the same glyph sets, the same phosphors, on
whatever the cue happens to be.

A cue carrying the effect gets its own TEXT MODE section in the inspector with
the full set of controls: columns, glyph set, shuffle, ink, custom glyphs,
phrases and phrase hold. Four of them — columns, corruption, glyph set and ink
— are also effect parameters, so the ones worth grabbing mid-set sit on faders
and can take an LFO.

Its amount is a **mix**, not a switch. At 1.0 the grid replaces the picture;
part way it sits over the original like a screen door, which is where a lot of
the best-looking settings turn out to be.

### Bring your own characters and your own words

Text mode takes a custom alphabet and a list of phrases:

- **custom glyphs** — the characters the picture is built from, darkest first.
  Two characters gives binary rain; a word gives that word as texture;
  box-drawing pieces read as a schematic.
- **phrases** — words separated by `|`, one showing at a time, landing
  somewhere new each time it moves. **phrase hold** sets the dwell.

Over the wire as `ASCII ON|OFF|TOGGLE`, `ASCII GLYPHS`, `ASCII PHRASES` and
`ASCII HOLD`, with Companion actions for all of it.

### The code source is a language now

It reads like code because it is. Name values, build on them, and end with what
the channels should be:

```
ox = sin(t)*0.55;
oy = cos(t*0.8)*0.4;
d  = length(cx-ox, cy-oy);
glow = smoothstep(0.45, 0.0, d);
glow, glow*0.35, 1-glow*0.6
```

Named values are also faster: a distance used by three channels is computed
once rather than three times. A source with no semicolon in it is exactly the
one-line form, so everything already written keeps working and keeps meaning
the same thing.

Seven more functions — `length`, `smoothstep`, `if`, `sign`, `exp`, `log`,
`atan` — and the editor grew to match: **Shift+Enter** for a new line, the
field sized to the lines in it, your own names syntax-coloured as names, and
six worked examples that start from the statement form. The helper alongside
it is set in a bigger, brighter face and explains each function as you reach
for it.

### VJ mode has a switch

`Settings → System → SHOW FLOW`, at the top: one deck and a playlist, or two
decks and a crossfader.

### The creatures come out when you want them

The switch is three-state: **off**, **when idle**, or **always**. "When idle"
stays the default and keeps the chrome still during a show; "always" is for
anyone who would rather have them there regardless.

### Steadier through long sessions

The video synth holds a flat memory footprint however long a cue stays live,
and runs at full frame rate at 4K. Text mode renders at 60fps on a 4K raster.


## 2026-08-29 - v0.87.0 (VJ mode, a code source, and eight effects nobody has)

Two decks, a crossfader and a tempo; a source you write instead of load, with
a real editor and a friend to explain it; an LFO on every effect parameter; and
eight new effects, six of which come out of physics rather than out of another
plugin.

### VJ mode

A toggle. Off, Deckboy is a cue deck and every existing show renders exactly as
it did, through the same code path. On, two decks run at once and a crossfader
decides what the audience sees.

Each deck has always had its own engine, playlist and transport; what VJ mode
adds is an output fed by both of them at once, through the layering the
compositor already had. A deck faded down, or mid cue-fade, stays faded down.

Both decks fade on a dissolve, not just the incoming one: they are drawn over
black, so holding A at full until B covered it would be a wipe. **Add** and
**multiply** are ways of combining two pictures, so there the base stays at
full and only the incoming deck rides the fader. Verified by recording the
composite with deck A solid red under deck B solid blue -- dissolve walks
250/0/0 to 0/0/253 through 64/0/127, add gives magenta, multiply gives black.
Colours in neither clip, which is the proof they are combined and not switched.

**Tap tempo** averages the recent taps rather than taking the last interval:
nobody taps evenly, and one interval makes the tempo jump on every beat. Taps
more than two seconds apart start again, because that is a person restarting
and not a 25bpm track. **Quantised takes** hold until the next beat -- the
point of tempo in a video mixer is not that anything moves by itself, it is
that what the operator does lands ON the music. Measured at 60bpm: unquantised
takes fire in 0.04s, quantised ones wait between 0.16s and 0.81s depending on
where in the beat they were asked for.

**It announces itself.** A mode you can enter without noticing is a mode that
ruins a show, so there are two signals: a band across the program column that
exists only in VJ mode and carries the controls rather than just announcing
itself, and the whole window edged in a colour used nowhere else -- for the
glance across a room before anyone touches the machine. Both playlists are on
screen side by side, each headed with which side of the crossfader it is,
because two lists both saying PLAYLIST is how the wrong clip reaches an
audience.

The animation carries information rather than decorating. The bar drops in over
a third of a second so the layout settles instead of jumping; the badge and the
frame breathe on the beat, which doubles as a tempo readout you can see without
looking at the number; and the fader handle leans the way it is travelling and
trails a wake that fades as it settles. The crossfader's readout sits in a dark
well, legible at every position, and the controls squeeze toward a floor so the
fader keeps a width worth dragging on a small window.

`VJ ON|OFF | MIX <0-1> | BLEND <dissolve|add|multiply> | TAP | BPM <n> |
QUANTISE <on|off> | DECKS <a> <b> | STATUS | TOGGLE` over the wire, because a
crossfader is a fader and a fader is the one control nobody wants to reach for
with a mouse.

### A code source you can write during a show

A pattern type called **Code**: the picture is an expression, evaluated per
pixel, edited while it runs.

    sin(x*12+t)*0.5+0.5, sin(y*9-t)*0.5+0.5, r

One expression, or three separated by commas for red, green and blue.
Variables are `x` `y` (0-1 across the frame), `cx` `cy` (-1..1 from the
centre), `r` (radius), `a` (angle) and `t` (seconds), with `sin cos tan abs
floor fract sqrt min max mod pow atan2 step clamp mix` to build from.

**Why not GLSL.** Deckboy draws through SDL_Renderer, whose backend is D3D11,
D3D12, Metal or OpenGL depending on the machine, and SDL's own shader path
wants SPIR-V, DXIL or MSL -- already compiled. Accepting GLSL at runtime on
every platform would mean bundling a shader compiler, tens of megabytes and a
per-backend translation step, to run arithmetic that fits in a few hundred
lines. So it is evaluated on the CPU, which is viable for the same reason the
effect stack is: the frame splits across cores.

The expression is compiled ONCE into a flat instruction list, cached against
its own text, and the inner loop sees only the instructions -- never a syntax
tree, which would spend its time chasing pointers instead of drawing.

**A compile error does not black the output.** The cue keeps drawing what it
last drew and the error appears in the inspector. Someone editing live is
mid-keystroke most of the time, and a source that goes black on every
half-typed function is unusable on a stage. Division by zero, mod by zero and
the square root of a negative are all bounded rather than producing infinities,
because an operator typing at speed will produce all three. The language has its
own test suite.

### The code source gets a real editor

The expression opens into a proper editor. The text is **syntax coloured** —
functions, values, numbers, brackets, operators and the commas that split red
from green from blue each have their own colour, and a name the compiler will
refuse is **red while you type it**, before you find out by looking at the
output. The caret moves with the usual keys and you can click into the text to
place it.

The colouring reads the compiler's own tables rather than keeping a copy, so it
cannot fall out of step with the language: "shown in red" means exactly "this
will not compile".

Every variable and function is a **chip that inserts itself** — a function
arrives with its brackets and the caret already inside them. The **examples are
a picker**, so you can see what you are about to get and go back.

And there is **a friend in the corner**, the same face that waits in an empty
program monitor, who tells you what the name under your pointer does — and reads
you the compile error when there is one. A syntax reference is a wall of names;
someone telling you what the one under your finger means is the same information
with a face on it.

`CODE GET | CODE SET <expression> | CODE EDIT` over the wire, so an expression
can come from a controller or a script and not only from typing.

### The effect chain tells you what it costs

A cue has always been capped at twelve effects, but a cap only bounds the
damage — a dozen cheap ones are free and four expensive ones at 4K are not, so
the count an operator can already see is the wrong number.

The EFFECTS section shows what the chain **actually costs per frame**, measured
on that machine at that raster while the cue is live, against the 16.7ms a 60fps
frame allows. Over budget, it says so. That is the difference between "it is
stuttering, why" and "this chain costs 47ms".

Measured rather than predicted, and only once it has run: a figure added up
from per-effect benchmarks would be a guess about somebody else's hardware,
which is exactly what the number is there to avoid.

Going over is not a failure, and it is worth knowing what it does. **Audio is
the master clock**: sound continues in real time and the picture slaves to it,
so you lose frames rather than sync.

### Effects on every kind of cue

The EFFECTS section is on all of them — a pattern, a still, a camera, an NDI
feed or a stream carries an effect stack and renders it exactly as a clip does.
Proven by adding grain, caustics and a vignette to a colour-bar pattern over the
wire and watching all three come out.

### The Companion module knows about all of it

Proper actions for the crossfader, blend mode, tap tempo, BPM, quantised takes
and deck assignment; for adding an effect, its amount, its parameters and its
LFOs; and for setting a code-source expression.

The crossfader and the tap are the point of it. Those are precisely the two
controls nobody wants to reach for with a mouse, which is the whole argument
for having a surface at all.

### Six effects that are not in anything else

Each of these comes out of something real -- an instrument, a physical
experiment, a solid-state process, or your own retina -- rather than from
stacking two existing filters. All six fit inside a 60fps frame at 1080p.

**Schlieren** is how physicists photograph air. You cannot see a shockwave or
the heat off a road, but light bent by a density gradient can be passed or
blocked by a knife edge at the focus, which turns an invisible gradient into
brightness -- it is how every photograph of a bullet's shockwave was taken. Here
the picture is the density field, and what comes out is not the image and not
its edges but the RATE at which it is changing, in one chosen direction, with
everything flat left as mid-grey. Turning the knife changes which features exist
at all, because gradients running along the edge miss it entirely. 4.7ms.

**Chladni** is the shape a sound makes. Sand on a bowed metal plate runs away
from everything that is moving and piles up along the lines standing still;
Chladni catalogued those figures in 1787 and they are why violins are the shape
they are. Your picture is the sand. The two mode numbers are the note: whole
numbers give the clean classical figures, and between them the plate is being
driven at a frequency it does not want, which is exactly what a real plate does.
7.2ms.

**Wavefront** solves the actual wave equation, seeded from the picture's own
brightness -- so unlike every sine-based ripple in every video app, it has
INERTIA. Waves leave their source and keep going, pass through each other and
interfere, and reflect off the edges of the frame and come back. None of that
can be faked with a sine, and all of it is what a real surface does. 14.9ms.

**Crystallise** is grain growth, not a mosaic. A mosaic divides the frame into a
grid; metal does not solidify on a grid. Crystals nucleate at scattered points
and grow until they collide, so the cell a pixel lands in is the one whose seed
reached it first -- and because the seeds grow at DIFFERENT SPEEDS, the result
is the irregular shard structure of a polished metal section rather than a
honeycomb. Each grain takes a facet normal from the direction back to its own
seed, so the light catches it. 15.7ms.

**Night eyes** is your own retina. Rods are fast, sensitive and completely
colour-blind; cones see colour and are slow and need light. So the brightness
runs at full speed and the COLOUR LAGS BEHIND IT: move something and it goes
grey as it moves, its colour catching up a moment later. The purkinje control is
the other half -- as the rods take over, peak sensitivity slides toward blue,
which is the real reason night looks blue and moonlight photographs that way.
3.8ms.

**Grain flow** smears the picture along its own grain. Line integral convolution
is how a vector field is drawn in scientific visualisation; pointed at an image's
own structure it makes every stroke follow the direction that part of the picture
is already running -- along a hair, around a jaw, down the length of a shadow.
The direction comes from the structure tensor, the direction in which each
neighbourhood changes least, which a plain gradient cannot give you: a gradient
says which way is uphill, not which way the ridge runs. Flat areas are left
alone. 13.0ms.

**At 4K the budget is a different question**, and it always has been. Measured
at 3840x2160: night eyes 14ms and schlieren 17ms still fit; chladni is 27ms,
grain flow 46ms, crystallise 54ms and wavefront 57ms -- one to three frames
each, alongside existing effects like caustics at 23ms. 1080p is the promise;
4K is one heavy effect at a time on a fast machine, and the app tells you what
any of them costs on YOUR machine with `--effect-bench <token> 3840x2160`.

### Caustics: the light, not just the bend

Every "water" effect displaces the picture. This one also computes what the
water does to the LIGHT, which is the part the eye actually reads as water.

A refracting surface bends what you see through it and, in the same motion,
concentrates or spreads the rays doing so. Where neighbouring rays are pushed
toward each other the brightness piles up, and those bright filaments are
caustics -- the moving net of light on the floor of a swimming pool. The
focusing term is the DIVERGENCE of the displacement field: one finite
difference per cell, and it is the whole difference between this and a ripple.

Four crossed waves at different angles and rates, so it never reads as a grid.
**Chop** runs from long ocean swell to rain on a puddle, **swell speed** sets
the rate, **focus** how hard the light gathers -- through a tanh, so a strong
swell makes filaments instead of clipping to white. 1.9ms at 1080p.

### Feedback that cannot run away

A camera pointed at its own monitor, except the transform between passes is
chosen rather than accidental -- and bounded, which is what makes it usable on
a stage.

Scale the echo slightly up and it walks toward you as a tunnel; scale it down
and it retreats; add a turn and the tunnel becomes a spiral; slide it and it
smears into a comet. Those are the four controls, because that is the loop:
**zoom**, **spin**, **drift**, and **colour bleed** for a trail that changes
colour as it fades rather than only going dim.

Real feedback blows out to white the moment the loop gain passes one, and there
is no getting it back during a show. So the echo LIGHTENS instead of adding: the
brighter of the live pixel and the decayed echo. Adding has a fixed point
several times the input; lightening has its fixed point at the input, so the
picture can never come out brighter than the picture went in. Measured over 120
passes it settles and stops moving, to within three levels out of 255.

The loop is cleared at every take, so a new cue never opens with a ghost of the
last frame of the old one. Two outputs showing the same deck step it once
between them, not once each. 1.0ms at 1080p -- 5.4x faster than the
straightforward version, and byte for byte the same picture.

### An LFO on any effect parameter

Every parameter of every effect — and the effect's **amount** — can be handed to
an oscillator instead of a fixed number. A `~` sits at the right of each
parameter row; switch it on and the parameter starts moving, with its shape,
rate and depth on the line underneath.

**Sine, triangle, saw, ramp, square, and sample-and-hold** — one random value per
cycle, *held*, so it steps rather than fizzes. The held value is hashed from the
cycle number rather than drawn from a generator, so the same moment of the show
always gives the same value: a random that differs between the rehearsal and the
performance is not usable.

**It can follow the tempo.** VJ mode already has a tap tempo, so an LFO that
ignored it would be a second clock in a machine that already knows what the
music is doing. Locked, the cycle is measured in beats — a quarter of a beat up
to thirty-two — and it steps through musical lengths rather than by a fixed
amount, so every stop is a length someone would actually choose. Free-running,
the rate is *multiplied* rather than added: the useful range runs from one cycle
a minute to several a second.

**The swing is centred on the value you set**, so switching an LFO on never jumps
the picture: it starts moving from where the parameter already was, and averages
back to it. It is clamped to the parameter's own 0–1, and near the ends the swing
goes lopsided rather than out of bounds.

The oscillators are evaluated **outside** the effects, into a modulated copy of
the stack. The effect code stays a pure function of its inputs — which is what
lets it be dumped headlessly, benched, and applied by the output and the preview
independently. Both paths read one clock sampled once per frame, so the
operator's monitor and the audience's screen are never at different moments of
the same oscillator. A cue with no LFO does not pay for the copy.

Saved on the end of each effect entry and only when armed, so every show ever
saved still loads — and a show saved here still loads in a build that predates
the feature.

`FX LFO <n> <A-E> on|off|shape|rate|depth|phase|sync|beats [value]` over the
wire, where E is the amount.

### Motion puppetry has memory now

The puppet has a spring and an accumulator: **memory** is how much each frame's
motion adds to what is already there, **spring** how fast it returns to rest.
Both are needed -- memory alone runs away, a return alone never builds. Measured
on the same driver, the mean displacement went from 10.5 to 25.9 grey levels per
pixel.

memory 0 returns the raw per-frame field, which is what every show saved until
today carries, so none of them change.

### Time-based effects run on stills

Grain moves, a ripple travels, and caustics and feedback -- whose entire subject
is motion -- animate on a still cue as well as on a clip. Measured on a static
colour-bar cue: 9.7% of the monitor changes between two shots a second apart
with grain, 2.0% with a ripple, 2.8% with caustics.

Video is untouched: the look still follows the SOURCE frame, so a given frame of
a clip always grades the same way and a recording stays reproducible.

### The same picture on every platform

Pixel sort now sorts on a total order, so the same cue on the same frame renders
identically under macOS, Windows and Linux.

### Releases build and publish themselves

Tagging produces all six packages and publishes them:
`-windows-x64-setup.exe` and `-windows-x64.zip`, `-macos-arm64.dmg` and `.zip`,
`.AppImage` and `-linux-x86_64.tar.gz` — an installer and a portable build for
every platform, which is what the README has always promised.

Nothing is built in the publish step. Every file is downloaded from the job
that already tested it, so what reaches the release page is the same file that
passed `--smoke` -- and both packaging jobs unpack their own output and run
the binary from inside it before uploading, because an installer nobody has run
is a guess. The release refuses to publish unless all six are present: a
half-empty release page looks like a release. The notes come from this
changelog's own section for the version being tagged, so the release page and
CHANGES.md cannot drift apart.

Packaging runs on every push to main, not only on tags. Only the publishing is
tag-gated -- so the packaging is exercised continuously.

### Smaller things

- `--effect-dump` takes a pass count, so an effect whose whole subject is what
  happens across frames can be rendered headlessly.
- `check_effects_offline.py --animation` renders every effect at nine frame
  indices and checks that the ones which claim to animate do.
- The preview sweep measures edge energy as well as differing pixels, so it can
  see an effect whose job is to smear, and runs against a held clip frame — half
  fractal, half colour bars — so a result never depends on where a seek landed.
- The text timeline and the cue inspector keep clear of their margins.


## 2026-08-27 - v0.86.0 (recordings that move, effects, a faster synth)

### Recording captures the whole take

A recording is the moving picture that went to air, frame for frame. The
readback into the staging ring is flushed and drained through a bounded queue,
so the file carries every distinct picture the show produced: the deliberately
starved case -- forced synchronous readback at 4K60 -- lands 896 frames of 902,
and a repeat costs a pointer rather than a 33MB copy.

`tools/record_rate_check.py` samples the finished file and counts *distinct
pictures*, so a recording is checked on what it shows rather than on how many
frames it claims.

### Segmented recordings carry continuous timecode

Roll a take into four files and they lay end to end: each segment starts at the
take's start plus every frame already written, with real SMPTE arithmetic
underneath, because drop-frame is a renumbering and frames and timecode are not
interchangeable. Verified across a minute boundary that correctly does not drop,
being a tenth minute.

### The dropped-frame alarm tells the truth

The readback is a three-deep pipeline, so a healthy 4K recording sits a constant
few frames behind and that is not a fault. The alarm now warns when the picture
has gone STALE, which is the thing that matters, and separately checks that the
writer is draining.

### The video synth is six times faster

6.1ms a frame at 4K, down from 35-40ms. The CRT stage runs on the small buffer
where the rest of the synth lives rather than on the full raster after the
upscale, and the per-frame 33MB clear the upscale immediately overwrote is gone.

Recording a synth cue went from 233 frames of 360 with twelve alarms to 354
with none.

### Per-cue effects

An ordered stack on each cue, applied in the order you arrange it: invert,
posterise, solarise, threshold, vignette, grain, scanlines, RGB split, temporal
dither and motion puppet.

**Temporal dither** quantises hard to a tiny palette but advances the dither
every frame, so at 60Hz the eye integrates shades that are not in the palette
at all -- and it freezes into visible checkerboard the moment you pause.

**Motion puppet** is the one to try. A cue can name a *driver* clip, which is
decoded only for the per-macroblock motion vectors its codec already computed;
its pictures are never shown. Those vectors displace this cue's pixels, so a
camera feed can be puppeteered by a crowd scene. `--motion-probe <file>` tells
you whether a clip makes a good driver before you wonder -- a mostly static
clip moves 1.7% of its cells and does nothing visible; a rotating one moves 50%
and is violent.

**The preview shows them**, with or without an output armed: the preview's
fallback path runs the same grade and the same effect stack the output does, so
a look dialled in at the desk is the look that goes to air.

`tools/check_preview_effects.py` sweeps all fifteen pixel effects through that
path with no output armed, seeking and pausing so every case is the same frame
and only the effect differs. All fifteen change the picture.

### The splash is the first thing on screen

The control window is created hidden and shown after the first present, from
whichever path draws first — the main frame, or the loading overlay when a show
is already opening. Launch opens on artwork rather than on an empty rectangle.

### Slide decks import as cues

Drop a PDF on Deckboy and it becomes one image cue per page, named after the
document -- "keynote 1", "keynote 2" -- in order, each one holding until it is
taken. Page Down on a presenter's clicker walks them.

After the import a slide is an ORDINARY CUE. It fades, it carries effects, it
crossfades to the next one, it can be reordered, and nothing during the show
depends on a document renderer. That is not a shortcut, it is the point: a
renderer that stalls mid-keynote is a black screen in front of an audience.

**Each platform's own engine, no bundled library.** Windows renders through
`Windows.Data.Pdf`, which is what Edge uses. macOS goes through CoreGraphics,
which is what Preview uses. Linux uses `pdftoppm` from poppler-utils, which is
what the desktop already renders PDFs with, and says so plainly if it is not
installed.

Pages render **3840 wide**, whatever shape the page is, because a slide is
mostly type and type is the first thing to fall apart scaled up to a 4K output
-- and once the page is a PNG the detail cannot be recovered. They go to the
state directory, never next to the operator's document, whose folder is
read-only as often as not.

A target width rather than a scale factor, because the three engines measure a
page in three different units: WinRT reports device-independent pixels at
96dpi, CoreGraphics reports points at 72dpi, pdftoppm wants a dpi. And on
Windows the first page is rendered, its width read back from the PNG header,
and the request corrected by whatever the machine's display scaling actually
applied -- so a deck imports at the same resolution whichever monitor the
operator happens to be sitting at. Verified on both: the same PDF renders 3840
wide on Windows and on Linux (Mint, poppler 24.02), to within a pixel of height
from each engine rounding the aspect its own way.

Rendering happens on a worker thread. A hundred-page deck takes seconds, and
doing it inline would freeze the app during load-in with no indication why.

Each page is set to HOLD rather than to auto-advance, so a slide waits for the
presenter rather than for a timer.

### What a PDF cannot carry, and what to do instead

**PowerPoint flattens every build to its final state on export, and drops
transitions entirely.** No PDF-based route can recover them; the information is
not in the file. So:

- A deck of **static slides** imports perfectly, and Deckboy's own cue
  transitions handle slide-to-slide -- which is arguably better, because they
  match the rest of the show rather than PowerPoint's idea of a wipe.
- **Keynote** can export one page per build stage. Those arrive as one cue per
  stage, so clicking through reproduces the builds exactly.
- A **PowerPoint deck that genuinely animates** should not be flattened at all.
  Run it in PowerPoint and capture it with a window-source cue: the builds and
  transitions are then the real ones, and the presenter's own clicker drives
  them.

Dropping a `.pptx`, `.ppt`, `.key`, `.odp` or `.pps` says which of those routes
to take, rather than leaving the operator guessing at 10am on a show day.

### Every effect has real controls now

Forty named parameters across the eighteen effects, up from six. Invert has a
pivot and a channel spread, so the negative can come back coloured and fold
around something other than mid grey. Vignette has size and falloff. Grain has
grain size and whether the noise is one value across all three channels (film)
or three (video). Scanlines has darkness. RGB split has an angle and a green
split, so a two-colour fringe becomes a prism. Pixel sort can run its runs
backwards. Ripple has frequency and speed. Kaleidoscope rotates. Reaction bloom
gained seed density and a glow mode that lifts the growth toward white instead
of folding the picture through its negative.

Every parameter is named by the effect itself, and an effect that does not use
one draws no row — so the inspector never shows a control that cannot do
anything. `tools/check_effects_offline.py --params` checks that every named
parameter actually moves the picture.

**The neutral values are load-bearing.** A show saved before an effect grew a
parameter reproduces exactly what that effect did before it had them. That is
checked, not asserted: the older header is compiled into a second binary, and
all fifteen pre-existing effects render byte-identically at the settings
existing shows carry. paramC and paramD serialise after the bypass flag, so
every show ever saved still loads, and a show saved here still loads in a build
that predates them.

### A presenter remote works

Page Down and Page Up take the next and previous cue. Every presentation
clicker -- D'San Perfect Cue, Logitech, Kensington -- appears as a USB keyboard
sending exactly those two keys, because that is what PowerPoint and Keynote
listen for, so a clicker plugged into Deckboy drives the show.

### Every effect fits inside a 60fps frame at 1080p

Measured with `--effect-bench`, median of eleven frames, 1920x1080:

| effect | per frame | effect | per frame |
|---|---|---|---|
| scanlines | 0.65ms | luma displace | 5.59ms |
| solarise | 0.64ms | polar warp | 7.50ms |
| posterise | 0.67ms | ripple | 7.19ms |
| temporal dither | 0.74ms | relativistic | 8.05ms |
| invert | 0.78ms | pixel sort | 9.13ms |
| channel offset | 0.87ms | kaleidoscope | 10.8ms |
| threshold | 0.93ms | dye advect | 14.9ms |
| grain | 1.74ms | reaction bloom | 15.6ms |
| vignette | 2.29ms | block glitch | 2.70ms |

**Every one of these is byte-identical to what it replaced.** That is checked,
not asserted: the pre-change header is compiled into a second binary and both
render the same frames, compared byte for byte.

Four things did it. **A table instead of the arithmetic**, for the effects that
produce at most 256 distinct answers from two million evaluations — built with
the *same* expression, so it is a lookup of the old answer rather than a new
approximation of it. **The frame split across cores**, which every effect but
block glitch can take, since each output row is written from inputs in that same
row or in an untouched copy. **Threads created to fit the shape of the work**:
reaction-diffusion is hundreds of small dependent steps, so its threads are
created once and parked on a barrier between them. And **bulk moves instead of
per-pixel ones** — block glitch's wrapped shift is a rotation, so it is two
`memcpy`s per row.

At 4K the picture is honest rather than solved: the table-driven effects are
1.5-2.6ms, but the ones that gather from somewhere else in the frame -- pixel
sort, the warps, dye advect -- are 25-70ms, dominated by random access across a
33MB buffer rather than by arithmetic. Lowering an effect's detail parameter is
the lever there.

`--effect-bench <token[:amt[:a[:b]]]> [WxH] [frames]` reports the median cost
per frame and what share of a 60fps budget it is.

### Three effects that are not in anything else

**Dye advect** treats the picture as dye in a fluid and carries it along the
flow of its own structure. The velocity field is the *perpendicular* of the
luma gradient, which is the part that matters: a gradient points across an
edge, so its perpendicular runs along one. Advecting down the gradient smears
the picture into mush across its own boundaries; advecting along it makes
colour orbit the shapes instead, and edges survive as the banks of a river.
Every pixel walks backward through the field for several short steps rather
than one long one, because a single jump follows a straight line and the curl
is the whole point.

**Reaction bloom** is Gray-Scott reaction-diffusion, seeded by the picture and
grown a few hundred iterations every frame. Two notional chemicals; one rule;
the coral, veins and dividing spots Turing predicted in 1952. The pattern is
not drawn, it *grows*, and it grows out of whatever is on screen -- so a cut to
a new shot grows a new organism. Feed and kill are one knob rather than two,
walking *along* the documented living presets -- waves, labyrinth, coral, worms,
holes -- because the living region is a thin curved sliver of that plane.

**Lightspeed** is what the frame looks like from something travelling into it
at a fraction of c. Relativistic aberration folds the forward hemisphere toward
the direction of travel, so the centre opens out and the rim smears away --
which is why the view from a near-light ship is a bright compressed disc and
not a zoom. Doppler is the other half: light from ahead arrives blueshifted and
brighter, light from the sides redshifted and dimmer. Without it the warp reads
as a lens; with it, as speed. Both halves depend only on distance from the
centre, so they are a radial lookup built once per frame rather than an `acos`
and two cosines per pixel.

### Effect chains move between cues

`copy chain` / `paste chain` at the foot of the effects section, and `FX COPY`
/ `FX PASTE` over the wire. Separate from the whole-cue COPY on purpose: that
one brings geometry, fades, crop and colour with it, which is not what is
wanted when the only thing worth keeping is the look that took twenty minutes
to dial in. Paste applies to every selected cue, and the driver travels with
the chain -- a motion puppet pasted without its driver is an effect that does
nothing and gives no reason why.

The whole-cue COPY carries the effect stack and the motion driver too, so
copying a graded cue gives back the look as well as the geometry.

### The motion driver has a preview and a scrub bar

The driver is not a cue: it never reaches the screen. So the inspector shows a
thumbnail of it, its position and field count, and a bar that can be clicked or
dragged to place it — and it advances in the preview as well as on the output,
with the first ask of each frame advancing it and the rest served the same
field. The thumbnail costs nothing: the decoder produced the picture on the way
to the vectors.

### A driver with nothing to drive is removed

Remove the last motion puppet from a chain -- or change it into another effect,
or clear the chain, or paste a chain that has no puppet in it -- and the motion
driver goes with it, so nothing is decoded for a field nothing reads.

A *bypassed* puppet still counts. Bypass is a temporary "not right now", and
throwing away the driver the operator chose because they muted an effect for a
moment would be losing their work to a toggle.

### On the wire

`HELP` covers the effects verbs, and `FX LIST` answers with the list in the
reply where a control client can read it, rather than putting it in a toast.

### Inspector

Eighteen values on the video synth, tone generator and chip synth can be
clicked and typed instead of only nudged: speed, scale, feedback, zoom, audio
reactivity, detail, smear, glitch, CRT, sprite spin, level, frequency, note,
attack, release, mod depth, mod ratio and retrigger. Holding shift while
dragging a value is a fine adjust.

### Elsewhere

- Cue schedules fire from the moment the show is running, so opening a file at
  2pm leaves the morning's cues where they are.
- `GOEND` over the wire jumps the playing cue to its last moment.
- `--effect-dump <token[:amount[:a[:b]]]> <in.ppm> <out.ppm> [frame]` applies
  one effect to one picture with no window, no decoder and no timing, and
  reports what it cost; `tools/check_effects_offline.py` runs the whole set
  through it in seconds and can write a contact sheet.
- `--inspector-scroll` reaches the whole inspector from the command line, the
  same way `--settings` opens a settings tab.
- `tools/audit_actions.py` finds dead controls: actions with no handler,
  actions nothing can fire, and duplicate settings ids. 257 actions, no
  orphans.
- Luma is computed from the right channels throughout, so luma displace bends
  the picture by the brightness you can see.
- macOS and Linux get the asynchronous readback through SDL_GPU. SDL 3.4 is
  now the floor.


## 2026-08-24 - v0.85.0 (program recording, audio in, the synth sources)

Deckboy can now record what it puts to air, take audio *in*, and generate its
own pictures and sound. Three capabilities it did not have, plus the recording
work that turns the first one from a viewing copy into something an edit suite
will accept.

### Program recording

The program output can be written to a file while the show runs. RECORD sits on
the button bar in the OUTPUT group, pulses while armed, and shows the running
file size; the destination is its own setting, separate from the encode queue's.

Getting from "it writes a file" to "a facility would accept the file" was most
of the work:

**The recording is its own standard.** Raster and rate are set independently of
the programme -- `RECFORMAT 1920x1080 59.94` off a 4K programme scales on the
GPU before readback, so the recording moves a quarter of the bytes. Both default
to *following the input*, because a recording should look like what went in
unless somebody says otherwise. Rates are exact where broadcast says they are
exact: 23.976 is 24000/1001, not 23.98.

**Constant frame rate by construction.** The file contains exactly
`rate x elapsed` frames. The pacer counts what is owed and repeats the last
picture to cover a gap, so a take is the length it was, and if it cannot keep up
it says so out loud, once a second, on the output health state and in the show
log.

**Timecode.** Start at a value, at time of day, or at zero, drop-frame or
non-drop, with auto picking correctly by rate -- DF only means anything at
29.97 and 59.94, where it skips two timecode *numbers* a minute (except every
tenth) to keep the count against the clock. No video frame is ever dropped. The
flag is carried in the file as SMPTE intends it.

**Codecs a post house asked for.** ProRes (LT, 422, HQ, 4444) and DNxHR
(LB, SQ, HQ, HQX) alongside H.264 and HEVC, in the right container, at the right
pixel format, with the right vendor tag.

**Segmentation and safety.** Roll to a new file every N minutes or N megabytes,
with a 3.8 GB ceiling so a FAT32 card cannot silently truncate a take. On stop,
a fragmented recording is remuxed into a normal MP4 -- the same trade OBS makes,
so a power cut leaves a playable file and a clean stop leaves a tidy one.

**It keeps up.** The frame leaves the GPU through an asynchronous staging ring
rather than a synchronous read, and the control window stops taking vsync while
an output is recording. Verified against a 4K programme: 2160p25, 2160p30,
2160p50, 2160p59.94, 1080p50, 1080p29.97 and ProRes HQ 1080p25 all frame-exact.

That fast path is Windows-only, so macOS and Linux take the portable read. It is
measured, not assumed -- frame-exact at 1080p50, 1080p59.94 and 2160p25, and
behind only at 4K above 30p, where the alarm fires.

### Audio input

Microphone and line input, with device selection, gain, a clip indicator, mono
folding, and a settable recording bitrate. It routes to the program, so it
reaches the stream and the recording -- both of which now carry live audio on
Windows.

### ASIO

Vendored SDK, driver enumeration, and a real-time output callback with a ring
buffer, so cue audio can reach an interface directly. A device whose sample rate
does not match gets a conversion rather than a refusal.

### The synth sources

**Tone generator** -- the audio counterpart of a test pattern, with diagnostic
displays and explicit visuals on/off.

**Chip voices** -- 2A03 and FDS as one SYNTH section, playable from parameters
rather than presets, reachable from the SOURCE menu, and playable live over MIDI
or the computer keyboard.

**Video synth** -- oscillators, mirrors and feedback, a glitch stack, hardware
palettes, CRT, a text mode with the full 95-glyph ASCII set, and sprite sets
loaded from `data/sprites` with rotation, flip, jitter and chaos per tile. A
sprite sheet or a folder of sprites can be imported through a picker.

### Timer

Custom colours, six chimes, a rest that is actually silent, an event logo, and
message placeholders.

### Elsewhere

- HAP conversion is offered where it would actually pay, with the real numbers.
- Datamosh gains an EXTREME recipe and per-cue encoder overrides.
- First launch always shows the green branded wordmark; splashes rotate per theme.
- Source type labels lost the redundant "Source" suffix.
- All three platforms build and smoke clean.

## 2026-08-20 - v0.84.0 (datamosh, HAP, stage timer, show log)

The largest feature release since the SDL3 migration. Four new things Deckboy
could not do at all, plus the first three items off the competitive survey.

### Datamosh
A per-cue effect that withholds keyframes from the decoder, so P-frames drag the
previous picture along their motion. Toggle it on and the cue prepares itself in
the background; it keeps playing the original until the transcode lands, so
taking the cue mid-prepare is safe and a half-written file can never go to air.

**CLASSIC vs SUBTLE, and why they are named that.** H.264 barely moshes: a
P-frame may legally carry intra-coded macroblocks, so x264 refreshes regions on
its own and the smear heals within a few frames -- fastest on exactly the
high-detail content you would want to mosh. MPEG-4 Part 2 has no such refresh
and gives the real effect. The names describe measured behaviour, not an
aesthetic choice.

### HAP playback
HAP files play, decoded by a vendored container parser and Snappy decompressor
rather than by ffmpeg -- letting ffmpeg decode HAP unpacks DXT to RGB on the
CPU, which is the cost the format exists to avoid. All-intra means seeking is
direct, with no decode-forward from a keyframe.

`tools/make_hap_sample.py` generates HAP test media, because ffmpeg only encodes
HAP when built with libsnappy.

### Stage timer
A new Timer cue: countdown, count-up or time-of-day, with amber/red thresholds,
overtime, a message line, and a progress bar. Digits are seven-segment or
dot-matrix GEOMETRY rather than text, so a stage screen renders identically
wherever it runs regardless of installed fonts.

The clock is deliberately NOT the transport. The cue stays on air while the
operator runs, holds, resets or nudges it -- tying it to transport would mean
pausing the clock took the display off air.

### Show log
Deckboy records what fired and when: takes, blocked takes, stops, reracks,
panics and show opens, with wall clock and running milliseconds — so "what
happened at 20:14?" has an answer after the show. Flushed on every write.

### Scheduled start
A cue can fire at a wall-clock time with no external timecode source, which is
what makes unattended playback possible. Edge-triggered on crossing the time, so
one long frame cannot skip a schedule, and daily schedules re-arm at midnight.

### Cue markers
Named jump marks inside a clip. Jumping seeks rather than takes, so the picture
does not blink, and stepping backwards has slack so it returns to the marker you
just passed rather than the one before it.

### Media encoder
Twenty-two output formats with availability probed from your ffmpeg, per-job
hold and reorder, and a real progress readout. The queue runs one job at a time
by default, so encoding never outbids playback, and the format picker encodes
the format you chose.

### Also
Press Start 2P on the chrome with a readable face for filenames; cue-row icons
sit inside their boxes; four new splash scenes; child processes close with the
parent, so a transcode never outlives the app.

## 2026-08-19 — v0.83.2 (media encoder: queue, presets, progress)

The Encoder tab went from a single button to a real queue.

### A queue, one job at a time
Jobs are enqueued and started at most `encoderConcurrency_` at a time, default
**1** — encoding must never outbid playback for CPU or for the drive the media
is streaming off during a show. CONVERT ALL FLAGGED on a 31-cue show queues 31
and runs 1.

### Real progress, not a spinner
Jobs stream ffmpeg's own `-progress pipe:1` output and parse `out_time_us`
against the probed duration. Progress is indeterminate until ffmpeg first
reports, which it does about once a second.

### Presets
Four, selectable from chips in the tab or by `ENCODEPRESET` over the wire:
**Delivery H.264** (NVENC with libx264 fallback), **Proxy 720p**, **Match
Source**, and **Datamosh** — `-bf 0 -sc_threshold 0 -refs 1 -g 120`, libx264
only. Verified against the source: B-frames 1 -> 0, refs -> 1, GOP `IBBPBBP` ->
`IPPPPPP`, keyframes regular at 2.0 s, and frame/duration parity with a normal
encode (1200 frames / 20.000000 s both) so the moshed copy can be swapped for
the original without shifting sync. Datamosh writes `<stem>_mosh.mp4` beside
the original rather than replacing it. NVENC is excluded from that preset
deliberately: it ignores the reference and scene-cut controls and injects its
own IDR frames.

### Queue control and the busy panel
Pause/resume, cancel-all, and a per-row cancel that stops ffmpeg and removes the
half-written file (a partial encode is not a usable cue). The panel fronts the
startup mascot — reused through a new `overrideTip` parameter rather than a
second mascot — with a bar per job and a whimsy line.

### Children close with the parent
Every spawned process joins a job object with
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, so no transcode outlives the app that
asked for it. Verified by hard-killing Deckboy mid-encode — ffmpeg went from 1
to 0.

### Encoder tab layout
The last part of the settings modal to join the uiScale sweep: cards, rows and
body text all follow the shared helpers and the theme's own ink roles, so the
tab scales with everything else and reads on every colourway. `PRESET` and the
queue whimsy line measure their labels rather than guessing a width.

### Splash pool
Four new scenes (beach, autumn rooftop, winter campfire, Brooklyn waterfront),
desaturated to grayscale masters and gamma-matched to the existing pool. The
cycle tints them per theme, so colour art would multiply to mud.

### Datamosh look: smooth or chunky
A `MOSH: SMOOTH / CHUNKY` toggle picks which recipe the Datamosh preset
prepares. **Smooth** is H.264 — its in-loop deblocking filter tidies block
edges as it decodes, so the smear reads as flowing and liquid. **Chunky** is
MPEG-4 Part 2, which has no deblocking at all, so blocks stay hard-edged: the
look people picture when they say "datamosh". Both verified moshable (no
B-frames, `IPPPPPP`, identical frame counts so either stays swappable with the
original); chunky writes `_mosh.avi`, smooth `_mosh.mp4`.

### Formats you can actually reach
Twenty-two output formats, with availability PROBED from `ffmpeg -encoders`
rather than assumed, so what the picker offers is what this machine can run.
Includes ProRes 422/4444, DNxHR, QuickTime RLE, VP9, AV1, FFV1, MJPEG, GIF, PNG
sequence, WAV/MP3 stems, and HAP/HAP Alpha/HAP Q. Quality args are per-codec
because there is no universal knob. Bulk-encoding 5+ cues to a mastering codec
warns about disk space first.

HAP is present but deliberately not advertised as fast: through the ordinary
decode path it decompresses DXT to RGB on the CPU, which is slower than H.264
for much larger files. See `docs/HAP_PLAYBACK_PLAN.md`.

### Also
`DECISIONS.md` records who decided what. `docs/DATAMOSH_PLAN.md` and
`docs/HAP_PLAYBACK_PLAN.md` specify the two features still to be built.

## 2026-08-16 — v0.83.2 (macOS: portable state, a real CLI, a protocol that answers)

### Read-only assets and writable state are separate

`Paths::dataDir()` is bundled assets — themes, fonts, sounds. The new
`Paths::stateDir()` is everywhere Deckboy writes: it resolves to
`DECKBOY_STATE_DIR`, else the data dir when that is both writable and not inside
a bundle — so a portable Windows or Linux install is byte-for-byte unchanged —
else per-user application data (`%APPDATA%\Deckboy`,
`~/Library/Application Support/Deckboy`, `$XDG_DATA_HOME/deckboy`).

That is what lets a macOS `.app` keep its code signature intact, and lets
Deckboy run from a read-only volume or an install the user cannot write to. An
existing show and last-opened pointer are copied across once, so an upgrade in
place still opens on the show you were working on. The soak log and the
`_converted` media dir live in the state dir too.

### The command line

- **`--help`**, listing every flag.
- **Option flags are read wherever they appear**, so
  `--decode-bench clip.mp4 --no-inproc-decode` and
  `--no-inproc-decode --decode-bench clip.mp4` mean the same thing.
- **`--flag=value` works** as well as `--flag value`.
- **Unknown flags and missing operands are errors** (message + exit 2) rather
  than a silent fall-through into the GUI.
- **A bare path opens that show or imports that media**, which is what makes the
  `.deckboy` file association the Windows installer registers —
  `"Deckboy.exe" "%1"` — do what double-clicking a show should do.

### Remote control answers you

Every command gets a line back — `OK <VERB>`, `ERR unknown command: <VERB>`, or
`ERR <VERB>: <reason>` — and `HELP` lists the protocol, so a script can tell a
command that worked from one it mistyped. The Companion module ignores the acks
and logs the errors.

- **`MASTERVOL` speaks percent**, like `STATE`, the toast, the Companion action,
  the MIDI CC and the OSC senders: `MASTERVOL 60` is 60%. It accepts explicit
  `150%` / `1.5x`, still reads a bare fractional value ≤ 2 as a multiplier so
  old scripts mean what they said, and refuses out-of-range input rather than
  clamping it out of sight.
- **`RERACK` exists**, so all four transport buttons are on the wire. The
  handler's own header now lists exactly what is implemented.

### Steadier teardown

Audio streams are handed back to SDL before their owners are destroyed, the
audio decode thread is serialised against a device swap, and PIP overlay engines
close inside `shutdown()` while the renderer is still up — so quitting takes the
same ordered path every time.


## 2026-08-09 — v0.83.1 (macOS in the field, and proper installers for all three)

Deckboy's first run on a Mac, and its first release with a real installer for
every platform.

### macOS
- **Native file dialogs.** Import, open, save and relink all go through SDL3's
  in-process pickers (`SDL_ShowOpenFileDialog` and friends) — one native code
  path on every platform, no subprocess, no thread fragility. The callback is
  handled thread-safely and the filter lifetime is documented.
- **iPhone photos import.** `.heic` and `.heif` load as stills, reconstructed
  through the filtergraph HEIF actually needs. Verified on a real
  camera-roll file.
- **A still that cannot be decoded says so**, and says whether it failed or was
  simply superseded by the next cue.
- **The startup splash sizes from measured text**, so its buttons read in full in
  the wider macOS and Linux fonts.
- **A real app icon**, built from the master art.

### Cross-platform parity
- **libltc is bundled** in the macOS and Linux portable builds, so LTC timecode
  works out of the box rather than only where libltc happened to be installed.
  The loader looks beside the executable first.
- **Controls audit:** OSC, Companion, HyperDeck, Art-Net and TSL tally work
  identically on all three platforms. Verified.
- Bundled `ffmpeg` is reachable off Windows, and a POSIX crash handler writes a
  symbolised `deckboy-crash.log` on Linux and macOS.

### Proper installers (new)
- **macOS `.dmg`** — drag-to-Applications; installing out of Downloads also
  sidesteps App Translocation. CI mounts it and checks its contents.
- **Windows Inno Setup `.exe`** — Start Menu, optional desktop shortcut, opt-in
  `.deckboy` association, real uninstaller. Verified install → run → uninstall.
- **Linux AppImage** — one self-contained file, runs on any current distro.
  Verified running with a cleared environment (`ffmpeg` and `libltc` both found).

Full build instructions for every format: `docs/PACKAGING.md`.

### Limits
- macOS notarisation and Windows code signing need accounts we do not have yet:
  a downloaded macOS build wants a one-time
  `xattr -dr com.apple.quarantine`, and Windows shows an "unknown publisher"
  prompt on first run.
- HEIC needs ffmpeg 7.1 or newer for its HEIF demuxer, so it decodes on macOS
  but not on a Linux build against Ubuntu 24.04's ffmpeg 6.1.

## 2026-08-08 — v0.83.0 (portable macOS bundle)

`tools/package_macos.sh` builds `dist/Deckboy-<VERSION>-macos-<arch>.zip`, the
counterpart to the Windows portable zip: a double-clickable `Deckboy.app` that
carries everything it needs.

Homebrew installs SDL3 and FFmpeg under `/opt/homebrew`, so a bundle that merely
links against those paths would run on the build machine and nowhere else. Every
non-system dylib is copied into `Contents/Frameworks` and every reference
rewritten to `@rpath` — recursively, because dependencies have dependencies.

Two things that break naive macOS bundles, handled explicitly:

- **`install_name_tool` invalidates a code signature**, and on Apple Silicon the
  kernel refuses to run a binary whose signature is broken. Everything is
  re-signed ad-hoc *after* rewriting, inside-out.
- **`Contents/MacOS` is a code-only directory**, so `data/` goes in
  `Contents/Resources` and `Paths::resolveProjectRoot()` has a small `__APPLE__`
  branch that recognises the `…/Contents/MacOS/<exe>` structure. Detection is
  structural rather than a `.app` suffix test, because whoever downloads the
  bundle may rename it. A plain build-tree run on macOS is unaffected.

The `Info.plist` carries `NSCameraUsageDescription` /
`NSMicrophoneUsageDescription` / `NSLocalNetworkUsageDescription`. These are
load-bearing rather than boilerplate: macOS terminates a process that touches
the camera or microphone with no matching usage string, so these are what make
"add a camera cue" work.

Per-machine state (`last_project.txt`, `default.deckboy`) is stripped, same as
the Windows packager.

A new `macos-package` CI job runs the packager on a real Mac and then checks the
result rather than assuming it: no `/opt/homebrew` or `/usr/local` reference
survives in any binary, `codesign --verify --deep --strict` passes, the app runs
`--self-check` and `--smoke` **from inside the bundle** (exercising the rewritten
`@rpath` and the bundled `data/`), and no build-machine state shipped.

Limits: no Developer ID signing or notarisation yet (a downloaded zip needs
`xattr -dr com.apple.quarantine`, documented in the bundle's README), and no
universal binary. Browser cues, Spout and d3d11va zero-copy decode are Windows
features by construction.

## 2026-08-08 — v0.83.0 (NMOS IS-04/IS-05: the 2110 senders become discoverable)

### What this adds
An ST 2110 flow is undiscoverable on its own, and copying an SDP out of a
settings modal by hand is not how facilities work: a node registers itself with
a Registration & Discovery System, and a broadcast controller connects it
through IS-05. This is the difference between "emits valid packets" and "shows
up in the plant".

New `native/platform/nmos_node.{hpp,cpp}`:

- **IS-04 v1.3 Node API** over HTTP — node, devices, sources, flows, senders.
  Receivers are advertised as an empty list, because Deckboy is a source device
  and an honest empty list is what stops a controller offering to route into it.
- **IS-04 registration** — POSTs the resource tree in dependency order
  (node → device → source → flow → sender; a registry rejects a flow whose
  source it has not seen), then heartbeats every 5 s against the registry's 12 s
  default health timeout. A 404 on heartbeat means the registry restarted and
  forgot us, which triggers automatic re-registration. A missing registry backs
  off 1s→30s instead of hammering the network.
- **IS-05 v1.1 Connection API** — constraints / staged / active / transportfile
  per sender, with a real staged scratch state so a controller's
  stage-then-activate workflow behaves as the spec describes. The transportfile
  is the *same* SDP the settings modal shows: `st2110ConfigForOutput()` is the
  single source of truth, so a receiver can never be handed two different
  descriptions of one flow.
- **IS-05 PATCH really reconfigures the sender.** `master_enable` and
  `transport_params` with `activate_immediate` are applied to the actual
  `OutputTarget` — moving the multicast group over IS-05 moves the stream, and
  the paired audio leg follows the video base port by the +2 convention. The
  handler blocks until the main thread has genuinely applied the change; telling
  a controller a route moved before it had is the exact lie that makes a plant
  untrustworthy.
- **Resource ids are UUIDv5** derived from a fixed Deckboy namespace and a
  stable per-output seed, so a controller's saved route survives an app restart
  with nothing persisted to disk. Verified byte-identical against Python's
  `uuid.uuid5` on three seeds.

Settings: Video Outputs → Devices, directly under ST 2110 (it is meaningless
without it). Remote command `NMOS ON|OFF|STATUS|REGISTRY|PORT|NIC`, mirroring
the existing `ST2110` command. Actions 710–714; next free 715+.

### Verified — AMWA official conformance suite
Run against the real app with **AMWA's own NMOS Testing Tool** (`IS-05-01`,
Connection Management API): **36 pass, 2 fail, 23 not applicable**. The 23 are
all receiver tests — Deckboy is a source device and advertises an empty receiver
list. The 2 failures are the deliberate scheduled-activation refusal below.

### Verified — reference registry interop
`nmos-cpp` (the AMWA reference implementation) run on a separate Linux machine.
Deckboy registered its full resource tree; the registry held 1 device, 2 sources,
2 flows, 2 senders, 0 receivers. That machine then fetched the SDP over the LAN
via the advertised `manifest_href` and issued an IS-05 PATCH moving the group to
239.77.7.7:22000, which the served SDP reflected. The registry's own
`registration_expiry_interval` is 12 s, the timeout our 5 s heartbeat was sized
against.

With the network set to LOCAL ONLY (the default) Deckboy **withholds
registration** and says why, in the settings status line and in a toast when
armed: publishing a LAN address while listening only on loopback would put a
sender in the plant that nothing can reach.

### The SDP names the interface the stream actually leaves by
The origin line resolves the local address by asking the routing table which
interface reaches **the stream's own destination group**, rather than probing a
generic internet address and getting the default route — which on a machine
running a VPN is the tunnel. Verified on a host with ProtonVPN up: origin
resolves to the LAN NIC, not the tunnel and not loopback. Memoised per
destination with a 30 s refresh, because SDPs are rebuilt every time the NMOS
sender snapshot is taken.

### Limits — do not claim it
- **No mDNS / DNS-SD.** The registry is configured by URL. Deckboy cannot find a
  registry on its own, and cannot be found in peer-to-peer mode. This is the
  single biggest remaining gap.
- **No scheduled activation.** `activate_scheduled_absolute` / `_relative`
  return 501; honouring them needs the PTP clock to gate the switch, and taking
  a source at the wrong instant is worse than refusing.
- No IS-05 bulk staging (501), no receivers, no IS-07/08, no HTTPS, no IS-10 auth.
- The underlying 2110 caveats stand: not PTP-locked, not narrow-model paced.
- **`IS-04-01` (Node API suite) has not been run**, only `IS-05-01`. It needs
  the tool's own registry and DNS-SD, which we do not support.

## 2026-08-07 — v0.82.1 (patterns hold a flat footprint)

### Pocket-test runs like every other pattern
The static layer of the pocket-test card is cached per raster in a small bounded
set, so the programme output and the cue-preview runtime hit the cache rather
than evicting each other. Animated patterns also build **in place** into the
frame the engine already holds, and that frame's GPU zero-copy state is reset
explicitly, so a pattern following a hardware-decoded video cue draws itself and
not a stale decoder surface.

Measured, with controls, at 3840x2160:

| | private commit | swing |
|---|---|---|
| idle | 244 MB | 1 MB |
| video playing | 265 MB | 1 MB |
| smpte-bars (static) | 244 MB | 1 MB |
| test-bars (animated) | 242 MB | 1 MB |
| pocket-test | 245 → 246 MB | 1 MB |

Live output frame rate with pocket-test at 4K is a steady 60.0. Rendering is
byte-identical.

### Deleting a cue from the right-click menu
Deleting the **live** cue from a context menu takes one click: the item reads
"delete LIVE cue" in a hotter red, because choosing a named item from a menu is
already deliberate. The keyboard Delete path still asks for confirmation, where
a repeated keypress is natural.

## 2026-08-07 — v0.82.0 (SMPTE ST 2110 output, PTP, dual streaming, safety UI)

### SMPTE ST 2110
- **ST 2110-20 uncompressed video output.** Correct pgroup packing (YCbCr-4:2:2
  10-bit = 2px/5 octets, and 8-bit), RTP + payload headers with Sample Row Data
  descriptors, marker-bit framing, BT.709 studio-swing conversion, multicast with
  TTL and NIC pinning, and SDP generation with an exact rational frame rate.
  Verified against ffmpeg: decodes as `yuv422p10le` / `uyvy422`, correct picture.
- **ST 2110-21 pacing.** Packets are spread across the frame interval on a
  dedicated sender thread, at a rate receivers accept, and the render loop is
  never held to pace a stream.
- **ST 2110-30 (AES67) audio.** 48 kHz, 1 ms packets, L24, fed from the engine's
  existing audio tap so the stream carries exactly what the PA hears. Verified:
  received as `pcm_s24be, 48000 Hz, stereo`.
- **PTP (IEEE 1588 / ST 2059) slave.** Follows the grandmaster on a configurable
  domain and disciplines the RTP media clock. **The SDP only advertises
  `ts-refclk:ptp` when genuinely locked** — never optimistically.
- Honest limits, stated in the UI and the code: software timestamping, wide-model
  pacing, no NMOS discovery. See `docs/ST2110_FEASIBILITY.md`.

### Streaming
- **SRT and RTMP are independent destinations that can run at the same time**,
  each with its own complete configuration.
- **SRT has its own controls**: caller/listener mode, latency, passphrase and
  stream ID, merged into the URL (anything typed by hand still wins).
- RTMPS is carried as RTMPS, and the GOP length follows the encoder settings.

### Timecode
- **LTC generator.** `--ltc-generate` produces LTC that round-trips through
  Deckboy's own decoder at 24/25/30 fps, including midnight rollover — so
  Deckboy can generate timecode as well as chase it. Not yet wired to a live
  audio output.

### Safety and clarity
- **BLACKOUT has a button** (and `B`), beside CLEAR — the fastest and most
  reversible way to kill the picture, and now the one nearest to hand.
- **Backspace deletes; `U` clears overlays.** One key, one meaning.
- **Escape is a three-stage escalation** — control window, then clear output,
  then quit.
- **The shortcuts overlay matches the app**, with ten missing bindings added.
- Animated on-air indicators per stream type in the OUTPUT group.

### Audio
- **Loudness normalize reaches its target.** Peaks are handled by a new
  look-ahead peak limiter in the deck audio path, so a −26.8 LUFS cartoon that
  wants +10.8 dB gets +10.8 dB.
- **The waveform is drawn on a dB scale with headroom**, so gain changes are
  visible where a linear scale pinned everything above about −6 dBFS.
- Waveform analysis covers the first 5 minutes of a file.

### Terrarium
- Re-vendored from upstream (`4931aa0`) with recorded provenance. A new
  `terrarium-pico` pattern renders the Raspberry Pi panel's 1px-per-cell
  picture.
- `--pattern-dump` honours its size argument for every pattern.

### Also
- Settings opens whatever the inspector is doing: the inspector's dropdowns are
  gated to their viewport, so a scrolled-away row keeps clear of the SETTINGS
  button.
- **Edge feathering works with and without perspective warp**, as a true edge
  ramp rather than a fade across the picture.
- Packaging ships no machine state from the build box.
- Kerning is disabled on UI fonts, so words stay whole ("Target URL").
- A crash logger writes a symbolised stack to `data/deckboy-crash.log`,
  including from file-dialog threads.
- Refresh-rate, bit-depth and raster-mode controls are back in the UI, and
  ~1,646 lines of superseded cue-inspector code and 21 unreachable handlers are
  gone.


## 2026-07-31 — v0.81.4 (MIDI input on every platform)

### MIDI
- **MIDI input works on Windows and macOS**, through the cross-platform RtMidi
  wrapper and into the same command queue Linux uses — so a note or a CC does
  exactly what it does on ALSA, and every documented note/CC mapping is live on
  all three platforms.
- **New MIDI remote command** (`MIDI ON|OFF|TOGGLE`), so a Companion surface can
  arm MIDI rather than requiring a trip to the Audio settings tab.
- `--self-check` reports `midi-runtime` with the RtMidi port count.
- MTC quarter-frame and MMC/MSC sysex are ALSA-only, since the wrapper surfaces
  channel-voice messages only. The catalog says so by name.

### Diagnostics
- `--self-check` reports the NMC bridge for real on every platform: it is
  cross-platform UDP, and on Windows it binds its port and reports `nmc[on,ok]`.

## 2026-07-30 — v0.81.3 (OSC Query on Windows, the full DeckLink card)

### Remote control
- **OSC Query runs on Windows**, on the same cross-platform socket code
  Companion control already uses. Verified live — `OSCQUERY ON` binds TCP 5511
  and an HTTP GET returns the Deckboy OSC Query page, with the mirrored
  `/deckboy/state` feedback.

### Video Outputs
- **Every DeckLink control is on screen.** The NDI, DeckLink, Edge Blend and AOI
  sections size from the shared header + row metrics, so DeckLink's **10-BIT**
  toggle and NDI KEY sit fully inside their panels at any font size.

## 2026-07-30 — v0.81.2 (genuinely dark themes)

### Themes
- **Deckboy can have genuinely dark themes.** The structural chrome — bottom-bar
  groups, playlist body, deck list, timeline lanes — draws from the
  `screen_tile` / `screen_fg` pair added in v0.79.3 rather than from the bright
  ink role. Both roles fall back to exactly what was there before
  (`tile` → `screen_light`, `fg` → `screen_deep`), so a theme that does not
  define them renders identically — `gameboy` is untouched — while the 15 that
  do become properly dark.
- The library now spans three looks: bright moulded-case, dark tinted chassis,
  and true-black OLED terminal.

### Audio
- **Both waveform views draw through `drawColumn`**, so the mono view carries the
  over-scale clip tint and keeps its bars inside their lane at any gain trim.

### Build
- CI is green on all three platforms: `find_package(PkgConfig)` runs
  unconditionally, so `-DENABLE_MIDI=ON` configures against a CONFIG-package
  SDL3, and the display-topology code builds against the stable SDL 3.2.x
  release as well as 3.4.

## 2026-07-30 — v0.81.1 (theme variety, honest LTC reporting, README)

### Themes
- **The library looks varied.** The 25 themes that shared one recipe now span
  three families: **moulded case** (muted plastic shell with a saturated LCD,
  the structure the default Game Boy theme uses), **tinted chassis** (a
  genuinely coloured dark body with bright ink, for booth use), and **true-black
  OLED terminals** (kept for `dark`, `virtual-boy`, `famicom`,
  `captain-falcon`, `game-and-watch`). `gameboy`, `pocket`, `sp`, `advance` and
  `color` were already distinct and are untouched.
- Case colours are deliberately low-chroma. `shell_inner` is the dominant panel
  fill across the whole UI, so a vivid case floods the interface — the
  character's hue belongs to the screen, which is exactly how the original
  Game Boy theme is built.
- All 30 pass `tools/audit_theme_contrast.ps1`.

### Timecode
- **`--self-check` reports LTC for real on every platform** — `ltc-runtime: ok`
  with the library present, `missing (...)` without it. `LtcApi` loads `ltc.dll`
  dynamically and the portable zip ships it, so Windows has LTC and now says so.

### Build
- **DeckLink builds with or without it enabled.** `decklink.cpp` compiles
  always, carrying its complete stub behind `!DECKBOY_HAS_DECKLINK`, so an
  `ENABLE_DECKLINK=OFF` build links.
- **CI is repaired and runs on every push and PR**, on the branch that exists,
  with SDL3 installed on all three platforms, one configure step per platform,
  smoke tests beside `--self-check`, and the Companion module's suite as its own
  job.

### Docs
- **README overhauled** to describe what exists: the portable zip leads the run
  instructions, Companion leads with the module, the theme count is one number,
  and shipped features are listed as shipped.

## 2026-07-29 — v0.81.0 (preview locked to output, display hot-plug, Companion module)

### Program monitor
- **The preview is locked to the program output.** The control window samples
  the output's finished composite on the output's own render pass, scaled down
  to preview size first (~0.5 MB read back instead of 3–12 MB), every presented
  frame — so the monitor cannot drift from what leaves the machine, and it runs
  at full rate rather than the ~10 fps a full-resolution hardware-frame download
  allowed. Verified by capturing both windows in both orders against a burned-in
  frame counter.
- The tap is taken *before* warp/AOI/edge blend, so the warp editor still draws
  its handles over an unwarped image. When no window output is armed the
  decoder-frame path still runs, unchanged.

### Displays
- **Deckboy notices displays connected or disconnected while it is running.**
  The topology scan compares a per-display fingerprint (name + desktop
  placement) rather than the display count, so it also catches a monitor swapped
  for another one, a rearranged desktop, and resolution changes. All SDL display
  events are handled and debounced, since Windows emits a burst of them and
  reports half-built topology partway through.
- On a hot-plug an affected output is re-homed **even while fullscreen**, which
  is what lets a monitor connected mid-show pick up its output. Unaffected
  outputs are left alone.
- **Unplugging a projector leaves your control screen alone.** An output whose
  pinned display disappears parks windowed and hidden with health
  `display missing: <name>`, and restores itself when the panel comes back.
- Toasts name what happened — "display connected: DELL U2720Q (2 total) —
  1 output re-homed" — and RESCAN forces a re-home even when nothing changed.

### Interface
- **One text-placement contract across the whole app.** Panels paint the rect
  they are given and all three label helpers centre on it, so neighbouring
  controls line up whichever helper drew them. Label padding is continuous with
  width, and every centring helper ellipsizes and clips, so a long label
  truncates inside its pill.
- Settings cards and Video Outputs sections share one header-plate contract, so
  a section title sits at the same height on every tab, and the CONNECTED
  DISPLAYS **IDENTIFY** button sits inside its header plate.

### About
- **Rewritten as a real credits page**: masthead with wordmark, version and
  build date; a PROJECT column (copyright, GPL-3.0-or-later and what that grants,
  source, warranty disclaimer, live session ports and theme); and a BUILT WITH
  column (platform, SDL3/FreeType, FFmpeg, the optional SDKs *this* binary was
  actually compiled against, timecode, font licence, key reference). Both
  columns share one label gutter.

### Patterns
- **New "Test Clock (sync + latency)" pattern** — colour bars, an aspect-truth
  circle (reads as an egg the moment a stretch mode is wrong), a scrolling hue
  band for sub-second phase, and a large seconds + frame counter with exact
  timecode in the corner. Point two displays at it and photograph them to see
  whether they agree. Aliases: `testsrc1`, `sync-card`, `latency-clock`.

### Audio
- **Loudness normalize reaches target on quiet material.** The trim range is
  −40..+40 dB, read from one shared constant by the setter, the normalizer, the
  waveform scaler, the engine mirror, project load and the remote command — so a
  −34 LUFS transfer that wants +17.8 dB gets it.
- **Normalize is peak-aware.** The same analysis pass measures true peak
  (`ebur128=peak=true`) and holds the boost so peaks stay under −1 dBFS. When
  the ceiling is what limited the result the toast says so — `normalized: -0.9
  dB (was -25.7 LUFS) - peak-limited at -0.1 dBFS`.
- **Gain changes are visible on loud material.** Over-scale columns draw in the
  theme's danger colour, so pushing past the ceiling reads as hot and backing
  off cools it — and doubles as a clip warning.
- Toasts size to their message, and normalize results hold for 2.6 s — 3.2 s
  when peak-limited.

### Remote control
- **A real Bitfocus Companion module** (`companion-module-deckboy/`) replaces the
  Generic TCP/UDP recipe. It polls `STATUS`, so buttons carry **cue tally**,
  transport colour, output health, a connection watchdog and a derived
  countdown, where a Generic connection could only push commands one way. Ships
  actions (transport, cue select/take/goto, seek, levels, output, find, plus a
  raw-command escape hatch), nine feedbacks, ~70 variables and wired presets.
  Parser tests run against a captured Deckboy status reply.
- The module names the localhost-only default in its config screen and in its
  connection error, since that is the usual reason a remote Companion sees
  nothing.

## 2026-07-22 — v0.80.2 (waveforms show gain, normalize feedback, SKIP button)

### Transport
- **New `>|` / `<|` skip buttons** in the timeline transport strip
  (`<| |< << ▶ >> >|`), hotkeys **`.`** / **`,`**, remote commands
  **`SKIP`** / **`SKIPBACK`**. `>|` takes the cue the deck would naturally
  play next — honouring goto targets, shuffle, playlist loop, and the
  missing-media walk — without waiting for the current cue to end
  (end-of-cue auto-advance and skip share one resolver, so they can never
  disagree about what "next" means). `<|` takes the previous playable cue
  (deliberately ignores goto/shuffle — back means the cue above).

### Audio
- **Waveforms grow and shrink with gain.** Every waveform view — inspector
  thumbnail, video-cue audio strip, timeline audio lane, active-cue mini view,
  output-monitor overlay — scales its drawn amplitude by the cue's gain trim,
  live, so nudging gain or landing an R128 normalize visibly changes the
  transients and you can *see* what normalize did.
- **Normalize always answers.** A selection with no file-backed audio toasts
  "normalize: selection has no file-backed audio".

## 2026-07-18 — v0.80.1 (fast on slow drives, a DMG sound pack, vivid Terrarium)

### Performance
- **The UI stays smooth when show media lives on a slow drive.** Cue path
  resolution is memoised per (path, project file), so after the first touch the
  per-frame cost is a map lookup rather than a disk stat per path component —
  which is what large playlists (1,400+ cues) on USB and exFAT drives feel most.
- **The splash appears immediately.** The media-presence scan behind the RELINK
  badge runs on a background thread at boot and on project open, with the
  "N media missing (RELINK)" toast following when it lands.

### Terrarium
- **Vibrant, nature-evoking colours** (exe and in-app pattern — they share the
  core). **Foliage lives in the green band** (spring-to-forest hues, varied
  saturation and value; sage and olive for desert; alien keeps the free wheel),
  **blooms draw from a real meadow distribution** (mostly yellows and whites,
  then violets, reds and oranges, blues uncommon, magenta rare) and render
  accent-dominant so they pop, and **fauna wears earth tones** (russet, tan,
  chestnut) with plumage accents from bird and beetle iridescence — plus a
  1-in-8 full-colour tropical/wetland showoff. Everything runs through a
  slightly stronger `vividify`.
- The big-flower glyph is a round rosette with a stem. Mushrooms wear
  forest-floor caps (cream, tan, fly-agaric red), and ambient flower glyphs take
  poppy, marigold, orchid and thistle tones.

### Game Boy sound pack
- **New DMG-style synth voice** behind the UI sounds: two pulse channels
  locked to the hardware duty cycles, a 15-bit LFSR noise channel, 4-bit
  quantized envelopes, and NR51-style stereo placement.
- **Boot jingle** — a swung chiptune over Coltrane changes (B△7 → D7 →
  G△7 → B♭7 → E♭△7, the Giant Steps major-third cycle) with an original
  melody: YMCK-school cute jazz, golden-changes edition. Plays over the
  startup splash, honours the "little bloops" toggle; ordinary bloops hold
  off until the final chord rings out.
- **New sound effects**: refused-action buzzer (take blocked on missing
  media), a panic dive-and-whoosh for PANIC/ESC, and a dice-roll trill when
  shuffle turns on. Existing bloops are unchanged.

### Cue inspector
- **PIP cues are editable in the live inspector**: source/type editor, corner
  presets (TL/TR/BL/BR), size presets (SM/BIG/70-30), CLEAR OVERLAY, and the
  GEOMETRY/KEY/METADATA sections.
- **Stream cues get an inspector.** SRT/RTMP/RTSP/UDP stream cues and NDI source
  cues have PLAYBACK (fades, per-cue transition + style, audio toggle) and
  METADATA (editable URL / NDI source name, tag, notes, cue id).
- **Audio cues get the AUDIO section**: gain trim, pan, mono downmix, output
  pair, independent a-fades and R128 normalize, the same as a video cue.
- The scroll extent tracks section bottoms, so a section ending in a status line
  is fully reachable; dropdown hit zones are cleared when the selection changes;
  and the empty inspector says "NO CUE SELECTED" once.


## 2026-07-15 — v0.80.0 (settings overhaul, stereo waveforms, panic audio, Test Bars)

### Settings menu
- **One steady dialog size** — every tab shares one envelope sized for the
  busiest tab, so the modal holds still as you move between them.
- **Area of Interest is a resolution, not four percentages** — the AOI card
  edits `X / Y / WIDTH / HEIGHT` in pixels of the output raster (e.g.
  `1920x1080 @ 960,540`), with `-`/`+` nudges (16 px), click-to-type exact
  values, and a `FULL` reset. The card header shows the live rect. Storage is
  still the four edge fractions, so show files are unchanged both ways.
- **Edge blending reads in pixels** too, of the output raster.
- **Tabs size to their labels** — "Video Outputs" reads in full.
- **Display sub-tab tidied** — Toggle Fullscreen and Orientation share one
  row, and the Display & Raster card uses the space it reserves.
- The About tab prints one `v`.

### Timeline / waveforms
- **Stereo audio shows both channels.** The waveform analysis measures true
  stereo-ness sample by sample (side-signal energy) and the L/R split follows
  the *content*: really-stereo material always splits, even for cues saved by
  older versions whose metadata lacks a channel count, while mono material in a
  stereo container gets one full-height lane instead of two identical twins.
  Older cues have their channel count backfilled by a background re-probe on
  project open.

### Playback safety
- **Panic / triple-Esc silences audio.** "Outputs off" stops every deck engine
  and browser cue as well as disarming the video outputs.
- **STOP works on Audio cues**, closing the decode pipe and the stream the same
  way it does for video.

### New
- **Test Bars pattern** — a testsrc2-style motion-diagnostics pattern: six
  saturated bars, a bouncing rainbow diagonal, a dissolving checker patch, a
  sliding grey reference block, and a running clock + frame counter. In the
  pattern picker as "Test Bars (motion diagnostics)"; `pattern://test-bars`
  (aliases: testsrc, testsrc2).
- **Boot console variety** — the splash deals a random hand of 8 lines from a
  40-line pool of sci-fi subsystems each boot, woven between the real init
  values, which always print.
- **`>LIVE` reads in full** — the playlist-header jump button sizes to its
  label, and the marquee dots stop short of it.
- **Right-click a cue → "show in explorer"** — file-backed cues (video, audio,
  image) get a context-menu entry that opens the OS file manager with the media
  file selected (Explorer `/select` on Windows, Finder reveal on macOS,
  containing folder on Linux).
- **Dev flags** — `--import <file>` imports media at launch, skipping the
  startup menu and splash; `--settings [tab[.subtab]]` opens the settings modal
  at boot. Both are for scripted testing and screenshots.

## 2026-07-12 — v0.79.13 (mascot tip: the trim keys)

- The mascot's trim tip names the keys it means: `Ctrl+I` sets the in point and
  `Ctrl+O` the out point; bare `I` is import.

## 2026-07-12 — v0.79.12 (telecined and variable-rate video stay in sync)

- **Audio and video stay locked on telecined (3:2-pulldown) and variable-rate
  material** — classic DVD MPEG-2 anime, some phone clips. Frames are indexed by
  their actual presentation timestamp, so 23.976 fps film soft-pulldowned into a
  29.97 container lands on the right point of the timeline and stays with the
  audio clock. Constant-frame-rate content is unaffected (the indices are
  identical), and a stream with no timestamps falls back to the sequential
  counter.

## 2026-07-12 — v0.79.11 (cue navigation: jump to current + auto-follow)

- **Jump to the current cue** — press `J` or the new `>LIVE` button in the
  playlist header to snap a long playlist back to the cue that is playing (or
  the selection if nothing is live) and centre it. For shows with hundreds of
  cues, this is "where's the show right now?" in one key.
- **Auto-follow** — when the focused deck's live cue changes (take,
  auto-advance, and especially **shuffle**, where the next cue is
  unpredictable), the playlist reveals it — but only if it had scrolled
  off-screen, so the list never yanks while you are looking right at it.

## 2026-07-12 — v0.79.10 (10-bit H.265, and a clean slate on open)

- **10-bit H.265/HEVC (Main 10) renders correctly.** The decoder reads each
  hardware surface's own software format and zero-copies true NV12; P010 and
  anything else takes a CPU transfer and swscale to NV12/RGBA. 8-bit content
  keeps the fast zero-copy path, and `--decode-bench` reports per-frame gpu/cpu
  counts so the split is visible.
- **Opening a show lands on a neutral "nothing live" state**, like a fresh
  launch: the timeline and the preview agree, the operator explicitly takes the
  first cue, and the startup mascot shows. The selected cue is preserved for
  prepping.

## 2026-07-12 — v0.79.9 (mascot tips: bigger, plainer, clearer)

- Mascot tips are **bigger plain text**, drawn full-width under the face rather
  than inside a speech box, in short jargon-free lines.

## 2026-07-12 — v0.79.8 (mascot: per-element Balatro-style drift)

- **The startup face has individual-element life.** On top of the overall hover,
  the whole face gets a small oscillating tilt/rock — placed through a rotation
  about the face centre, so the eyes swing one way as the mouth swings the other
  — and each element, each eye and the mouth, also drifts and breathes on its
  own phase, so they float slightly out of sync. Evokes the springy,
  semi-independent motion of Balatro's card animations, kept subtle.

## 2026-07-12 — v0.79.7 (startup mascot in the empty program monitor)

- **A hovering "terminal face friend" + rotating tips** fill the empty program
  monitor at the start of a session, until the first clip is loaded into it.
  BMO-style: glowing theme-tinted eyes and mouth drawn straight onto the dark
  screen, so the monitor itself is the face. Everything animates smoothly and
  continuously — a 2D floaty hover, eased squish-blinks, a slow look-around
  drift, a smile that breathes, and twinkling stars slowly orbiting the face —
  with no discrete state snaps. A tip line beneath cycles operator hints
  (import, timeline grip, themes, relink, per-cue audio, shortcuts). It retires
  the moment a clip loads and stays gone for the run.
- While the face is up the empty-monitor backdrop is darkened, which also reads
  better on OLED themes than the bright idle fill.

## 2026-07-12 — v0.79.6 (denser timeline filmstrip; consistent thumbnail aspect)

- **9 stills per clip** on the timeline filmstrip (was 5), so it reads as a
  continuous strip and samples the clip densely enough for the now-enlargeable
  timeline lane.
- **Consistent thumbnail aspect.** Each tile is drawn into its own column with
  an aspect-preserving centre-crop, so thumbnails stay undistorted at any lane
  size.

## 2026-07-12 — v0.79.5 (settings on terminal themes)

- **The settings modal reads on terminal themes.** The content frame uses the
  tile fill — a dark frame on OLED themes, unchanged on light ones — and the one
  bare card-body label ("Mappings") routes through `screen_fg`. The rest of the
  modal was already terminal-safe: card titles render bright on dark title
  plates, hints use the audited `screen_ink_soft`, and control buttons sit on
  bright/`mid` fills with dark text.

## 2026-07-12 — v0.79.4 (resizable timeline; dialog readability; sharper thumbnails)

- **Resizable program monitor ↔ timeline split.** A draggable grip in the gap
  under the program monitor lets the operator take height from the preview to
  enlarge the timeline lanes, and give it back. The monitor never shrinks below
  a usable minimum, and the grip appears only when there is room to move.
  Runtime-only, like the existing pane splitters.
- **Sharper timeline filmstrip.** Lane thumbnails are 2x resolution (256×144)
  and rendered with linear filtering, so they stay crisp when the lane is
  enlarged.
- **Terminal-theme dialog readability.** The startup boot splash, the
  startup-mode menu ("New show / Open previous / Open saved"), the keyboard
  shortcuts overlay and the Settings title draw through the on-body ink roles
  (`screen_fg` / `screen_fg_soft` / `screen_ink_soft`), and the boot console
  uses the tile fill — bright text on dark panels in OLED themes, unchanged on
  light ones. The themes were already right; the dialogs simply had not been
  routed through the new roles yet.

## 2026-07-12 — v0.79.3 (terminal/OLED themes; manual rewrite)

- **Terminal / OLED themes.** New theme roles let dark themes render as a
  true-black terminal — OLED-black backgrounds and tiles with bright
  phosphor text and per-theme accents. `screen_fg` (primary on-body ink),
  `screen_fg_soft` (secondary on-tile ink), and `screen_tile` (interactive
  tile fill) each fall back to an existing role (`screen_deep`,
  `screen_dark`, `screen_light`), so **every existing theme is byte-for-
  byte unchanged** and only themes that set the new keys invert.
  - All 25 dark-cased themes reworked as OLED terminals with hue-matched
    accents (green `dark`, red `virtual-boy`/`famicom`/`mario`, amber
    `metroid`, teal `n64`/`wave-race`, violet `gamecube`/`super-famicom`,
    blue `star-fox`/`switch-neon`/`captain-falcon`, magenta `ganon`, pink
    `kirby`/`peach`, gold `zelda`, yellow `pikachu`, lime `piranha-plant`,
    cyan `dolphin`, ice `ice-climber`, white `r-o-b`, grey-green
    `game-and-watch`, and more). Light-cased themes (gameboy, pocket, sp,
    advance, color) are untouched.
  - On-body text sites (inspector row labels, playlist/timeline/header/
    footer chrome) and interactive tiles (cue rows, toolbar and quick-row
    buttons, section headers) draw through the new roles.
  - The inspector body fills with `shell_inner`, so its labels always have a
    legible fill.
  - `tools/audit_theme_contrast.ps1` covers the new roles; all themes pass.
- **MANUAL.md rewritten** from scratch for the current app (SDL3, Windows-
  first, in-process decode) covering cue types, the inspector, outputs and
  geometry, per-cue + multichannel audio, missing-media relink, timecode,
  themes, remote control, soak testing, and the full keyboard reference.

## 2026-07-11 — v0.79.2 (soak harness; shuffle seeding; inspector readability)

- **`--soak [minutes]` long-run stability harness.** Loops the loaded show (or
  synthesized patterns if none) through the real app loop, logging RSS /
  decode-stall / missing-media counters once a minute to stdout and
  `deckboy-soak.log`, then quits. Never persists the looped state into the show
  file. For 24 h+ runs on show hardware. Default 24 h; e.g.
  `Deckboy.exe --soak 720` for a 12 h run.
- **Shuffle deals a different order every launch**, from a `std::mt19937` seeded
  by `std::random_device` with a proper uniform distribution.
- **Cue inspector text is readable on every theme.** The inspector body fills
  with `shell_inner`, the fill the palette contract already assumes for dark
  ink and the contrast audit verifies. Theme colours are unchanged; each console
  keeps its case.

## 2026-07-11 — v0.79.1 (mid-show media loss hardening)

- **A vanished file cannot take a deck down mid-show.** Taking a cue whose media
  is gone (drive pulled, share dropped) is refused with a MEDIA MISSING toast
  before the engine sees it — the output keeps whatever it was showing.
  Auto-advance skips missing cues, with a toast per skip, bounded so an
  all-missing looped playlist cannot spin.
- The take-time check is fresh from disk and updates the MISSING badges and the
  toolbar RELINK count in both directions — a re-mounted drive clears the
  warning on the next take, with no project reload.
- A decode stall whose file turns out to be gone reports "MEDIA LOST (RELINK
  when restored)".

## 2026-07-11 — v0.79.0 (multichannel audio output routing)

- **Cues can route to any output pair of a multichannel interface.** Settings →
  AUDIO OUTPUT has an "Outs" control (2/4/6/8 channels, per deck) that reopens
  the deck's device with that many channels. When more than 2 are open, the cue
  inspector's AUDIO section grows an **outs** row: route each cue's (post
  gain/pan/mono) stereo onto outs 1-2, 3-4, 5-6, or 7-8 — VT to the PA on 1-2,
  click to monitors on 3-4.
- The engine pipeline stays stereo end to end (gain, fades, delay line, VU tap);
  expansion to the device's channel count happens only at the final stream
  write, with silence on the unused outs. All byte↔frame math (A/V master clock,
  backpressure, sync-pop pacing) is channel-aware.
- On a device with fewer physical outs than the opened count, SDL folds the
  extra pairs down — prep on the laptop, route at the venue.
- Remote: `AUDIOOUTS <pair>` (1-based) sets the selected cue's output pair.
- Routing persists per cue and per deck; smoke covers the round trip.

## 2026-07-11 — v0.78.16 (missing media: detection + relink)

- **Deckboy notices when show media is missing.** Every project load scans
  file-backed cues; cues whose files cannot be found get a red MISSING badge in
  the cue list, and a red **RELINK n** button appears in the toolbar next to
  BUNDLE while anything is missing.
- **One-click relink.** RELINK opens a folder picker; Deckboy searches the
  chosen folder recursively for files matching each missing cue's filename and
  repoints the cues — exact file-size match wins when several files share a
  name. Toast reports "relinked X, Y still missing". Clicking RELINK first
  re-checks the disk, so a re-mounted drive clears the warning without any
  picking.
- Smoke test covers the scan + relink round trip.

## 2026-07-11 — v0.78.15 (audio fade envelopes on every waveform)

- **The audio fade envelope is drawn over every waveform** — the timeline's
  audio lane, the program monitor strip, and both cue-panel thumbs and strips.
  Ramps show the EFFECTIVE audio fades (the cue's a-fades when set, otherwise
  the visual fades), anchored to the in/out points — the same resolution the
  audio thread applies, so what you see is what plays.

## 2026-07-11 — v0.78.14 (AUDIO section; deck fader named; independent audio fades)

- **The cue inspector has a collapsible AUDIO section** in both layouts: enable,
  gain, pan, mono, the new audio fades, and normalize — one place for the cue's
  whole audio story, collapsible like GEOMETRY and OVERLAYS.
- **"volume" is renamed "deck fader."** It is the deck's live playback level
  (keyboard +/-) and is not saved with the cue; per-cue trim is gain, in the
  AUDIO section.
- **Independent audio fades**: `a-fade in` / `a-fade out` per cue — follow the
  visual fade (default), none, or explicit seconds. Duck audio early under a
  long video tail, or hold it under a fast visual cut. Applied through the same
  audio-thread fade mirrors; persisted, backward compatible, with a smoke
  round-trip.

## 2026-07-11 — v0.78.13 (readability lives in the themes)

- **Readability is a theme-data contract**, and the v0.78.12 renderer-side guard
  is gone (the owner: fix the data, not the renderer).
  - The 26 affected themes take a light trim tint of their own hue for
    `shell_inner` — which is also what the real consoles look like — tuned until
    every ink role clears its ratio.
  - The `dark` theme is reworked: graphite chrome, light rows, near-black ink.
  - `gamecube`'s selected-row purple and two `screen_light` tones nudged.
- New `tools/audit_theme_contrast.ps1` encodes the exact ink/fill pairs the UI
  draws with, at per-pair minimum ratios — run it after editing any theme; "all
  themes pass" is the contract.
- The startup wordmark stays headline-sized from v0.78.12; the version tag stays
  small on the subtitle line.

## 2026-07-10 — v0.78.12 (theme readability guard; startup headline)

- **Every theme renders readable text.** `rebuildPalette` ends with a
  readability pass: WCAG-style contrast is enforced between the role pairs the
  UI actually draws (light-on-deep 4.5:1, dark-on-light 3:1, accents and
  secondary ink proportionally), nudging only the offending tone's lightness.
  Hue identity survives; compliant themes are untouched.
- The startup prompt's "Deckboy" headline is headline-sized (a new 42 pt
  pixel-font instance, scaling with UI scale).

## 2026-07-10 — v0.78.11 (A/V delay offset)

- **Settings → Audio: "A/V delay" (0–1000 ms, ±10 steps).** Holds ALL deck audio
  back by the set amount, for chains where the display or PA DSP lags the video.
  Applied live in the audio threads through a delay FIFO — no restarts — and the
  A/V master clock anchors to the undelayed timeline so the skew is real at the
  device. VU meters follow the delayed (heard) audio. The Pocket Test sync pop
  runs through the same delay, so the dial-in workflow is: take the Pocket Test
  at the venue, watch the beacon, and nudge the delay until flash and pop land
  together.
- Persisted with the show (`audio_delay_ms`).

## 2026-07-10 — v0.78.10 (per-cue loudness normalize)

- **Normalize loudness, per cue**: one button in the audio section, and the
  `AUDIONORM` remote command, measures the file's EBU R128 integrated loudness
  on a worker thread and sets the gain trim for -16 LUFS playback — a starting
  point you can still nudge. Toasts the measured loudness and the applied trim
  when the analysis lands.

## 2026-07-10 — v0.78.9 (per-cue audio: gain, pan, mono)

- **Every cue with audio has its own audio section:**
  - **Gain**: -24 to +12 dB trim (dB, not multipliers), for media that arrives
    at wildly different loudness.
  - **Pan**: stereo balance with centre snap.
  - **Mono**: downmix toggle for mono sources and mono venue PAs.
- All three apply **live** in the audio thread — no decode restart, VU meters
  follow — and they sit under the audio toggle in the inspector as scrubbable
  quick rows. The Pocket Test sync pop honours them too.
- New remote commands for Companion: `AUDIOGAIN <dB>`, `AUDIOPAN <-1..1>`,
  `AUDIOMONO <0|1>` — the same single write path as the inspector.
- Persisted per cue (appended fields, backward compatible); covered by a new
  `--smoke` round-trip assertion.

## 2026-07-10 — v0.78.8 (Pocket Test: the ball bounces; patterns run at display rate)

- **Pattern rebuilds are locked to the selected display's refresh rate** — or
  the project's explicit output refresh when set — and the card is layered: the
  static layer (grid, bars, grayscale, ramp, PLUGE/detail patches, border,
  crosshair) is cached per raster and memcpy'd, the island scene renders at a
  fixed internal 640x360 (chunky pixel art — nearest sampling is on-brand), and
  only the dynamics draw per frame.
- **The circle is a ball.** It bounces slowly around the whole frame, DVD-logo
  style, at constant velocity — the scene behaves as the full background behind
  the card, and the ball is a porthole revealing whatever it floats over (sky up
  top, beach and characters at the bottom). It doubles as the smooth-motion /
  judder object and a burn-in rover; the A/V sync beacon rides at its 12
  o'clock. The alignment crosshair stays fixed at frame centre.
- Motion is smoother across the board: pattern animation steps once per display
  frame.

## 2026-07-09 — v0.78.7 (Pocket Test: PM5544 edition; pattern policy pass)

- **Pocket Test rebuilt as a proper broadcast test card**: full-frame crosshatch
  grid, 75% colour bars + grayscale staircase across the top, ramp +
  PLUGE/fine-detail/shimmer patch row across the bottom, slow diagonal sweep
  over the grid, Emerald-style ID box (version, raster, clock, scene) — and the
  island scene lives INSIDE the centre circle, Test Card F style, still cycling
  day/sunset/night/storm with the crossfade. The sync beacon sits at the
  circle's 12 o'clock, flashing in the 80 ms pop window; crosshair at dead
  centre.
- **Motion policy: all pattern motion is slow, smooth, and diagonal.**
  Crosshatch drifts one cell per 8 s at 45°, checkerboard one period per 10 s,
  SMPTE bars get a 12 s diagonal sweep line instead of scan lines.
- **Solid colour patterns are static** — a pulsing reference level is a
  contradiction; legacy saves degrade to the static colour.
- **Terrarium is a secret again:** hidden from the pattern pickers until it is
  found. Saved terrarium cues load regardless.

## 2026-07-09 — v0.78.6 (Pocket Test: audible for real, strobe defused)

- **Existing Pocket Test cues become audible on load.** `normalizeProject` flips
  legacy pocket-test cues to audible once, and new ones are created with
  `hasAudio=true` so the audio toggle is visible; a mute made after that
  persists. (`--sync-pop-test` verified the engine synth path end to end against
  a real device: PASS.)
- **The ? block shimmers rather than strobing.** A full-square 30 Hz flash is a
  photosensitivity hazard; it is now a single-pixel checkerboard whose phase
  inverts at 30 Hz — a soft shimmer, while dropped or doubled frames still make
  it freeze or beat visibly.
- New `--sync-pop-test` CLI: runs the real sync-pop path against the default
  audio device and reports PASS/FAIL — first stop for any "test card has no
  audio" question.

## 2026-07-09 — v0.78.5 (Pocket Test field notes: audio, scaling, Emerald)

Four things from the owner's first hands-on with the test card:

- **The sync pop plays.** Pocket Test cues default to audio ON, and the pop
  follows the visual rather than the transport state: if the buoy lamp is
  flashing on the output, it pops — STOP-dark silences it.
- **Scene transitions crossfade** (1.4 s pixel blend) instead of hard palette
  cuts. The card chrome draws over the blend and never fades.
- **All patterns build pixel-mapped to the LIVE program-output raster,**
  re-checked every rebuild, so a display switch mid-show retargets patterns
  automatically (new `OutputSizeProvider` on MediaEngine). A test pattern that
  is not 1:1 with the selected display is lying.
- **A skiff sails the ocean** at constant velocity (4 s per screen) as the
  judder object, over the same 10% fence ticks.
- **Chrome restyled as GBA Pokémon Emerald** (the game, not the colour): white
  windows with dark-grey outline + teal beveled frame band, dark-grey text with
  the signature light drop shadow, red name text and continue-cursor, plus an
  Emerald battle-style status panel — the HP bar drains across each 14 s scene
  (green→yellow→red, doubling as the scene timer) and the EXP bar fills every
  second in step with the buoy pop.


## 2026-07-09 — v0.78.4 (Terrarium goes native)

- **Terrarium is a first-class Deckboy generator source:** `pattern://terrarium`
  runs the full ecosystem simulation in-process — no companion exe, no window
  capture. The sim (`native/extras/terrarium_core.hpp`) ticks at its native
  9 TPS and renders its 200x112 glyph world to a 1600x896 frame with the same
  8x8 font, palettes, seasons, weather, clouds and creatures as the standalone
  app. One world per show: every deck and preview shows THE terrarium, and it
  keeps living across cue reloads. New shows start with a warmed-up ecosystem,
  not bare dirt.
- "Terrarium (living ecosystem)" appears openly in the pattern picker; the
  secret purple cue is now backed by the native pattern.
- The standalone `terrarium.exe` still builds and ships, sharing the same
  simulation core.
- `--smoke` verifies the native terrarium renders a living world.

## 2026-07-09 — v0.78.3 (Pocket Test A/V sync pop)

- **The test card checks audio/video sync.** A buoy bobs in the ocean; its lamp
  flashes for 80 ms at the top of every second, and the deck plays a matching
  1 kHz pop in exactly that window — the first audio a pattern cue has ever
  produced. Watch and listen at the end of the chain: any gap between flash and
  pop is the chain's A/V offset. The pop respects deck volume, master gain and
  cue fades, and drives the VU meters like any decoded audio. Mute it per-cue
  with the cue's audio toggle.

## 2026-07-09 — v0.78.2 (Pocket Test goes diegetic)

- **The test card's instruments are part of the island world** — same
  measurements, exact values, new bodies:
  - Colour bars → a **billboard** on the beach.
  - Grayscale staircase → **stone steps** climbing to the right edge.
  - Banding ramp → a **banner** strung across the sky.
  - Black-crush check → a **cave** whose creature-eyes (2% / 4% on black)
    vanish if the chain crushes blacks.
  - White-clip check → a **cloud** with 98% / 96% lumps.
  - Flicker box → a **flashing ? block** (~30 Hz).
  - Fine-detail patch → a **beach TV playing static** (1px checker + stripes).
  - Judder lane → a **runner** crossing the screen at constant velocity past
    **fence posts at exact 10% spacing**.
  - The Pokémon-style dialog box (version, raster, clock, "A WILD NIGHT
    APPEARED!", blinking continue-cursor) stays as the game-UI layer, with
    chunky Game Boy borders.
- The scene and its rainbow footer are fully visible, with the border,
  safe-area and crosshair guides unchanged.
- Smoke verifies the diegetic values (75% red, 2% cave eyes, 96% cloud lump).

## 2026-07-09 — v0.78.1 (Pocket Test is a real test card)

- **The Pocket Test pattern grew broadcast instrumentation.** The island scene
  stays as the living backdrop; drawn over it (auto-cycling `pocket-test` only —
  `pocket-day/sunset/night/storm` stay clean for use as backgrounds):
  - **1px checkerboard border + corner marks** — pixel mapping and
    crop/overscan: any scaling between Deckboy and the display greys the border
    out instantly.
  - **Dashed 90% / 80% safe-area guides + centre crosshair.**
  - **Instrument strip:** 75% colour bars, 11-step grayscale staircase,
    continuous ramp (banding check), PLUGE-style 0/2/4% black and 100/98/96%
    white patches (black crush / white clip), single-pixel checker + 1px stripe
    patches (fine detail / interlace), ~30 Hz flicker box (dropped/doubled
    frames), and a full-width constant-velocity motion lane with 10% ticks
    (judder).
  - **Info plate** in a built-in 3x5 pixel font: build version, actual raster
    (e.g. 1280x720), running clock, scene name.
- New `--pattern-dump <pattern-id> <out.ppm> [WxH] [t]` CLI renders any pattern
  frame to a PPM for inspection and docs.
- `--smoke` asserts the card instrumentation is present and that the scene
  variants stay clean.

## 2026-07-09 — v0.78.0 (in-process GPU decode — zero-copy video)

- **File-backed Video/Audio cues decode in-process through the FFmpeg libraries
  (libav\*)** rather than two `ffmpeg.exe` subprocess pipes per deck. On Windows,
  video decodes via **d3d11va directly on the program output renderer's D3D11
  device and the frames never touch the CPU**: the output compositor GPU-copies
  each decoded NV12 texture slice into a persistent SDL texture. That removes the
  per-frame GPU→CPU download, the swscale pass, ~41–83 MB/s of pipe transfer and
  the CPU→GPU re-upload — the transport cost that made the fanless Pocket 3
  marginal.
- **What still uses the ffmpeg CLI** (by design): live streams (SRT/NDI), source
  capture, stills/thumbnails, waveform analysis, ffprobe ingest, and stream
  encode-out. Files with rotation metadata also stay on the CLI, which
  autorotates where libav does not.
- **Automatic fallbacks:** effects cues (chroma key / colour controls) and
  non-hw codecs decode in-process to CPU frames — still no subprocess, no pipe —
  and if an in-process open fails for any reason the engine falls straight back
  to the CLI pipe path. `--no-inproc-decode` forces the CLI path for a whole
  run, and building with `-DDECKBOY_INPROC_DECODE=OFF` produces the pure-CLI
  binary.
- **Resilience:** the decoder validates files by priming the first frame before
  committing, tolerates runs of corrupt packets by degrading to EOF, and a
  watchdog reracks the deck dark with a toast if a decode wedges mid-show. A
  corrupt-file test is part of `--smoke`.
- **Playback semantics unchanged:** same frame queue and backpressure, same
  audio-master A/V clock (audio decodes in-process to the same s16/48k stereo
  stream, speed through the same atempo semantics), same seek/EOF/loop
  behaviour.
- **`--decode-bench <file> [seconds] [cli]`** measures decode throughput through
  the real engine path for before/after comparison on a given machine.
- **Cheap wins bundled:** decode threads are capped so they cannot starve the
  render loop on 4-thread CPUs; the always-on no-op scale pass is skipped on
  both paths; and the engine re-uploads a frame to its texture only when it
  changes.
- **Load-bearing hint:** `SDL_HINT_RENDER_DIRECT3D_THREADSAFE=1` is set at init
  — SDL otherwise creates single-threaded D3D11 devices, which cannot be shared
  with a decode thread. Never remove.
- The zip carries the libav\* DLLs plus the FFmpeg licence notice
  (`LICENSE-ffmpeg.txt`).

## 2026-07-08 — v0.77.0 (SDL3 migration)

- **Whole-app migration from SDL 2.32 to SDL 3.4** (same for SDL_ttf), the
  platform groundwork for the in-process GPU decode rewrite, which needs SDL3's
  `SDL_CreateTextureWithProperties` D3D11 texture import for zero-copy video.
- **Immediate wins shipped with the migration:**
  - Per-renderer vsync (`SDL_SetRenderVSync`): program outputs stay vsynced to
    their display; stream-only outputs and the hidden per-deck decode renderers
    run unthrottled.
  - SDL3's Windows DPI handling replaces the `permonitorv2` hint; mixed-DPI
    display topologies are handled natively by SDL.
  - Audio moved to SDL3 audio streams (`SDL_OpenAudioDeviceStream` +
    `SDL_PutAudioStreamData`): each deck keeps its own logical device on the
    chosen output, UI sounds keep a separate logical device on the default
    output, and LTC ingest reads a recording stream that resamples to 48 kHz
    mono S16 in SDL.
- **Compatibility layer** `native/core/sdl_compat.hpp` keeps the codebase's
  integer-rect layout math and SDL2-style display indices working on SDL3.
- **Terrarium** companion exe migrated in the same pass — one SDL3 runtime ships
  in the zip.
- Validated: clean build, `--self-check` ok, `--smoke` 0 failures, live visual
  check (control UI + program output on the HDMI dongle, video + audio playing,
  VU meters live).

## 2026-07-05 — v0.76.31 (Media Encoder, splash system, multi-select, audio hot-swap)

- **Built-in media converter + ENCODER tab.** Cues Deckboy cannot play, or would
  play poorly — 10-bit HEVC, AV1, ProRes, >1080p — are flagged on import; a
  contextual CONVERT button appears in the inspector, and a new **Settings →
  Encoder** tab batch-converts them (H.264 MP4, GPU with libx264 fallback) into a
  portable `_converted/` folder next to the show, then swaps the cue to the copy.
  Original media is never touched.
- **Splash system.** Grayscale scene splashes cycle at boot, tinted to the active
  theme; the default (gameboy) theme boots the branded DECKBOY-wordmark splash.
- **Themes.** The sci-fi colourways take Nintendo names (Luigi, Kirby, Mario,
  Pikachu, Star Fox, Game & Watch, Peach, Ganon, R.O.B., Waluigi, Midna, …) and
  every generated theme's background goes to a near-pure `#050505` black so
  accents pop harder. Theme is saved per show; a New show resets to the default
  skin.
- **Multi-select.** Ctrl+A selects all cues; per-cue row toggles (hold, loop,
  fade, audio) apply to the whole selection.
- **Reset button.** RESET in the SELECTED CUE panel restores a cue's inspector
  settings to deck defaults across the whole selection, leaving media, name and
  metadata intact.
- **Scrolling.** The main deck cue list clamps with a bottom-only rubber-band
  that springs back when the wheel goes idle.
- **Changing audio device keeps playback running** — the device is hot-swapped
  on the running engine.
- **The fullscreen button re-arms in one click** after New Show or a relaunch.

## 2026-07-04 — v0.76.30 (Save/Save As, theme library, output black-on-disable)

- **SAVE prompts for a location.** The toolbar SAVE button and Ctrl+S open a file
  picker and write the project only. BUNDLE is unchanged (export with media);
  Ctrl+Shift+S remains an explicit Save As.
- **24 new themes** under `data/themes/` — dark, high-contrast sci-fi colourways
  (Tritium, Cerenkov, Ion, Amber CRT, Plasma, Halon, Nebula, Infrared, Hazard,
  Cryo, Toxic, Cobalt, Ultraviolet, Quasar, Voidsteel, Crimson Protocol) plus
  Nintendo-flavoured ones (Virtual Boy, Famicom, Super Famicom, N64, GameCube,
  Switch Neon, Hyrule, Metroid). The Appearance dropdown auto-discovers any
  `data/themes/<name>/theme.txt`.
- **Theme is saved with the show.** `Project::theme` persists the chosen
  colourway so it survives restarts (`DECKBOY_THEME` still overrides at boot);
  opening an older theme-less show leaves the current pick untouched.
- **Output clears to black instead of holding a frame.** Disabling an output,
  starting a new show, switching displays or quitting flushes a black frame, so
  the program display — and capture dongles that latch the last signal — do not
  keep the previous session's last picture.
- **The cue list stops at the last cue**, in both the deck list and the overlay
  bin.
- **Inspector fields are type-to-replace.** Opening a value field treats the
  existing value as selected: click → type → Enter.
- **Transport play/pause reflects state** — pause icon while playing, play icon
  while paused.
- **The audio timeline is click-to-seek**, matching the video lane.
- **Dropping a folder recursively imports** every acceptable video, image and
  audio file in name order; audio files import as Audio cues.

## 2026-07-03 — v0.76.29 (Extended boot sequence)

- The splash boot console runs a ~24-line scrolling sequence: real init values
  (theme, fonts, raster/displays, audio buffer, decks/cues, Companion port, NDI
  state, wall-clock/audio-crystal sync) interleaved with critical subsystems —
  flux capacitor (1.21 GW nominal), heisenberg compensators (probably), dilithium
  matrix, gremlin containment field, spline reticulation. Lines scroll
  console-style with randomised boot timing — quick bursts, normal lines, and the
  occasional probe that stalls for half a second — reshuffled every boot and
  always finishing within the ~5s splash (Enter/Esc/click still skips instantly).

## 2026-07-03 — v0.76.28 (Terrarium wired in, richer boot console)

- **The secret grows things.** Terrarium (v0.46, vendored at
  `native/extras/terrarium.cpp`) ships as a bundled companion exe. Entering the
  code launches it windowed and adds a "TERRARIUM (secret)" window-source cue to
  the playlist — TAKE it to put the ecosystem on program.
- **Splash boot console enriched**: the original boot tasks joined by real values
  (version, raster + display count, decks/cues shelved, live Companion port, NDI
  runtime state) and essential hardware checks (rubber chicken calibration),
  revealed line by line.
- Packaging bundles terrarium.exe.

## 2026-07-03 — v0.76.27 (Window picker, boot log, layer fader)

- **Window cues get a real picker**: adding a Window Source cue lists every
  visible window by title, plus "Entire Desktop". Note: gdigrab matches titles
  exactly, so apps that retitle themselves (browsers per tab) need re-picking
  after a title change.
- **Startup boot log**: the startup dialog plays a retro BIOS-style boot log —
  real values (raster, displays, decks/cues, Companion port, NDI runtime)
  interleaved with important diagnostics such as RUBBER CHICKEN ... CALIBRATED,
  revealed line by line.
- **Deck layer fader tidied**: the playlist-footer opacity rail is the multi-deck
  LAYER fader (compositing weight when decks stack on one output). It hides in
  single-deck shows and appears labelled ("LAYER n%", legible deep ink) when a
  second deck exists.

## 2026-07-03 — v0.76.26 (transport verbs, cameras, full-speed in background)

- **Full speed whether or not Deckboy has focus**: it opts out of Windows 11
  EcoQoS and timer-resolution coalescing at startup, so the decode and audio
  timing loops hold their 4ms cadence while you work in another window.
- **Three distinct transport verbs**: PAUSE freezes in place (resumable); RERACK
  returns to the top holding the first frame, ready; STOP darkens the deck AND
  reracks — visual cleared, decode pipes and capture devices released, so a
  stopped webcam turns its light off. PLAY after a dark STOP revives the pipes.
- **Camera cues work on Windows** through a new DirectShow backend. Webcams,
  HDMI capture sticks and Blackmagic WDM devices are all DirectShow video
  devices, so one Camera/Capture cue covers them: adding a camera cue enumerates
  devices and opens a picker, auto-selecting when there is exactly one. TAKE
  starts capture; STOP releases the device. (DirectShow devices are
  exclusive-open, so live preview-while-live-program of the same device needs a
  shared-capture architecture — future work.)
- **Audio lane honesty**: an empty waveform analysis is cached rather than
  respawning ffmpeg, live sources show "live audio", audio-less cues show "no
  audio track", and waveform analysis runs only for file-backed video and audio
  cues.
- **Test pattern cleanup**: the pattern picker lists base types only — motion is
  the toggle's job — and the four Pocket scene variants merged into the one
  cycling Pocket Test (legacy ids still load). The crosshatch grid is anchored to
  the centre crosshair at every raster.
- **Deck opacity fader labelled**: the strip under the playlist reads
  "OPACITY n%".
- **??? **: ↑ ↑ ↓ ↓ ← → ← → B A Start.
- **Dead-control sweep**: automated checks for duplicate action ids, unhandled
  quick actions, buttons without handlers, and struct fields written but never
  consumed — all clean after this release.

## 2026-07-03 — v0.76.25 (the master volume fader drives the audio)

- **The header master volume fader controls audio.** It feeds a per-engine master
  gain applied in the audio thread on top of the per-cue volume, synced from the
  project every tick, so every path to it — the fader, the `MASTERVOL` remote
  command, project load, undo — takes effect on all decks.
- **The fader is draggable** as well as clickable.
- Saved boost levels load at the value they were saved with (range 0–2).
- New splash art (clean cityscape, no baked-in dialog) for both mascots.

## 2026-07-03 — v0.76.24 (settings readability pass)

- **Every settings button fires.** The Video Outputs sub-tabs, the Allow Remote
  toggle and the Stream Key prompt have action ids of their own, so Processing,
  Mascot, UI Scale, Pocket 3 and Identify each do their own job.
- **Settings modal readability redesign** — zero functional change, every control
  in the same place:
  - Pixel-face **SETTINGS** title.
  - Cartridge-shelf tab bars: the active tab is full height and "plugged in" to
    the content frame; inactive tabs sit recessed. Applied to the main tabs and
    the Video Outputs sub-tabs.
  - Every card and section has a dark **label plate** header with light text —
    one strong, scannable anchor per group. One shared helper replaces three
    duplicated card-drawing lambdas.
- **New splash art**: the "cue gremlin cityscape" scene (Deckbot + Deckgirl
  rigging a dot-matrix wall over the skyline) for both mascot choices.

## 2026-07-03 — v0.76.23 (value scrubbing, math shorthand)

- **Drag any inspector value to scrub it.** Click-hold a value cell (width,
  height, offsets, rotation, crop, fades, volume, speed, …) and drag horizontally
  to step it — the same gesture as number scrubbing in AE and Resolve. A plain
  click still opens the exact-entry editor. Works on every inspector quick row
  with -/+ buttons; width/height scrubbing respects the aspect link.
- **Math shorthand in numeric entry**: `x` multiplies and `px` units are ignored,
  so `1920x2`, `960px * 2` and `3840/2` all evaluate. Applies to every numeric
  entry field — they share one expression parser — covered by four new smoke
  checks.

## 2026-07-03 — v0.76.22 (fades off by default, pixel commands)

- **New clips import with fades OFF**, so a freshly imported clip cuts in and out
  cleanly until you turn a fade on per cue (cue-row fade icons or the inspector
  fade rows toggle 0 ↔ the deck's default fade time). Existing show files keep
  their saved deck settings; the FADE IN / FADE OUT pills in Settings → System →
  Show Flow set an existing deck's default for future imports.
- **Fades are covered by the smoke harness**: three end-to-end checks drive a
  real MediaEngine through loadCue → position clock → `currentVisualFadeGain()`
  — the exact gain the output compositor multiplies into the frame alpha — and
  assert the fade-in and fade-out ramps.
- **Remote `WIDTH <px>` / `HEIGHT <px>` commands**: pixel-based cue sizing from
  Companion or OSC, through the same code path as the inspector editors, so the
  aspect link applies. Legacy `SCALE`/`SCALEX`/`SCALEY` factor commands are
  unchanged.

## 2026-07-03 — v0.76.21 (pixel-based geometry editing with aspect link)

- **Cue size is edited in pixels, everywhere.** The GEOMETRY rows are labelled
  `width` / `height`, show the actual rendered output size in px, and clicking a
  value prompts for a pixel value ("Width (px)"). The multiplier still exists
  under the hood, per cue, derived from the cue's base rendered size, but the
  operator never sees it.
- **Aspect-ratio link** (new `link aspect` toggle row, on by default, persisted
  in the show file): changing width scales height proportionally and vice versa —
  including the `-`/`+` nudge buttons and typed exact values. Toggle it off for
  deliberate distortion, like the chain-link in most media software.
  Multi-select edits apply the pixel value per cue, so "make them all 960px
  wide" works.

## 2026-07-02 — v0.76.20 (fullscreen that stays put, display identify)

- **Fullscreen outputs stay on the program screen when you click the control
  window.** `SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0` is set at startup, so a
  playout output holds its display no matter where focus is.
- **Borderless fullscreen by default**: exclusive fullscreen — a real display
  mode switch, with its screen blanking and mixed-DPI placement quirks — is used
  only when the operator explicitly asked for a fixed raster or a specific
  refresh rate. Display-native outputs use borderless fullscreen, which is what
  every modern playout tool does.
- Recovery messages name the trigger, so the log says what moved an output and
  why.


## 2026-07-02 — v0.76.19 (engine and display robustness)

- **The media engine owns its cue.** The engine keeps a private snapshot of the
  loaded cue, so importing media or deleting cues while another cue is live
  leaves the live cue exactly as it was. Live edits to the active cue — fade in
  and out, and the rest — still apply immediately, because the app refreshes the
  snapshot on every project edit.
- **Audio fades are race-free**: the audio decode thread reads fade parameters
  from atomic mirrors, so a gain change lands cleanly.
- **A/V sync — audio is the master clock**: video position re-anchors to the
  audio device clock when the two drift more than about two frames apart, which
  holds lip-sync over long-form clips and keeps variable-frame-rate sources
  watchable.
- **Displays are matched by name, not number** (new `displayName` field in the
  show file). After a hot-plug, a reboot or a driver re-enumeration shuffles
  display numbers, an output re-attaches to the monitor it was aimed at, so
  program output stays off the operator's screen.
- **Display hot-plug respects the Esc safety contract**: connecting or
  disconnecting a monitor leaves outputs the operator escaped to windowed
  windowed, and healing goes through the per-output recovery path.
- **HyperDeck server hardening**: transport and clips replies are served from a
  structured main-thread snapshot, so a cue named "playing" is just a cue name.

UI polish, from the operator's screenshot notes:

- Empty timeline lanes draw no stray grid lines under the "take or select a
  cue..." placeholder — time graduations draw only over an actual timeline, in
  theme ink.
- Cue inspector rows share one label-column width, so value boxes line up down
  the panel.
- Playlist footer ("LOOP | ORDER") and the inspector empty-state panel derive
  text heights from the live font, so descenders survive at scaled and HiDPI
  font sizes.
- Cue-row action icons are solid shapes: filled rising/falling wedges for fade
  in and out, thicker pause bars, a filled speaker horn — legible at 20px.

## 2026-06-20 — v0.76.18 (new app/taskbar icon)

- **New Deckboy app icon**: a pixel-art handheld-cartridge icon with a green
  play triangle, embedded in `Deckboy.exe` and applied to the live windows.
  Source art at `art/windows/icons/deckboy_app.ico` (multi-resolution 16→256 px,
  regenerable from `deckboy_app_master.png`). `art/windows/deckboy.rc.in` plus a
  `configure_file` step generate the resource at build time, so it survives a
  fresh CMake reconfigure and feeds both the Explorer/Alt-Tab/pinned-taskbar
  icon and the runtime window icon.

## 2026-06-20 — v0.76.17 (stills hold, high-refresh loop)

- **Still cues hold at the end of their duration** at full opacity — an
  Image, Pattern, Browser or Composite cue set to pause on its last frame stays
  on screen. Pausing mid-cue keeps its fade state where it was, too.
- **Still-type cues default to no fade-out.** A static graphic that holds should
  not dip to black at the end; a fade-out is still honoured if you turn it on
  per cue.
- **High-refresh render loop**: the main loop's floor is 240 Hz, so stills and
  transitions render at the monitor's full native rate (144/165/240 Hz) through
  vsync.

## 2026-06-17 — v0.76.16 (resizable control window, F11 fullscreen, mascot dropdown)

- **The control window is resizable** (minimum 1500×900). The per-frame layout
  reflow already adapts to the live window size, so dragging an edge or
  maximising spreads the UI into the space.
- **F11 toggles fullscreen** on the control window, with a
  "fullscreen"/"windowed" toast.
- **The mascot picker is a dropdown**, matching the Theme and UI Scale
  selectors.

## 2026-06-17 — v0.76.15 (layout chrome scales, dependency prompts, touch mode)

- **Layout chrome scales with `uiScale`.** Header height, button height, panel
  padding, gaps and the spacing unit are recomputed whenever UI scale changes,
  so panels, buttons and the bottom bar grow alongside the fonts instead of
  leaving them stranded in 1× chrome.
- **The dependency prompt fires from every operator-initiated path** — UI
  toggles, hotkeys, and the OSC/Companion `NDI` and `DECKLINK` commands all
  prompt when a runtime is missing. Loading a project stays silent, so opening a
  show authored elsewhere does not surprise you with prompts.
- **Touch interaction mode**: a new `Project::interactionMode` ("mouse" default,
  "touch"). The Pocket 3 preset flips it alongside the 2.0× scale, and in touch
  mode the splitter and context-menu hover highlights are suppressed — a tap
  cannot hover, and a sticky highlight after a drag-release is the worst of it.

## 2026-06-17 — v0.76.14 (mascot swap, dependency prompts, UI scale, portable zip)

- **Swappable splash mascot**: Settings → APPEARANCE has a `MASCOT` pill that
  flips between **Deckbot** and **Deckgirl**, saved per project. The splash
  loader tries `.mp4` and `.gif` before `.png`, so an animated mascot is a
  drop-in asset replacement with no code change.
- **Runtime dependency prompt**: Deckboy detects when you enable a backend whose
  runtime is not installed on this machine and offers a one-tap link to the
  official vendor download page. Lazy by design — nothing fires at startup.
  Wired for the NDI Runtime (NDI Output toggle and the `N` hotkey), Blackmagic
  Desktop Video (DeckLink Output toggle) and the Microsoft WebView2 Runtime
  (Browser cue creation, Windows). No auto-downloaded installers.
- **UI scale factor**: Settings → APPEARANCE has a `UI SCALE` dropdown (1.00×
  default, 1.25×, 1.50×, 2.00×). Every TTF face is reopened at `base × scale`
  point size, so text stays crisp on 4K and Pocket 3 displays.
- **Pocket 3 / Touch preset**: an APPEARANCE pill that flips UI scale to 2.0× and
  back — a single-click ergonomic profile for 8" 1920×1200 handhelds.
- **Portable Windows zip**: `tools\package_windows.ps1` produces
  `dist\Deckboy-<VERSION>-windows-x64.zip` with the exe, every co-located vcpkg
  DLL, ffmpeg + ffprobe, the MSVC C++ runtime DLLs (app-local, so no Visual C++
  Redistributable install is needed), `data/`, `LICENSE`, and a `README.txt`
  describing the optional NDI / DeckLink / WebView2 installs. Unzip and run.

## 2026-06-16 — v0.76.13 (GPU video upload — NV12 fast path)

- **Live video decode uses NV12 by default** — about 62% less pipe bandwidth and
  CPU memory traffic than the RGBA path. FFmpeg writes a planar Y plus
  interleaved UV plane straight into the frame buffer; SDL uploads it through
  `SDL_UpdateNVTexture` and the GPU samples the YUV→RGB conversion at blit time.
  Cues with chroma key or colour controls decode as RGBA so the CPU effects path
  keeps working unchanged.
- **The software scaler runs fast_bilinear** in the live video pipeline, which
  costs a third of bicubic and is indistinguishable on moving video at deck
  output sizes. Stills and thumbnails keep their `flags=neighbor` path.
- `DecodedFrame` carries a `FramePixelFormat` tag, and all six upload sites
  recreate their texture on a format or size change and pick the right upload
  call. New helper `syncFrameTexture()` in `render/texture_helpers.hpp`.
- **Known trade-off**: toggling chroma key or colour controls on a cue already
  decoded as NV12 takes effect at the next TAKE. A cue that needs
  live-toggleable effects should have at least one effect parameter enabled
  before TAKE, so the decoder picks RGBA up front.

## 2026-04-24 — v0.76.12 (full networking stack on Windows)

- **Every network protocol works on Windows**, through Winsock2:
  - **Companion control** (TCP+UDP): Bitfocus Companion integration, OSC message
    parsing, subscriber tracking, feedback broadcasting
  - **OSC Query server** (TCP HTTP): endpoint discovery and state queries
  - **ATEM tally bridge** (UDP): ATEM tally packets as transport triggers
  - **Art-Net DMX bridge** (UDP): DMX packets for lighting trigger integration
  - **NMC sync** (UDP): Network Master Clock, both input (play/stop/seek) and
    output (broadcasting transport state to followers)
  - **NDI trigger bridge**: NDI metadata frames as cue triggers
  - **HyperDeck server** (TCP): Blackmagic HyperDeck protocol emulation for
    hardware controllers
- **LTC ingest works on Windows**: libltc 1.3.2 built from source as `ltc.dll`,
  found by the dynamic loader alongside the Linux and macOS candidates, with SDL
  audio capture providing the PCM input on every platform.
- **What remains platform-gated**: ALSA MIDI input is Linux-only, and is covered
  elsewhere by the cross-platform RtMidi backend.

## 2026-04-24 — v0.76.11 (DeckLink UI, NDI on Windows, Spout output, RtMidi)

- **DeckLink settings UI** in the Video Outputs tab: enable toggle, device
  dropdown (connected Blackmagic devices with SDI/HDMI/4K capability labels),
  output mode dropdown (720p through 4K at all standard frame rates), and a
  10-bit toggle. Changing device or mode re-initialises the output with the new
  configuration.
- **NDI output works on Windows**, audio sends included.
- **NDI 6 SDK search paths** added to CMake for Windows and Linux.
- **Spout2 output backend** (Windows): interprocess texture sharing through the
  SpoutLibrary DLL, so a Deckboy sender is visible to any Spout-capable receiver
  — OBS, Resolume, TouchDesigner. Includes the catalog entry with a
  `DECKBOY_HAS_SPOUT` build gate, route planning, lazy sender lifecycle,
  settings UI (enable toggle + sender name editor) and project serialisation.
  Installed through vcpkg (`spout2:x64-windows`), auto-detected by CMake.
- **Stream output works on Windows**: ffmpeg SRT/RTMP egress reads raw video
  from `pipe:0`, through a new `StdioMode::Pipe` on the cross-platform
  `ChildProcess` API.
- **Live source capture works on Windows**: WindowSource cues capture desktop
  regions through ffmpeg's gdigrab.
- **RtMidi integration** (cross-platform): real hardware MIDI input through
  RtMidi 6.0.0 — device enumeration, port open/close, and message polling that
  dispatches CC, NoteOn, NoteOff and ProgramChange, including the
  NoteOn-with-velocity-0 → NoteOff conversion the MIDI spec asks for.
  Auto-detected from vcpkg; statically linked on Windows, so no extra DLL.

## 2026-04-11 — v0.76.10 (audio-lane loading animation)

- **The audio timeline lane has its own loading animation** while a cue's
  waveform peaks are still being computed: the same widget frame, dimming
  overlay and pulsing LOADING label as the video lane's filmstrip, with an
  animated 9-bar EQ meter in place of the filmstrip cells. Each bar's height is
  driven by an offset-per-bar sine phase with a squared envelope, so the motion
  feels musical rather than mechanical, peak bars get a brighter fill, and each
  bar has a 2px highlight cap.


## 2026-04-11 — v0.76.9 (settings text that fits at every font size)

- **Every settings card derives its row positions from the live font.** SAFETY /
  TIMECODE, SHOW FLOW, CUE TOOLS, PLAYLIST PREFERENCES, APPEARANCE, AUDIO
  OUTPUT, MIDI CONTROL, REMOTE CONTROL, OSC QUERY/FEEDBACK, DISCOVERY / NOTES,
  INTEGRATION ADAPTERS and the About tab's RUNTIME rows all reflow cleanly at
  1×, at retina sizes and at every UI scale between.
- The empty-deck hint in the control window ("I import / B browser / P pattern")
  and the Edge Blending and Area of Interest sub-panels follow the same rule.
- New helpers in `render/layout.hpp`: `textLineHeight(font)`,
  `rowYBelowLabel(labelY, font, gap)`, `rowYBelowLines(startY, font, lines,
  gap)`.
- **`drawTextSafe` / `drawCenteredTextSafe` centre on the rect midline and
  expand their clip to fit the text**, so descenders render in full even in a
  rect shorter than the font — the modal title, the AOI RESET button and the
  thin rects in the inspector thumbnail area all read completely.

## 2026-04-11 — v0.76.8 (correct probe of audio-first mp4s, duplicate cues allowed)

- **A video clip imports as a video cue** whichever order its streams appear in,
  so an mp4 that carries audio before video is no longer taken for an audio-only
  file.
- **The same media can be added to a deck twice** — the same clip as two cues
  with different in/out points, or a playlist that returns to an asset. The
  toast and the cue list make a duplicate obvious; library-level dedup belongs
  in the media library, not the cue list.

## 2026-04-11 — v0.76.7 (video cues fade in playlists)

- **Video cues in a playlist honour `fadeInSeconds` / `fadeOutSeconds`**, so a
  playlist mixing browser and video cues fades both the same way. The per-cue
  fade-in is the visible transition on playlist advance; a looping cue still
  suppresses the ramp on each cycle, which is what you want.
- **Audio fade ramps apply on auto-advancing video cues too**, so a cue with a
  fade-out ramps to silence at end-of-cue rather than stopping flat.

## 2026-04-10 — v0.76.6 (audio thread hardening, camera factory per platform)

- **The audio decoder thread copies whole samples**, aligning a short read from
  the FFmpeg pipe down to an even byte count and picking the odd byte up on the
  next read.
- **The camera capture factory answers per platform**:
  `createCameraCaptureBackend()` returns the v4l2 backend on Linux and the
  correct scaffold id elsewhere (`mediafoundation` / `avfoundation`), matching
  what the capture backend catalog advertises.
- Linux `ChildProcess::stop` closes the read end before the kill, so a reader is
  always unblocked, whether or not the process had already been reaped.
- Smoke asserts `supported && backendId == "gdigrab"` on Windows, which is what
  window capture has done since v0.76.1.

## 2026-04-07 — v0.76.5 (rapid takes, and TAKE without a pause)

- **Rapid TAKEs of video clips are safe.** Decoder shutdown kills the process
  first, so the read end returns EOF cleanly, then joins the threads, then
  releases the handle — the same order for the image thread and the thumbnail
  and timeline-strip threads.
- **TAKE is instant.** The dimensions probed at ingest are reused, so there is
  no synchronous ffprobe on the main thread at take time.

## 2026-04-06 — v0.76.4 (fade in/out on the output path)

- **Cue fade-in and fade-out apply on every output.** `MediaEngine` exposes
  `currentVisualFadeGain()` and the compositor multiplies it into the bridge
  texture alpha — `alpha = deckOpacity × fadeGain × 255` — for every cue type:
  browser, video, image, pattern and source capture.

## 2026-04-04 — v0.76.3 (GPU decode, TSL/tally, SRT/NDI input, audio buffer tuning)

- **GPU hardware decode**: video decode passes `-hwaccel auto`, so DXVA2/D3D11VA
  on Windows and NVDEC/VAAPI on Linux are used when available and fall back to
  software automatically — no configuration.
- **TSL/Tally protocol**: a UDP tally sender on port 5800 (TSL 3.1) sends
  program and preview tally state to tally hardware as deck active status
  changes. Settings → Network → Tally / TSL, with configurable port and target,
  on every platform.
- **SRT input**: a Video cue accepts an `srt://host:port` URL as its media path,
  handled natively by FFmpeg (which needs `--enable-libsrt`). Probe and seek are
  skipped for live URLs.
- **Live stream support** (RTMP/RTSP/UDP): the same live-stream handling for
  `rtmp://`, `rtsp://` and `udp://` — no probe delay, no injected seek.
- **NDI input via ffmpeg**: a Video cue accepts `ndi://NDI_Source_Name`, decoded
  by ffmpeg's `libndi_newtek` input device, with Windows NDI SDK library
  candidates added.
- **Audio buffer size tuning**: the SDL audio buffer reads from
  `Project::audioBufferSamples` (256/512/1024/2048, default 1024) — smaller for
  latency, larger for stability.

## 2026-04-04 — v0.76.2 (browser cue duration, Area of Interest crop)

- **A browser cue keeps its still duration**, so fade-out and auto-advance work
  for browser cues in a playlist.
- **Area of Interest (AOI) output crop**: per-output fractional edge crop
  (left/right/top/bottom, 0–1, 5% step) applied at the compositor→window blit,
  controlled from Settings → Output → Area of Interest, with a reset button and
  persistence in the show file.

## 2026-04-01 — v0.76.1 (browser cues on Windows, SOURCE menu, filmstrip)

- **Browser cues work natively on Windows** with no separate Chromium download:
  Deckboy finds the system Edge (or Chrome) installation, launches it in
  `--app=` mode at the requested size, and pipes frames into the deck engine
  through ffmpeg's `gdigrab` desktop-region capture — the same pipeline as the
  Linux Xvfb approach, with Edge and GDI.
- **"Browser / URL Cue" lives in the SOURCE dropdown**, alongside Window, Camera
  and Syphon sources, which keeps the three-group button balance.
- **Button labels sit centred**: the centring helpers snap the full button rect
  before computing the vertical centre, matching the painted background.
- **"Select or import a cue" keeps its descenders**, at an inspector
  empty-state rect height that snaps exactly.
- **Timeline filmstrip grid lines stay inside the filmstrip**, through per-draw
  clip rects.

## 2026-03-31 — v0.76.0 (audio waveform, VU meter, text overflow, version flow)

- **A clip that failed to analyse retries.** Empty waveform results are not
  cached, so a clip that could not be decoded on one run is analysed on the
  next.
- **The VU meter returns to zero** when the focused deck stops playing.
- **Text stays inside its box, everywhere.** `drawTextSafe` and
  `drawCenteredTextSafe` clip to the same snapped rect `drawUIPanel` paints, so
  the LIVE/STREAM/DECODE/WARP badges and the dB scale labels keep to their own
  separators.
- **Build-time version generation**: a change to `VERSION` takes effect on the
  next `cmake --build`, with no reconfigure.
- **Project root detection hardened**: the walk-up skips known build
  subdirectory names before checking for a `data/` directory, so the app anchors
  to the real root rather than to `build/windows/Release/data/`.

## 2026-03-29 (the previous show is the one you were in)

- **"Open Previous Show" opens the last project you actually used.** Deckboy
  remembers the last opened or saved `.deckboy` path in
  `data/last_project.txt` and offers that at startup.
- The saved default show's cue paths were repaired, so its clips load again.

## 2026-03-29 (cue inspector text stays in its box)

- **Inspector text clips to its own control box while scrolling**, against the
  real control bounds as well as the scroll viewport, so a partially scrolled
  row keeps its text inside its row.
- **Inspector spacing is roomier**: taller rows, more gap between rows and
  sections, and more breathing room around label, value and edit controls.

## 2026-03-29 (timeline scrubbing, and a roomier cue list)

- **The timeline supports held-drag scrubbing.** Clicking the progress bar still
  seeks immediately; holding and dragging updates the seek target continuously
  until you let go, which is what the tooltip always said.
- **Cue rows and inspector controls have more vertical headroom**, with the cue
  name in the smaller face so long names sit comfortably.

## 2026-03-29 (the Deckboy icon on every window)

- **The running Windows app applies the Deckboy icon to its SDL windows**, big
  and small, from the embedded `IDI_DECKBOY_APP_ICON` resource — so the
  taskbar and titlebar match the executable's branding whatever Explorer has
  cached.

## 2026-03-29 (the program monitor owns its area)

- **The program monitor fills the monitor area** at every layout width, with no
  second preview strip appearing — and nothing is decoded for one in the
  background either.
- **Program header telemetry respects tight layouts**: the output, decode and
  stream badges shrink, and drop count if they must, so header chrome never
  collides with the `WARP` control.

## 2026-03-29 (background analysis stays in the background)

- **A failed media probe is a toast**, not something that reaches the main loop;
  the same for a waveform analysis that cannot complete.
- Windows waveform analysis drains ffmpeg's PCM output, so it produces peaks.

## 2026-03-29 (a seek holds the picture)

- **A jump keeps the last visible frame until the new one is ready**, so
  transport jumps, progress-bar seeks and `goto -20s/-30s` no longer flash
  black. An explicit hard clear is still available through `seek(..., true)`.

## 2026-03-29 (calmer output display switching)

- **Switching an output's display is one clean move** on multi-monitor setups: a
  single fullscreen re-entry path, no resizing while SDL still reports the
  window fullscreen, and an enabled window output rebuilds its runtime on the
  newly chosen display. The rebuild is queued onto the next update tick, so
  recovery and fullscreen logic do not fight the same monitor move, and teardown
  clears pending transition flags so a fresh output starts clean.

## 2026-03-29 (Windows launches like a normal app)

- **`Deckboy.exe` is a Win32 GUI executable**, so launching it opens the UI and
  no blank terminal. `--version`, `--self-check` and `--smoke` route through the
  same app logic behind a `WinMain` wrapper.

## 2026-03-29 (keyboard focus hygiene)

- **Transport hotkeys belong to the main control window.** An output or
  secondary window receiving a key event does not fire them, though output
  windows still pass `Esc` through so fullscreen escape and emergency disarm
  work from the big screen.
- **Opening an inline editor raises the control window** before starting text
  input, so `Ctrl+G` captures the cue token you type.

## 2026-03-28 (the app is called Deckboy)

- **The built app is `Deckboy` / `Deckboy.exe`**, and GitHub Actions artifacts
  publish as `Deckboy-linux-*`, `Deckboy-macos-*` and `Deckboy-windows-*`. The
  startup dialog, splash overlay and About/settings menu render `Deckboy` with
  the live generated version.

## 2026-03-28 (version flow groundwork)

- **One repo-wide version source of truth**: a top-level `VERSION` file, read by
  CMake, with `deckboy_version.hpp` generated at configure time so native code
  and build metadata stay aligned.
- **`Deckboy --version`** prints the SemVer tag-style version, and
  `--self-check` includes it near the top.
- **GitHub Actions understands release tags**: CI validates that a pushed `v*`
  tag matches the repo `VERSION`, and the Linux, macOS and Windows builds share
  that guard.

## 2026-03-28 (playback fixes + browser backend seam)

- **The timeline filmstrip's last tile samples inside the clip**, and a fully
  built cached strip replaces the placeholder rather than leaving it on screen.
- **Animated engineering patterns update on every output path**: a regenerated
  frame publishes with a fresh index, so motion-enabled patterns and Pocket Test
  variants move.
- **Crosshatch and checkerboard motion loops wrap cleanly**, and
  `crosshatch-motion` drifts on one axis in both preview and output.
- **A new pattern cue defaults to `hold`**, so it is live-safe rather than
  inheriting playlist auto-advance; static checkerboard frames are smoke-tested
  as opaque.
- **The audio lane shows an animated loading state** like the video filmstrip,
  and the focused-deck VU meter decays to zero when a cue stops feeding audio.
- **Browser cue runtime has a real backend seam**: the browser lifecycle and
  phased startup live behind `native/platform/browser.*` rather than inside
  `App`.


## 2026-03-26 (DeckLink SDI output + SRT subtitles)

- **DeckLink SDI output, end to end** (feature-gated by `DECKBOY_HAS_DECKLINK`):
  22 modes — 720p, 1080i, 1080p and 2160p at all standard frame rates — with
  per-output device id, mode and 10-bit selection, saved with the show. The
  compositor captures the egress frame and `DeckLinkOutput::sendFrame()` handles
  the BGRA→UYVY conversion, alongside stream and NDI on the same output.
  Companion commands: `DECKLINK ON/OFF/TOGGLE`, `DECKLINK DEVICE <id>`,
  `DECKLINK MODE <token>`, `DECKLINK 10BIT ON/OFF/TOGGLE`. A stub fallback
  compiles on every platform when the SDK is absent.
- **SRT subtitle rendering**: `core/subtitle_parser.hpp` parses an external
  `.srt`, and an embedded subtitle stream is extracted with
  `ffmpeg -map <streamId> -f srt`. `probeCue()` auto-detects an embedded track
  (`0:s:0`), the track is cached on take, and the output window renders the text
  centred at the bottom with a drop shadow over a semi-transparent bar.
  Companion commands: `SUBTITLE ON/OFF/TOGGLE`, `SUBTITLE FILE <path>`,
  `SUBTITLE CLEAR`. The cue's subtitle path, stream id and enable flag are saved
  with the show.

## 2026-03-26 (Companion module + status snapshot)

- **A Bitfocus Companion module** in `companion/companion-module-deckboy/`:
  - `connection.js` — a TCP client polling `STATUS JSON` on a configurable
    interval
  - `actions.js` — 35 actions covering transport, cue navigation, deck focus,
    seek, volume, blackout, transitions, cue properties, overlays, outputs, NDI,
    streaming, timecode, panic, shuffle, fullscreen, and raw command passthrough
  - `feedbacks.js` — 13 boolean feedbacks: playing/paused/stopped per deck,
    blackout, output health, NDI enabled/receivers, stream enabled, output
    enabled, test card, deck focused
  - `variables.js` — 50+ variables: global state, focused deck
    transport/cue/position/volume/timecode, per-deck (1-4) status, focused
    output health/NDI/stream/FPS
  - `presets.js` — 30+ drag-and-drop button presets by category (Transport, Cue
    Navigation, Deck Selection, Master, Output, Status, Transitions)
- **The status snapshot carries more**: `masterDimmer` (0-100), `blackout`
  (bool) and `masterVolume` (0-200) in both the JSON and text forms, which is
  what powers the blackout feedback and the dimmer and volume variables.

## 2026-03-25 (splash redesign, animation, and readable settings)

- **Splash screen redesign**: full-bleed background art through the new
  `drawUiImageCover()`, with the original framed card (760×430) preserved on top
  over a semi-transparent backing. Boot console and sparkle animations retained.
- **Program monitor animations**: corner sparkles while playing, idle floating
  particles, and a playhead sparkle on the timeline during playback.
- **Bottom bar animations**: a pulsing red border glow on the blackout button
  while it is active, and header sparkles across the full available space.
- **Inspector animations**: an activity sparkle in the header when a cue is
  selected, doubled while playing. Path and URL displays wrap to two lines
  inside their clip.
- **VIDEO and AUDIO timeline labels read in full.**
- **All five settings tabs redesigned for readability**:
  - Every card subtitle has clearance before the first interactive element.
  - System tab: "FI"→"FADE IN", "FO"→"FADE OUT", "AUD"→"AUDIO",
    "P-BEGIN"→"PAUSE BEGIN", "P-END"→"PAUSE END", "NEXT X"→"NEXT TRANSITION";
    toggle rows go from four per row to two; "PLAYLIST PREFS"→"PLAYLIST
    PREFERENCES".
  - Network tab: "NDI TRIG"→"NDI TRIGGER".
  - Video Outputs tab: a two-column rewrite — the dropdowns self-describe
    ("Hardware Display: …", "Resolution: …"), NDI source name becomes a
    dropdown, edge blend labels sit above their buttons, and fullscreen and
    orientation are full-width rows.
  - About tab: runtime info renders in full, with dynamic paths ellipsized.
  - Audio tab: long info text ellipsizes rather than overflowing its card.
- Industry abbreviations (TC, SFX, NDI, OSC) are kept; only truncated words were
  expanded.

## 2026-03-16 (visual overhaul: beveled panels, scanlines, generation themes)

- **The palette is built once and used everywhere**, converted from the theme
  constants at startup and on every theme load, so all ~774 colour references
  draw the theme's own colours.
- **A differentiated default DMG palette**: shell colours (grey-green plastic
  `C4CFA1`, `A5B088`, `5A6B4A`) distinct from the LCD screen colours (classic
  `9BBC0F`/`8BAC0F`/`306230`/`0F380F`), with `inkSoft`, `buttonBezel` and
  `deleteBezel` all their own values.
- **Beveled panel rendering**: panels draw a highlight on the top-left and a
  shadow on the bottom-right, detecting raised versus inset automatically — an
  accent brighter than the fill reads as raised, darker as recessed. No
  call-site changes.
- **Scanline overlay**: a 1×4 procedural texture drawn before each present, for
  a CRT / dot-matrix feel. Controlled by `pal.scanlineAlpha` (0 disables); theme
  key `scanline_alpha`.
- **Game Boy generation themes**: `pocket` (silver-grey LCD), `color` (vivid
  green + indigo shell), `advance` (washed-out + indigo) and `sp` (bright
  backlit + metallic silver).

## 2026-03-16 (responsive cue rows and imports)

- **Importing a large batch of media stays responsive.** `importPaths()` creates
  placeholder cues immediately, usable in the UI, and probes them on background
  threads; a row shows "probing..." until its metadata lands.
- **Cue row display strings are cached** per cue — token, kind label, ellipsized
  name and metadata line — self-invalidating by input comparison, so the text
  measurement loop does not run per row per frame.
- **Formatting allocates nothing per frame**: `formatSeconds()`,
  `formatTimecode()` and the inspector's float formatting use `snprintf`.
- **One waveform lookup helper** (`getWaveformPeaks(path, pending)`) shared by
  every render path.
- The `Cue` struct is grouped by alignment, saving ~40 bytes of padding per cue;
  serialisation is by explicit field name, so the show format is unaffected.

## 2026-03-15 (steadier remote control, one palette, one inspector)

- **Companion/OSC TCP client handling is thread-safe**: the client list is
  snapshotted for `select()` with the lock released while it blocks, then
  re-locked for recv/accept/close, with the timeout at 100ms for
  responsiveness.
- **A pre-converted colour palette** (`Palette pal` + `rebuildPalette()`) is
  built once at startup and after each theme load, and used by every draw site.
- **One inspector implementation.** The docked and floating panels share 15
  helpers parameterised by an `InspectorCtx` (inset, fonts, ellipsize, gaps),
  so a change to a row type reaches both.
- `docs/AUDIT_ROADMAP.md` maps the remaining optimisation and cleanup work.

## 2026-03-15 (one subprocess API across platforms)

- `native/core/subprocess.hpp/cpp` becomes a unified cross-platform API: a
  `SpawnOptions` struct with a `StdioMode` enum (Inherit, Null, Pipe, Merge) and
  a detached mode, one `spawnProcess()` entry point, and convenience presets
  (`pipedStdout()`, `detachedSilent()`, `captureAll()`). The legacy wrappers
  remain as thin forwards, so every existing call site works unchanged.

## 2026-03-15 (Deckboy 0.60 audit + cleanup pass)

- **Every text prompt uses the inline editor**: `Ctrl+G` cue goto, cue
  renumbering (from settings and `Ctrl+Shift+R`), MIDI port, Companion/OSC port,
  OSC Query port, OSC feedback rate, Art-Net port, canvas size, and browser-cue
  creation.
- **Deck auto-advance is gone from the model as well as the UI**, so the
  keyboard and the interface agree about what a deck does. Old `auto_advance`
  fields still load harmlessly.
- **The unfinished overlay/scene authoring surfaces are parked**: `LOWER 3RD`,
  `SCENE` and `PIP` come out of the bottom `MEDIA` group, and `G`, `M`,
  `Shift+P` and the remote add commands say so. Existing `Lower Third`, `PIP`
  and `Composite` cues still load, inspect, save and render.
- Portability conclusion for this pass: cross-platform work is realistic without
  an architecture rewrite; the remaining blockers are the Unix-only
  subprocess/FIFO paths, the Linux-only browser and source capture backends, and
  the Windows/macOS runtime backends.

## 2026-03-15 (Deckboy 0.60 branch, first composite cue cut)

- A real `Composite` cue kind in the project model and the save format, with
  `SCENE` in the bottom `MEDIA` group and `M` to add one.
- A composite cue stores its layout preset (`2-UP`, `70/30`, `QUAD`), a
  per-slot source spec, a per-scene audio slot selection and a scene background
  colour.
- A dedicated composite inspector: `PLAYBACK` for hold, duration, fades and end
  action; `SCENE` for layout presets, slot source entry and audio-slot cycling;
  `OVERLAYS` for attached `Lower Third` / `PIP` bin items.
- The first rendering pass is deliberately bounded: taking a `Composite` cue
  shows an authored scene placeholder in Program, Preview and Output. Live slot
  rendering from media, browser and source runtimes is the next phase.

## 2026-03-15 (Composite cue architecture spec)

- An engineering spec for the `Composite` cue in
  [docs/COMPOSITE_CUE_SPEC.md](docs/COMPOSITE_CUE_SPEC.md), recommending
  `Composite` over a generic live layer system for Deckboy's
  single-primary-cue architecture, and covering the cue data model and slot
  model, runtime and render integration, audio and transport rules, inspector
  and monitor-editing behaviour, and rollout phases with explicit non-goals.

## 2026-03-13 (copy and paste cue settings, longer default fades)

- **Settings copy/paste for cue work**: `Ctrl+C` copies the selected cue's
  playback, geometry and key settings, `Ctrl+V` pastes them onto the selection
  while each cue keeps its own media, name and identity. The inspector summary
  card carries visible `COPY` / `PASTE` buttons.
- **Warp copy/paste**: `Ctrl+Shift+C` copies the focused deck's warp and blend
  state, `Ctrl+Shift+V` pastes it back, with `COPY` / `PASTE` buttons beside
  `SAVE` in the warp editor.
- **Naming a warp preset uses the inline editor.**
- **New decks and cues default to a 1.5s cue fade** (up from 0.5s), so a fade
  reads as a fade.

## 2026-03-13 (attached overlays + self-contained PIP sources)

- **Overlays are reusable bin items plus per-cue attachments**: a primary cue
  has an `OVERLAYS` inspector section and can attach one `Lower Third` and one
  `PIP` from the `OVERLAY BIN`. Attached overlays fire on TAKE only, and stay
  out of the main cue's next/loop sequencing.
- **PIP carries its own source**: `Media File / Still`, `Browser URL`, `Window
  Source`, `Camera Source` or `Syphon/Spout Source`. Legacy cue-linked PIP cues
  still load and are shown as `Legacy Cue Link`, and a live PIP resolves from
  the configured source.
- **Firing an overlay by hand uses the same runtime path** as an attached one,
  so taking a `Lower Third` or `PIP` from the bin replaces the live overlay of
  that kind rather than stacking duplicates.

## 2026-03-13 (overlay bin split, PIP presets, sequencing)

- **The rundown and the overlay bin are separate columns.** `Lower Third` and
  `PIP` cues live in the `OVERLAY BIN`, which stays hidden until an overlay cue
  exists; each column has its own mouse-wheel scroll.
- **Main cue sequencing skips overlay-only cues** — next badges, keyboard
  next/prev and cue-end auto-advance all stay on the main list.
- **PIP controls are direct**: `SET TARGET CUE` at the top of the `PLAYBACK`
  section, with corner presets (`TL / TR / BL / BR`) and size presets
  (`SM / BIG / 70/30`) before any manual geometry.

## 2026-03-13 (cue row controls, inspector and bottom bar)

- Empty states in the cue inspector render inside proper framed cards; the
  program monitor `OUTPUT / DECODE / STREAM` pills are wide enough for their
  numbers; and the `WARP` and `-30 / -20 / -10` transport buttons have roomier,
  aligned labels.
- **The bottom action bar reworked**: the floating `Source` / `Pattern`
  selectors are gone, the section labels (`MEDIA`, `TRANSPORT`, `OUTPUT`) sit
  inside their group panels, and a `LOWER 3RD` button joins `IMPORT`, `SOURCE`
  and `PATTERN`.
- **Per-cue playback state lives in the playlist row**: icon toggles for fade
  in, fade out, loop, hold on last frame and cue audio.
- **Source-cue type selection lives in the inspector**, so the footer needs no
  separate source-kind selector to create window, camera or Syphon/Spout cues.
- Loop, hold and fade toggles apply consistently across still, source, browser,
  pattern and lower-third cues as well as video and audio.
- Follow-up on the same pass: larger `MEDIA / TRANSPORT / OUTPUT` footer tiles;
  telemetry pills that split label from value; `Clear` drops active Lower Third
  overlays immediately; Lower Third cues are edited in the inspector (`title`
  and `sub`); and the `System`, `Audio` and `Network` tabs are reorganised —
  theme and UI feedback under `System → Appearance`, audio device selection
  under `Audio`, and network/integration controls in larger cards.
- **A first real `PIP` overlay cue**: `PIP` in the bottom `MEDIA` group and on
  `Shift+P`, pushed into the overlay stack like a Lower Third but running its
  own silent media engine, with a target cue token editor plus geometry, colour
  and key controls, and the target's own thumbnail in the preview.
- `System → Appearance` shows `UI MOTION` as always-on feedback rather than an
  on/off toggle, and older projects load with motion normalised back on.


## 2026-03-12 (NMC transport sync runtime)

- **A live NMC transport sync backend** behind the existing `NMC` adapter
  toggle: a UDP transport/locate bridge with one active mode at a time. Input
  mode listens for transport and locate packets and applies them to the focused
  deck; output mode broadcasts play, pause, stop and locate updates from it.
- Runtime controls: `DECKBOY_NMC_MODE=input|output`, `DECKBOY_NMC_PORT`,
  `DECKBOY_NMC_HOST` (output target), `DECKBOY_NMC_SOURCE` (sender filter) and
  `DECKBOY_NMC_LOCATE_MS` (rolling locate cadence).
- `--self-check` reports `nmc-sync-runtime`, and route planning reports
  `nmc[ok]`.

## 2026-03-12 (NDI metadata trigger runtime)

- **An NDI metadata trigger backend**: `libndi` is loaded at runtime rather than
  needing SDK headers at build time, and a lightweight receive bridge listens
  for metadata frames on a discovered source. Accepted metadata routes into the
  existing remote-command path as `NDIEVENT`, so it is handled exactly like a
  Companion, OSC or ATEM command.
- Conservative parsing: raw Deckboy command text, common XML forms with
  `command` / `cmd` / `action` / `event` attributes or elements, and `cue`,
  `goto` and `group` XML attributes as `GOTO ...` and `GROUP ... FIRE`
  shortcuts.
- `--self-check` reports `ndi-trigger-runtime: ok/missing`, and route planning
  reports `ndi-trigger[ok]`. `DECKBOY_NDI_TRIGGER_SOURCE` constrains the bridge
  to one source name; `DECKBOY_NDI_LIB` overrides the runtime library path.

## 2026-03-12 (LTC ingest)

- **An LTC ingest backend**: `libltc` is loaded at runtime, capture comes from
  the default SDL audio input, and decoded LTC feeds the existing timecode chase
  and trigger path — emitted internally as `LTCEXT`, the same ingest route MTC
  quarter-frame decode uses.
- `--self-check` reports `ltc-runtime: ok/missing`, and route planning reports
  `ltc[ok]`. `DECKBOY_LTC_LIB` overrides the library path and
  `DECKBOY_LTC_DEVICE` names a specific capture device.

## 2026-03-12 (bundled show export)

- **BUNDLE** on the toolbar, and `Ctrl+Shift+E`: exports a new `.deckboy` plus a
  sibling `<show>_media/` folder, with the copied media rewritten to relative
  cue paths so the show moves as one thing.
- File-backed cues resolve against the current project folder for decode,
  thumbnails, preview and waveform analysis, so a bundled show does not depend
  on the original absolute paths.

## 2026-03-10 (SRT stream stability)

- **A stalled or unlistened stream never holds the UI.** Child stream processes
  launch through explicit pipes, video writes happen on a dedicated stream
  writer thread rather than the render thread, startup and shutdown keep
  Deckboy's own control and listener sockets out of the ffmpeg children, and a
  missing listener or a reconnect surfaces as retry/recovering state.
- The local SRT loopback workflow is spelled out: `OUTPUT ON` arms the output,
  `STREAMING: ON` starts network egress for it, and local viewing uses an
  external SRT listener such as `ffplay`.

## 2026-03-10 (portability prep + docs refresh)

- The top-level CMake prefers exported `SDL2` / `SDL2_ttf` config packages and
  falls back to pkg-config or a manual lookup, and macOS feature-gated framework
  linking goes through a real helper.
- `native/core/paths.cpp` resolves executable locations on Linux, macOS and
  Windows; sans and mono font lookup includes the macOS and Windows system font
  locations alongside project-local overrides; the subprocess layer keeps
  Unix-only members and headers behind conditionals; and socket send helpers
  tolerate a platform with no `MSG_NOSIGNAL`.
- `PORTABILITY.md` documents executable-root lookup on all three platforms and
  the current build and runtime readiness.

## 2026-03-06 (Phase 4 inline editing + floating panel workspace)

- **Panel presentation and visibility persist** on top of the Phase 1 workspace
  model.
- **A `PANELS` control row in the status strip**: `PGM[D/F]`, `INS[D/F]`,
  `OUT[D/F/H]`, `RTG[D/F/H]`, `MSC[D/F/H]`.
- **A secondary `Deckboy Panels` floating workspace window** for popped-out
  operational panels, with a per-panel `DOCK` return control.
- **Panel-local focus badges** so focused Deck / Output / Cue state is visible
  in panel headers and cue rows, not only in the global strip.
- **The inline editor replaces every remaining operational text prompt**: custom
  output raster, output refresh, output canvas size, canvas view offset, stream
  URL, stream bitrate, output alpha, output delay, NDI name, NDI key name, cue
  goto target, cue notes, browser URL and cue ID.
- Safe-text cleanup continues through the quit confirmation, startup dialog,
  splash overlay headings, About tab, and the video output advanced and routing
  headers.

## 2026-03-06 (Phase 3 workflow polish + selector cleanup)

- **Every operational selector is a non-blocking dropdown**: audio output
  device, output display, stream protocol and mirror source. The old blocking
  list picker is gone from the live UI flow.
- **Program / Transport reads clearly**: explicit `CURRENT` and `NEXT`, and
  labelled `TRANSPORT`, `TIMELINE` and `REMAIN`.
- **One next-cue rule** shared by the Program summary and the deck playlist
  rows.
- The default workspace gives `Program / Transport` more room, and a text-safe
  pass covers the Decks window tracker columns, playlist headers and rows,
  Master Scene programmer and list rows, Program / Preview labels, cue thumbnail
  placeholders and details footer, the settings modal title, tabs and output
  summary, and dropdown popovers — long labels ellipsize rather than colliding.

## 2026-03-06 (Phase 2 operational panel split)

- The control UI splits into explicit panels rather than one shell:
  - `Deck Playlist` for the focused deck
  - `Program / Transport`, with the current and next cue summaries, the focused
    Deck / Output route summary, the Program and Preview monitors, progress and
    remaining time, and the stack view
  - `Cue Inspector` as its own singleton panel
  - `Output Panels` as a repeating operational panel list
  - `Routing Matrix` as a singleton operational panel rather than a
    Preferences-only surface
  - `Master Scene` in the right-hand operational column
- Reusable panel chrome through `drawOperationalPanel(...)`, with docked and
  floating panel instances recording their real frames.
- Scrollable, non-blocking views for `Output Panels` and `Routing Matrix`, and
  Output-panel controls for focus, recover, disarm and the FPS toggle.
- The Program panel uses its full width now that the Inspector is its own panel.
- No playback, routing, OSC, Companion, shortcut or output-safety behaviour
  changed.

## 2026-03-06 (workspace foundation: panel registry + persistence)

- A panel/workspace foundation — `UiPanelDefinition`, `UiPanelState`,
  `UiPanelManager`, `UiWorkspaceState`, `UiFocusState` — with logical panel
  kinds registered for Program / Transport, Preview, Cue Inspector, Routing,
  Master Scene, Preferences, Deck Playlist and Output.
- Always-visible workspace and focus summary lines in the operational strip:
  `WORKSPACE ...` and `FOCUS: DECK ... | OUTPUT ... | CUE ...`.
- Workspace save/load at `data/deckboy.workspace`: panel visibility,
  presentation (docked / floating / modal), frames, control and decks window
  geometry, focused panel, focused Deck / Output / Cue context, and the last
  Master Scene sidebar and programmer state. It loads during init, applies
  window geometry and focus, and auto-flushes from the update loop — with no
  change to the show file format, and a serialisation smoke test.

## 2026-03-06 (operator terminology normalization)

- Terminology normalised across the live UI and the docs: `Master Scene` →
  `Master Cue`, the `MASTER SCENES` sidebar → `MASTER CUES`, `Create Standard` →
  `Create Window`, `Camera` / `Syphon/Spout` → `Camera Source` /
  `Syphon/Spout Source`, "lower-third / graphic" → `Lower Third`, and
  "Decks panel" / "tracker window" → `Decks window`.
- The Master Cue sidebar reads consistently: an `MC#` focus badge, `<MC` / `MC>`
  nav buttons, a `TAKE` fire button, and a `Master Cue Name` rename prompt.
- Compatibility aliases and protocol identifiers are unchanged: the `GROUP` and
  `SCENE` command aliases still work, and `.deckboy`, `DECKBOY_*`, `/deckboy/*`
  and `deckboy-native` stay as they are.

## 2026-03-05 (audio inspector metadata section)

- The audio cue inspector matches the section model: `PLAYBACK` for transport
  and audio behaviour, `METADATA` for tag, notes, cue id and pause points, with
  `ROUTING` below. Loop, hold and end rows use the shared panel helpers, and
  pause points render in the aligned metadata row style.

## 2026-03-05 (inspector metadata sections for lower-third, browser and source cues)

- Lower-third cues use boxed `PLAYBACK` and `METADATA` sections;
  still, pattern, browser and source cues split playback controls from metadata
  and source rows, in the same aligned panel style as the rest of the inspector.
- Shared inspector row helpers for message/info rows, edit rows, status rows,
  action rows and tag rows.
- Lower-third `CLEAR OVERLAY` is a real clickable action.

## 2026-03-05 (inspector sections, routing alignment, control styling)

- **Scoped inspector section cards** for `PLAYBACK`, `GEOMETRY`, `KEY` and
  `ROUTING`, with consistent collapse affordances, boxed grouping, and a shared
  row style with aligned labels and +/- controls.
- **Routing controls use a compact table row layout**, and the output-strip
  routing rows use `UITable` alignment for the deck label, output selector,
  layer selector and assign/link action.
- **Control styling**: bottom-bar buttons take a stronger top band and adaptive
  title font sizing, and dropdowns and buttons share one panel treatment and
  safer text rendering.

## 2026-03-05 (grid layout cleanup: safer spacing, a clearer control window)

- Reusable layout and safety primitives: `VerticalLayout`, `HorizontalLayout`,
  `GridLayout`, `UITable`, `drawTextSafe(...)`, and shared `drawUIPanel(...)`,
  `drawUIButton(...)` and `drawUIDropdown(...)`.
- The main control window snaps to an 8px grid: panel padding `16`, panel gap
  `12`, chunky `2px` framing, a shared bottom-bar button height of `40`, and a
  compact `56` global header.
- The header splits into title / output+TC / controls zones, the content area
  reserves space for selectors and bottom controls before laying out columns,
  and deck header, footer and cue row text ellipsizes inside its bounds.
- The bottom bar takes consistent-width buttons, keeps the `MEDIA / TRANSPORT /
  OUTPUT` grouping, and simplifies its labels to `IMPORT / SOURCE / PATTERN /
  TAKE / STOP / PLAY / CLEAR / PREFS`.
- The program area gets a clearer title/time/progress hierarchy, a single
  monitor frame, and stack view and inspector spacing on the new constants.

## 2026-03-05 (output activation: explicit health, recover and disarm)

- **An explicit per-output health model**: `OFF`, `ARMED`, `LIVE`,
  `RECOVERING`, `ERROR`, each carrying the reason it last changed.
- **Output chips show it**: the state token comes from the health model rather
  than ad-hoc stream flags, with the reason inline on the chip and the focused
  output still highlighted.
- **Per-chip controls**: `REC` recovers or re-arms that output in one click;
  `OFF` disarms it.
- Health transitions are wired through fullscreen enable and recovery, stream
  start, write and audio failures, an unavailable NDI runtime or sender, and the
  escape-to-windowed path, which reports as armed with a reason. Pressing `ON`
  again on a stream output performs a real egress restart.
- Status snapshots expose it: `health=` plus optional `health_reason="..."` in
  text `STATUS`, and `health` / `healthReason` per output in `STATUS JSON`.

## 2026-03-05 (toggleable per-output FPS counter)

- Per-output FPS measurement with rolling sampling, an `FPS ON/OFF` toggle in
  the output strip, and an `xx.xfps` readout on each output chip that updates
  continuously while outputs render.

## 2026-03-05 (browser diagnostics, cue-panel refactor, regression smoke)

- **Live browser startup diagnostics** in the cue panel: a `state` row reading
  `starting xvfb`, `starting browser`, `starting capture`, `live`, or
  `failed: <reason>`, with concise reasons stored in the deck runtime (`url
  missing`, `browser not found`, `xvfb launch failed`, `browser launch failed`,
  `capture start failed`).
- Shared helpers for labelled value, edit and status rows replace the duplicated
  row blocks for `source`, `url` and `notes`.
- New smoke regression checks: the Decks window visibility policy (one deck
  hidden unless manual, two or more visible), the transport transition
  source-gain policy that keeps a stop or take from flashing black, and the
  browser status summary label mapping.


## 2026-03-05 (browser cue take/capture reliability)

- Browser capture goes through the same platform capture planner as source and
  window capture, so a browser cue's ffmpeg arguments match the known-good
  `x11grab` path.
- If browser capture cannot start, the startup sequence stops and toasts
  `browser capture failed` rather than leaving the cue black.

## 2026-03-05 (browser cue URL editing in the cue panel)

- **A browser cue's URL is edited in the cue panel**, in the same right-side
  settings flow as still, pattern and source cues: a `url` row with an `edit`
  button, accepting a URL or a local file path and normalising it to a
  browser-safe URL.
- Editing the URL of the live cue reloads it, so the change is on screen
  immediately.

## 2026-03-05 (transport continuity: no black flash)

- `STOP` rewinds while holding the visible frame until frame 0 is decoded, and a
  `TAKE` from a paused or stopped deck keeps its transition source at full — so
  neither flashes the output black. `seek(...)` preserves the visual frame
  through the decoder restarts that stop and rewind use.

## 2026-03-05 (Decks window visibility)

- The Decks window starts hidden for a single-deck show and opens itself when a
  show crosses to two decks. The header `decks` button toggles it open and
  closed, and closing it keeps it closed.
- Manual open state resets on `New` and `Open`, after which visibility is
  re-evaluated from the deck count, and the renderer does no work while the
  window is hidden.

## 2026-03-05 (UI de-clutter + non-blocking dropdowns)

### A responsive control path
- `DECKBOY_UI_PROFILE=1` adds UI-thread timing and popup watchdog logs: frame
  timing when `dt > 50ms`, segmented event/update/layout/render timings, popup
  open and close logs with item counts, and popup render counts.
- The `PATTERN` and `SOURCE` button actions run immediately, with no blocking
  dialog anywhere in the live control path.

### A reusable non-blocking dropdown
- Click to open; close on outside click, `Esc` or selection; mouse wheel
  scrolling; keyboard navigation (`Up/Down/Enter/Esc`); type-to-filter with
  `Backspace`; clipped visible-row rendering. No nested modal event loops.

### Where it lands
- The bottom bar carries `Source: … v` and `Pattern: … v` default selectors;
  the `PATTERN` button and `P` add the selected default pattern directly, and
  `SOURCE` adds using the selected default source type
  (`window`/`camera`/`syphon|spout`).
- In the cue settings panel, pattern type and transition style become dropdowns,
  and source cues get in-menu `source` editing through the inline text editor,
  with human-friendly prompts and plain aliases — `focused`/`recommended` for a
  window, `default` for camera and Syphon — resolved per cue type across a
  multi-select.
- Optional external UI art pack support: `data/ui/deckboy_ui_pack_v3` preferred,
  falling back to `v2`, covering header art, output-chip backgrounds, cue-type
  icons, the monitor frame and the splash image. Mascot art stays on the splash.

### Layout
- The bottom bar is grouped into `MEDIA`, `TRANSPORT` and `OUTPUT`, the main
  layout reserves its space so content panels never overlap the controls, and
  compact selector chips sit above the bottom controls.

## 2026-03-05 (output activation stability)

- Display-mismatch recovery is evaluated only when the output window is
  non-fullscreen and its display index is valid, so arming an output does not
  cycle through fullscreen tear-down and re-apply on some SDL and display-driver
  combinations.
- Anti-thrash gating: non-fullscreen auto-recovery triggers only shortly after an
  explicit fullscreen request, hidden/minimised/wrong-display recovery stays
  active, and attempts are throttled.
- The display-apply path has a non-transition mode that preserves fullscreen
  state, which recovery uses; and enable, recover and fullscreen actions check
  whether fullscreen was actually entered and toast when it was not.

## 2026-03-05 (integration runtime: ATEM bridge, MTC ingest, Art-Net triggers)

- **A live ATEM UDP trigger bridge**: a listener on UDP `9910` by default
  (`DECKBOY_ATEM_BRIDGE_PORT` overrides), enqueuing inbound payloads into the
  remote command path as `ATEMEVENT ...`. Supported payloads include `CUT`,
  `AUTO`, `TAKE`, `PLAY`, `STOP`, `NEXT`, `PREV`, `CLEAR`, `PANIC`,
  `SCENE <n>` and `DECKBOY <command>`.
- **A live Art-Net trigger bridge**: a listener on the configured `artNetPort`
  parsing `ArtDMX` packets and edge-triggering `ARTNETEVENT ...`. Default
  channel map: ch1 `TAKE`, ch2 `PLAY`, ch3 `STOP`, ch4 `GO`, ch5 `NEXT`, ch6
  `PREV`, ch7 `CLEAR`, ch8 `PANIC`, with ch9 `TAKE <value>` and ch10
  `GROUP <value> FIRE` on value changes.
- **MTC quarter-frame ingest** through ALSA: the MIDI loop decodes
  `SND_SEQ_EVENT_QFRAME` to `MTCEXT <seconds> <fps>`, applied to chase-enabled
  decks and otherwise the focused deck.
- The `INTEGRATION ADAPTERS` panel on the Network tab shows the ATEM and Art-Net
  UDP ports, and an Art-Net port edit restarts the listener at runtime.

## 2026-03-05 (integration adapter foundation)

- An integration backend planning module
  (`native/platform/integration_backend.hpp/.cpp`) with a catalog API and a
  route planner.
- Backward-compatible project fields and keys for ATEM, NDI trigger, NMC, MTC,
  LTC and Art-Net, plus the Art-Net port.
- `Settings → Network → INTEGRATION ADAPTERS` with direct toggles for all six,
  an Art-Net port prompt and an all-adapters quick toggle.
- Commands `ATEM`, `NDITRIGGER`, `NMC`, `MTC`, `LTC`, `ARTNET`, `ARTNETPORT`
  and `INTEGRATIONS`, with OSC endpoints `/atem`, `/ndi/trigger`, `/nmc`,
  `/mtc`, `/ltc`, `/artnet`, `/artnet/port` and `/integration`.
- `STATUS` and `STATUS JSON` carry an integration route summary, the OSC mirror
  publishes `/deckboy/integration/*`, and `--self-check` prints
  `integration-backends` and `integration-route-defaults`.

## 2026-03-05 (runtime egress route wrappers)

- Output egress runs through the backend route planner: a stream send happens
  only when the route includes a supported `stream` backend, an NDI send only
  with a supported `ndi` backend, and stream runtime stops when stream routing
  is unsupported or inactive.
- Output status snapshots carry the route: `backend=...` in text `STATUS` and
  `backendRoute` in `STATUS JSON`.

## 2026-03-05 (capture/output backend planning APIs)

- `native/platform/capture_backend.*` grows executable planning interfaces —
  `SourceCaptureRequest`, `SourceCapturePlan`, a `SourceCaptureBackend` factory
  set and `planSourceCapture(...)` — with Linux backends (`x11grab` window
  capture, `v4l2` camera capture, a desktop-fallback app-texture path) and
  explicit macOS and Windows scaffold plans carrying reason strings.
  `MediaEngine::buildSourceCaptureArgs(...)` delegates to the planner.
- `native/platform/output_backend.*` grows route planning —
  `OutputBackendRouteRequest`, `OutputBackendRoutePlan`,
  `planOutputBackendRoute(...)` — describing the active backend chain for
  `window` / `stream` / `ndi` / `decklink`.
- `--self-check` reports `capture-plan-defaults` and `output-route-defaults`.

## 2026-03-05 (official Stream Deck + Companion profile package)

- A profile docs bundle: `docs/streamdeck/README.md`,
  `docs/streamdeck/deckboy_companion_profile_map.json` and
  `docs/streamdeck/deckboy_main_page.csv`. The JSON manifest is the canonical
  Deckboy key map for Stream Deck through Bitfocus Companion
  (`Generic TCP/UDP`), grouped into Main transport, Deck focus, Output control
  and Master Cue control pages.
- The README's and MANUAL's Companion sections link straight to it.

## 2026-03-05 (warp mode: linear vs perspective)

- **`Deck.warpMode`**, `linear` (default) or `perspective`, serialised
  append-only so older show files load as `linear`.
- The output present path honours the mode: `linear` keeps the quad-geometry
  path, `perspective` uses tessellated projective UV mapping
  (`renderPerspectiveWarp`) for proper corner-pin behaviour. Orientation and
  edge blend are unaffected.
- `Video Outputs → Advanced` gains `Mode Linear` / `Mode Perspective`, with
  `VIDEO WARP MODE LINEAR|PERSPECTIVE|NEXT|PREV` and the direct aliases
  `VIDEO WARP LINEAR|PERSPECTIVE`. The status toast reports enable state and
  mode.
- Deck status carries `warp_mode`, `STATUS JSON` carries `warpMode`, and the OSC
  mirror publishes `/deckboy/deck/<n>/warp_mode`.

## 2026-03-05 (output parity: span/duplicate, orientation, test cards)

- `OutputTarget` gains `outputLayoutMode` (`span` | `duplicate`),
  `outputOrientationDegrees` (`0/90/180/270`) and `outputTestCardEnabled`,
  serialised append-only with defaults for older shows.
- Window presentation honours per-output orientation; `span` uses the host
  deck's canvas view offsets when canvas mode is on, `duplicate` locks to origin
  (`0,0`); and a per-output test-card feed renders in the compositor path. The
  egress capture path (NDI, stream, delay) captures the same view region and
  applies orientation before sending.
- The Video Outputs tab gains `Span` / `Duplicate`, a `Rotate
  0°/90°/180°/270°` cycle, `Test Card ON/OFF` for the focused output and
  `All Cards ON/OFF`, with commands
  `VIDEO OUTPUT LAYOUT SPAN|DUPLICATE|NEXT|PREV`,
  `VIDEO OUTPUT ORIENTATION 0|90|180|270|NEXT|PREV|RESET` and
  `VIDEO OUTPUT TESTCARD ON|OFF|TOGGLE|ALL ON|OFF`.
- `STATUS` / `STATUS JSON` expose `layout`, `orientation` and `test_card`, and
  the OSC mirror publishes `/deckboy/output/<n>/layout`, `/orientation` and
  `/testcard`.

## 2026-03-05 (multi-select inspector parity)

- **A dedicated multi-selection inspector mode**: common controls with `mixed`
  labels where values conflict, grouped Playback / Geometry / Key / Routing
  sections, and compatibility masking when the selection mixes cue kinds.
- **Every quick-action edit applies to the whole selection**: trim in/out and
  clear, cue timecode mark set and clear, loop / hold / pause begin / cue audio /
  transition-to-next toggles, fade in and out, transition duration and style,
  end action, geometry (scale mode, scale X/Y, offsets, rotation, crop), key
  (enable, colour, tolerance, softness), colour (brightness, contrast,
  saturation, hue), lower-third alpha, still duration, repeats and playback
  speed.
- Mixed selections resolve to the first eligible cue, so a toggle still works
  when the focused cue is not the compatible one.

## 2026-03-05 (OSC Query + OSC feedback mirror)

- **An optional OSC Query HTTP server**: `/` as a lightweight endpoint browser,
  `/oscquery.json` for endpoint docs plus live state, and `/state.json` for the
  live status payload.
- **An optional canonical OSC feedback mirror**: value-based `/deckboy/deck/*`
  and `/deckboy/output/*` updates to subscribed senders, with a configurable
  rate limiter (40–2000 ms, default 120 ms). The existing `/deckboy/state` JSON
  feedback is retained.
- The Network tab controls all four, a Companion/OSC port change restarts the
  listener immediately, and the commands are `OSCQUERY ON|OFF`,
  `OSCQUERYPORT <port>`, `OSCFEEDBACK ON|OFF` and `OSCFEEDBACKRATE <ms>`, with
  matching OSC addresses.
- Persisted as `osc_query_enabled`, `osc_query_port`, `osc_feedback_mirror` and
  `osc_feedback_rate_ms`, clamped to a valid port and 40–2000 ms.

## 2026-03-05 (playlist preferences: deck-level timebase and defaults)

- **Per-deck playlist preferences**, persisted: playlist timebase FPS (24, 25,
  29.97, 30), playlist start timecode offset, default cue fade duration, default
  non-movie duration, and the default new-cue toggles — loop, fade in, fade out,
  audio, pause begin, pause end and transition-to-next.
- A `PLAYLIST PREFS` block in `Prefs → System` for the focused deck: an edit
  dialog for timebase, start, fade and still defaults, direct toggles for the
  new-cue flags, and an inline summary showing SMPTE base, start TC and default
  timings.
- Every new-cue path applies them — media import, and browser, source,
  lower-third and pattern cue creation — so a long playlist behaves
  predictably.

## 2026-03-05 (capture/output backend catalogs)

- Platform backend catalogs (`native/platform/capture_backend.*` and
  `output_backend.*`) report the planned backend families per platform: window
  capture (Linux `x11grab`, macOS ScreenCaptureKit, Windows DXGI), camera
  capture (Linux `v4l2`, macOS AVFoundation, Windows Media Foundation) and
  app-texture transport (Syphon/Spout); and for output, SDL window, FFmpeg
  stream, NDI (SDK-gated) and DeckLink (feature-gated).
- `--self-check` prints `capture-backends` and `output-backends`, so backend
  readiness on all three platforms can be audited in one place.

## 2026-03-05 (cue metadata, toggles and deck opacity)

- **New cue fields**, append-only in the show format: `cue_id` (a short
  operator-facing ID, up to 6 characters, normalised uppercase), `audio
  enabled`, `pause at beginning`, `transition to next` and `goto target`.
- **Deck-level playlist fader fields**: `playlist opacity` (0–100%),
  `playlist auto fade` and `playlist fade seconds`.
- Runtime: `pause at beginning` loads without autoplay on take; `transition to
  next` decides whether auto-advance and goto use transition timing or a cut;
  `goto target` resolves by cue token (`cue_id`, cue number or name token) at
  end of cue; `audio enabled` gates the audio decode path, so a muted cue runs
  video-only; and deck playlist opacity multiplies the deck's contribution in
  the compositor, with an optional fade-to-target animation.
- Cue rows show the operator's cue token (`cue_id`, then cue number, then
  index). Multi-select arrives: `Shift+click` for a range, `Ctrl/Cmd+click` to
  toggle, and batch apply for notes, cue id, loop/hold, fades, colour tag and
  the parity toggles. The deck footer gains an opacity rail (click or drag, with
  `Alt+click` snapping to 0% or 100%), and video cues gain `pause in`, `audio`,
  `next xfade` and `goto` inspector rows.
- Commands: `PAUSEBEGIN`, `PAUSEEND`, `CUEAUDIO`, `NEXTTRANS`, `CUEGOTO`,
  `CUEIDSHORT`, `PLAYLISTOPACITY` / `DECKOPACITY` / `DECKDIM`,
  `PLAYLISTAUTOFADE` / `DECKAUTOFADE`, `PLAYLISTFADE` / `DECKFADE`, with OSC
  paths for each.

## 2026-03-05 (UI clarity: header, stack, routing table, splash)

- The header carries a compact per-deck live summary (`D1 LIVE ...`) alongside
  the show file and Companion/TC state.
- **Output chips scan faster**: index, target type and armed/live state, a
  stronger highlight on the focused output, and a one-click inline `ON/OFF`.
- **The deck column header** shows `Deck N`, the routed output and layer token,
  and the audio device label.
- **Cue rows use fixed scan columns** — cue token / type token / cue name /
  duration-state — truncating with a hover tooltip for long names.
- **The program monitor** shows focused output info (output, raster, refresh),
  with a `STACK VIEW (Output X)` beneath it listing deck and layer occupancy top
  to bottom, and a chunkier progress bar carrying its time text.
- **The cue settings panel** gains grouped, collapsible `Playback`, `Geometry`,
  `Key` and `Routing` headers, plus routing controls (output prev/next, layer
  +/-, assign/unassign).
- **The Video Outputs routing table is editable inline**: `Deck | Output | Layer
  | Assigned`, with per-row output prev/next, layer +/- and a link/unlink
  toggle, wired to the existing assignment logic.
- **Master cue rows are two lines**: the indexed scene name above, the deck slot
  summary below, and a larger `TAKE` on the right.
- **A launch splash overlay** with boot messages and `press ENTER to start`,
  skippable with `Enter`, `Esc` or a click. Character art stays out of the
  operational UI, and the About tab uses text and logo runtime info only.
- `DEVNOTES.md` documents the layout component map, palette tuning points, the
  cue-type icon hook and the routing table action wiring.

## 2026-03-05 (output FX: alpha, delay, overlay, colour space)

- **Focused-output FX controls** in the Video Outputs tab: `Overlay ON/OFF` (an
  output-scoped time/ID overlay), `Alpha` (0–100% output dimmer), `Delay`
  (0–5000 ms) and `Color` (`AUTO`, `BT709`, `SRGB`), plus a one-tap
  `Delay +100`.
- Per-output alpha applies as a post-composite dimmer; the output-scoped overlay
  toggles independently of the deck-local one; and a per-output delayed frame
  queue feeds the egress paths — NDI and stream send the delayed-or-live frame
  while window presentation stays immediate.
- The stream encoder applies colour metadata from the output's colour-space
  mode: `BT709` → bt709 matrix/primaries/trc, `SRGB` → bt709 matrix/primaries
  with the `iec61966-2-1` transfer.
- Commands `VIDEO OUTPUT ALPHA|DELAY|OVERLAY|COLORSPACE ...`, with alpha
  percent, delay ms, overlay state and colour-space token in both status
  snapshots and persisted on the output target.

## 2026-03-05 (live source cues)

- **`Window Source` cues capture live** through ffmpeg `x11grab` on Linux, and
  **`Camera` cues** through `v4l2`. `Syphon/Spout` cues run through the source
  transport path, using desktop capture on Linux while the native backends are
  planned.
- **Source cues take part in normal transport**: `Take` with autoplay starts
  live capture, `Play` starts or restarts it, `Pause` parks capture and holds
  the frame, `Stop` parks capture and restores the source slate. Deck transport
  status shows `Live Source` / `Source Ready`.
- Cue-row hover tips describe source cues as live cues, and toasts distinguish
  source-loaded, source-live and source-unavailable.

## 2026-03-05 (output operations flow + source cue scaffold)

- **Explicit creation actions in Video Outputs**: `Create Standard` (window
  output) and `Create Stream`, with direct `Set Window` / `Set Stream` buttons
  and a focused-output signal-flow line reading deck layer stack → output →
  display.
- **New cue kinds** — `Window Source`, `Camera`, `Syphon/Spout` — created
  through one shared path: the `SOURCE` button and
  `Preferences → System → Add Source Cue...`, the commands `SOURCE WINDOW|CAMERA
  |SYPHON ...` with the aliases `WINDOWSOURCE`, `CAMERACUE`, `SYPHONCUE` and
  `SPOUTCUE`, the persistence tokens `window_source`, `camera` and `syphon`, and
  OSC mappings for each. They render through a common generated placeholder
  frame, with transport, routing, save/load and status on the normal cue flow.

## 2026-03-05 (NDI is per-output)

- **NDI is output-scoped end to end.** The `N` key, the `NDI ...`, `NDINAME` and
  `NDIKEY...` commands and the Video Outputs actions all resolve through the
  focused output target, and the output runtime sends fill video, an optional
  key video stream, and mixed stereo audio for that output's assignment stack.
- A migration shim maps legacy deck NDI settings onto output NDI during project
  normalisation where output NDI is not already configured, and the legacy deck
  fields stay in the save format for older shows.
- NDI status is reported under output entities only, and `MANUAL.md` and
  `README.md` describe NDI as per-output, with examples that focus an output
  first (`VIDEO OUTPUT <n>`, then `NDI ...`).

## 2026-03-05 (UI declutter + output workflow clarity)

- The no-cue control state is text-only: `Insert cartridge`, `Drop media here`,
  `Press A to take cue`.
- In Video Outputs, `Routing In Main Strip` is informational rather than
  clickable, and a `Show Advanced` / `Hide Advanced` toggle keeps the dense
  refresh, depth, canvas and warp controls out of the way until wanted.
- The Decks window reads as `Deckboy Decks`, with a `DECKS` header over the deck
  list and playlist view.

## 2026-03-05 (demo show generator + layout presets)

- `scripts/generate_demo_shows.sh` writes repeatable demo shows into
  `data/demos/`:
  - `demo_70_30_4pip_bg_5deck.deckboy` — a 5-deck show with a full background
    and four right-column PiPs, with master cues `Open - BG + 4 PiP`,
    `BG Only` and `PiP Motion Sweep`
  - `demo_quad_2x2_4pip_bg_5deck.deckboy` — a 5-deck show with a full background
    and a 2x2 PiP quad, with master cues `Quad Open` and `Quad Motion`
  - `demo_program_preview_clean_3deck.deckboy` — a 3-deck show with a program
    background, preview PiP and corner bug, with master cues
    `Program + Preview + Bug`, `Program + Bug` and `Program Clean`

## 2026-03-05 (safety, output quality, master-cue speedups)

- **A single-instance lock** at startup, so a second launch exits with a clear
  terminal message. `--allow-multi-instance` bypasses it deliberately.
- **Output quality goes auto-native on arm and re-arm**: enabling a `window`
  output switches global output sizing to display-native when fixed mode was
  active, and repeating `VIDEO OUTPUT ON` applies the same path before recovery.
  Toasts say `auto native`.
- **Master-cue programming without a popup**: clicking a programmer row outside
  its buttons assigns the slot from that deck's selected cue, and the mouse
  wheel over a row cycles the slot cue directly.

## 2026-03-05 (panic timing, cue find, timecode follower)

### Panic timing
- `Preferences → Audio` gains `Panic fade (sec)` and `Panic auto restore`, and
  the panic fade profiles use the configured duration. The dimmer restores
  automatically after a panic deck action when enabled. Commands:
  `PANICFADE <seconds>` and `PANICAUTORESTORE ON|OFF|TOGGLE`.

### Cue find and renumber
- `FIND <token>`, `FINDNEXT`, `FINDPREV`, `FINDTAKE <token>`, plus
  `RENUMBER [prefix] [start]` and `RENUMBER CLEAR` (aliases `AUTOID`,
  `CUEAUTOID`).
- Cue token matching understands `FIRST`, `LAST`, `NEXT`, `PREV`, `SEL`, `ACT`,
  relative offsets (`+N`, `-N`) and cue-number prefixes.

### Timecode follower: jam and freewheel
- Per-deck persistent `timecodeJamSyncEnabled` and `timecodeFreewheelSeconds`,
  with menu-first controls in `Preferences → Audio` for the focused deck, the
  commands `TIMECODE JAM ON|OFF` and `TIMECODE FREEWHEEL <seconds>`, and the OSC
  mappings `/timecode/jam i` and `/timecode/freewheel f`.
- A chasing, running deck holds after the freewheel timeout when external TC
  stops; with `TC JAM OFF`, incoming TC inside the freewheel window does not
  re-jam the running clock; and `TIMECODE SET ...` remains a forced operator
  jam.

### Status and the Audio tab
- Text status carries the panic profile, fade and restore state and per-deck
  `tc_jam` and `tc_freewheel_s`; JSON status carries the same as top-level panic
  fields and per-deck `timecodeJam` and `timecodeFreewheelSeconds`.
- `Preferences → Audio` is grouped into `System + UI`, `Playback Semantics`,
  `Safety + Timecode Follower` and `Cue Tools (menu-first)`, the last carrying
  `Find Cue...`, `Next`, `Prev`, `Find+Take`, `Renumber...`, `Clear Numbers` and
  `Clear Find`.
- Find state has its own commands and endpoints: `FINDCLEAR` / `FINDRESET`,
  `FINDSTATUS`, the `CUEFIND*` aliases, `STATUS CUES` / `STATE CUES` and
  `STATUS FIND` / `STATE FIND`, with find token and cursor metadata and per-deck
  `selected_num`, `selected_id`, `active_num` and `active_id` in the payloads,
  and OSC mappings `/find`, `/find/next`, `/find/prev`, `/find/take`,
  `/find/clear` and `/renumber`.


## 2026-03-04 (playback semantics)

### Jump mode and panic profiles
- `Preferences → Audio` gains `Jump Mode` (`Trigger` or `Load`), `Jump
  Transition` (`ON`/`OFF`), `Panic Profile` (`Outputs Off`, `Fade+Pause`,
  `Fade+Rewind`, `Fade+LoadNext`) and a `Run Panic` button, all persisted in the
  show file as `jump_mode`, `jump_transition` and `panic_profile`.
- `Take` and `Goto` run through jump-mode semantics: `Trigger` jumps the
  selected cue live, `Load` loads it without autoplay. Jump Transition decides
  whether a jump uses cue and deck transition timing or cuts.
- The panic profiles execute properly: `Outputs Off` disarms every output, and
  the fade profiles arm a dimmer fade and then run the deck action — pause,
  rewind, or load next.
- Commands: `JUMPMODE TRIGGER|LOAD|TOGGLE`, `JUMPTRANSITION ON|OFF|TOGGLE`,
  `PANICPROFILE <name>|NEXT|PREV` and `PANIC` (or `PANIC <profile>` for a
  one-shot override).

## 2026-03-04 (a bigger, clearer decks workspace)

- One operating flow, no mode split.
- The companion deck window opens at `1560x920` (minimum `1260x700`), in Game
  Boy green with star accents, with larger tracker rows, deck-name width, deck
  cards, per-deck header blocks and transport buttons, and larger cue rows in
  each playlist card.
- The main transport button reads `Play/Pause`, and a deck card carries `Take`
  and `Stop` without a competing `Go`.
- Master-cue slot programming is inline: `-` / `+` cycle the cue per deck slot,
  and middle or ctrl interactions cycle cues rather than opening a picker.

## 2026-03-04 (an always-on master-cue sidebar)

- **The master-cue sidebar is a permanent panel**, wider, with a fixed
  `Master Cues` badge in the output strip marking where it lives.
- **Direct sidebar actions**: `Name` renames the focused master cue, `CapSel`
  captures each deck's selected cue into it, and `CapAct` captures each deck's
  active cue. The expandable programmer (`Prog+` / `Prog-`) keeps a row per deck
  with `Sel`, `Act`, `Byp`, `-` and `+`.
- **The `Deckboy Decks` window reads more easily**: larger tracker and header
  sizing, expanded labels (`selected`, `active`, `layer`, `state`, `timecode`),
  bigger deck-card headers, buttons and cue rows, and an interaction hint
  ("click cue to select, right-click cue to take").
- **Changing a focused output's display goes auto-native**, switching video
  sizing from fixed raster to display-native and saying `auto native` in the
  toast.
- **The Pocket Test scene expands**: an original platform-adventure vibe with an
  automatic scene cycle (`pocket-test`) and selectable variants (`pocket-day`,
  `pocket-sunset`, `pocket-night`, `pocket-storm`), plus procedural animated
  creatures — a crab, a jumping fish, a parrot, a turtle, a dino-style enemy and
  a puff friend — alongside the explorer, coin line and signal strip.

## 2026-03-04 (deck/output separation + stream outputs)

### Outputs never take a screen by surprise
- Window outputs default to `OFF` in new projects and new outputs, output
  runtime windows are created hidden, and a loaded show opens disarmed — so a
  saved output-on state cannot take over a screen at launch.
- `Preferences → Video` gains an `Enabled` toggle for the focused output, which
  fullscreens it on the selected display, plus `Prev` / `Next` / `Rescan`
  display assignment with a live display label. `F` enables the focused window
  output if it is off. The command is `VIDEO OUTPUT ON|OFF|TOGGLE`.
- **Emergency escape**: `Esc` exits fullscreen on the active output window,
  falling back to any fullscreen output sharing the control window's display,
  and marks that output intentionally windowed so auto-recovery leaves it alone.
  Three presses within ~0.9s triggers panic disarm (`outputs off`). Re-arming is
  explicit — `F` on that output, or `VIDEO OUTPUT ON` again.
- **Recovery**: `recoverWindowOutputIfNeeded(outputIndex)` re-applies display
  placement, raises a hidden or minimised window and re-asserts fullscreen on
  the target display; repeating `VIDEO OUTPUT ON` performs it, `F` re-asserts
  fullscreen rather than dropping to windowed, and a 1 Hz background poll
  recovers enabled window outputs that drift off target.
- **Display topology refresh**: monitor hot-plug is detected and outputs
  re-clamped and re-applied automatically; manual `Rescan` runs the same path. A
  `Connected Displays` list shows what is detected, and clicking a row assigns
  the focused output to it.

### Routing you can read
- Route labels are plain English — `Deck 1`, `Output 2`, and route values
  `None`, `Background` or `Layer N` — with no shorthand codes.
- Layer edits are direct in the Route Editor: `-` / `+`, `Ctrl` for a ×10 step,
  `Shift` to reverse, and explicit `Assign` / `Unassign`, with no blocking
  popup.
- **A Deck × Output routing matrix** in the Video tab: click row labels to focus
  decks, column headers to focus outputs, and cells to assign. A `MOVE` /
  `ADD` toggle switches between single-output routing and fan-out, and an
  assigned cell nudges its layer directly (`click +1`, `Shift+click -1`, `Ctrl`
  for ×10).
- The main window carries an `Add Output` button and focused-route controls —
  `Link` / `Unlink`, `Layer-` / `Layer+` — with a plain status line:
  `Focused Route: Deck N -> Output N  Background/Layer N/Not Linked`.

### Per-output network streaming (SRT + RTMP)
- An ffmpeg-backed stream output per `OutputTarget`, with focused-output
  controls in the Video tab for enable, protocol (`SRT` / `RTMP`), URL and
  bitrate, and the commands `VIDEO STREAM ON|OFF|TOGGLE`,
  `VIDEO STREAM SRT|RTMP`, `VIDEO STREAM URL ...` and
  `VIDEO STREAM BITRATE ...`.
- The stream muxes H.264 video with AAC stereo audio, and the audio follows the
  output assignment stack, falling back to the host deck when there are no
  assignments.
- `OutputTarget` gains `outputType` (`window` or `stream`),
  `mirrorSourceOutputIndex`, `streamEnabled`, `streamProtocol`, `streamUrl` and
  `streamBitrateKbps`, with protocol normalisation, bitrate clamping
  (500..50000 kbps) and a default URL per output index and protocol.

### Master cues
- Group presets become operator-facing **Master Cues**: default names
  `Master Cue 1`, `Master Cue 2`, …, with `MASTER` / `MASTERCUE` command
  aliases alongside the existing `GROUP` commands, and toasts and prompts that
  speak in master-cue terms.
- A persistent `GroupPreset` + `GroupSlot` model — one slot per deck (`cueId` or
  bypass), with the focused index saved in the show — driven by `GROUP ADD`,
  `ADDEMPTY`, `SELECT`, `NAME`, `DELETE`, `SET <deck> <cue-token|SEL|ACTIVE|
  BYPASS>`, `BYPASS`, `CAPTURE SEL|ACTIVE` and `FIRE [index]`.
- Keyboard: `Ctrl+Shift+G` fires the focused preset, `Ctrl+Shift+N` creates one
  from the selected cues, and `Ctrl+Shift+[` / `]` cycle the focus.
- Master-cue rows show each deck's slot inline and fire from the row, and slot
  assignment happens in the window — click to assign, `Shift+click` for the
  active cue, middle or `Ctrl+click` for a picker, right-click to bypass — with
  no external dialog.

### Deck window and main window
- The main control window is output- and program-first; the separate decks
  window carries the deck playlists, per-deck transport (`Take`, `Go`, `Stop`)
  and per-deck header state — transport status, `tc` value and fps, and the
  chase/free and run/hold flags. The main panel shows the focused deck's number,
  name and timecode line.
- The deck tracker pages around the focused deck rather than always starting at
  deck 1, and reserves height for the playlist grid so lower deck columns stay
  visible with many decks.
- The decks window is always shown, at a larger default and minimum size.

### File controls
- `New`, `Open`, `Save` and `SaveAs` in the main header. `Save` writes the
  active show path immediately, and `SaveAs` writes as soon as a path is chosen.

### Frame-accurate trim
- `I` sets trim-in at the playhead and `O` sets trim-out, both snapped to cue
  frame boundaries using the cue's fps. `Shift+I` opens the import picker and
  `Shift+O` toggles the time overlay.
- Paused nudge: `Left/Right` for ±1 frame, `Shift` for ±5, `Ctrl` for ±10, and
  `Alt` for ±1.0 second, frame-snapped.

### Pattern workflow
- A `Pattern` button opens an in-app type picker, and a pattern cue's settings
  gain a `pattern` row: `-` / `+` cycles the base type and the centre toggles
  motion where the type supports it.
- Animated variants for the engineering patterns: `smpte-bars-motion`,
  `crosshatch-motion`, `checkerboard-motion` and the `full-*-motion` set, with
  the animation loop rebuilding for any animated type.
- Commands: `PATTERN SET <type>`, `PATTERN LIST` and the `PATTERN <type> MOTION`
  shorthand.

### Pocket Test
- Reworked into a deterministic tropical retro scene: sky, ocean and beach
  gradients, procedural palms and an island silhouette, an animated coin line
  and a retro explorer sprite, over the bottom signal reference strip.

### Quit and close
- `SDL_QUIT` exits immediately, closing the main window exits, closing the Decks
  window hides it cleanly, and closing an output window disarms that output.

### Geometry precision and status
- The output path applies independent X and Y scaling and respects the cue's
  scale mode (`Fit`/`Fill`/`Stretch`/`Unscaled`).
- `off X` / `off Y` step by 1px, and `scale X`, `scale Y`, `off X`, `off Y` and
  `rot` accept typed values, including simple expressions with `+`, `-`, `*`,
  `/` and parentheses.
- Status carries the output entities: `OUTPUT ...` rows in text `STATUS`
  (`type`, `host`, `display`, `layers`, `mirror`, stream state, url and
  bitrate), and `focusedOutput`, `outputCount` and an `outputs[]` array in
  `STATUS JSON`.
- The `Deckboy Decks` tracker takes a denser LSDJ-inspired pass: compact rows
  for every deck in view, `sel` and `act` cue-number columns, and a focused-deck
  highlight.

---

## March 2025 — the foundations

Deckboy began as a single-file prototype and was rebuilt into the modular
application these notes describe. That first pass established what everything
since has been built on:

- **A modular architecture**: eight logical modules — core, media, render,
  control, ui, platform, ndi, browser — with public APIs, a CMake compilation
  strategy, feature gates and a documented dependency graph.
- **`native/core/utils`**: 55 shared utilities — string operations, timecode
  parsing, colour conversion, field parsing, JSON escaping — with no external
  dependencies, underneath every other module.
- **`native/core/subprocess`**: one place for spawning and managing FFmpeg and
  capture processes.
- **Professional broadcast features**, each behind a feature gate with a
  graceful stub when its SDK is absent: cross-platform MIDI input through
  RtMidi, DeckLink 10-bit SDI output, Syphon and Spout texture sharing, and
  native browser rendering.
- **Cross-platform infrastructure**: platform abstraction, feature gates and
  GitHub Actions CI building on Linux, macOS and Windows.
- **GPL compliance and licensing** put in order at the same time.

Build it with `cmake .. && make -j4`, and `./deckboy-native --self-check`
reports what this machine can do:

```
deckboy-native self-check
project-root: "..."
font-sans: ok
font-mono: ok
font-pixel: ok
ffmpeg: ok
ffprobe: ok
ndi-sdk: not built (set DECKBOY_NDI_SDK or install SDK headers)
ui-sfx: enabled by separate SDL audio device when available
companion-control: tcp/udp port 5510 by default
```

Optional features are switched on at configure time —
`-DENABLE_MIDI=ON`, `-DENABLE_SIPHON=ON`, `-DENABLE_SPOUT=ON`, and
`-DENABLE_DECKLINK=ON -DDECKLINK_SDK=/path/to/sdk`.

