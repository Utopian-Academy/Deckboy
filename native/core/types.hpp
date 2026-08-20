// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// types.hpp — Central domain model for the entire Deckboy application.
//
// Every major subsystem depends on this file:
//   - MediaEngine (engine/media_engine.*) reads Cue fields for decode config
//   - Project save/load (app/app_project_state.ipp) serializes all structs here
//   - UI rendering (app/app_render_*.ipp) reads struct fields for display
//   - Companion/OSC (app/app_remote_command.ipp) maps remote commands to fields
//   - Output compositor (app/app_render_output.ipp) reads OutputTarget + Deck
//
// Struct layout notes: fields are ordered by alignment (8→4→1 byte) to
// minimize padding on both MSVC and GCC/Clang. Do NOT reorder fields
// without understanding alignment implications.
//
// Serialization: all structs are persisted to tab-delimited .deckboy files.
// Adding a new field requires updating BOTH saveProject() and loadProject()
// in app/app_project_state.ipp, with a backwards-compat guard on field count.
// ============================================================================

#ifndef DECKBOY_CORE_TYPES_HPP
#define DECKBOY_CORE_TYPES_HPP

#include "core/sdl_compat.hpp"
#include <memory>
#include <string>
#include <vector>
#include "constants.hpp"

// Domain types for the cue deck. SDL_Color/SDL_Rect used for UI integration.

// ---------------------------------------------------------------------------
// CueKind — Discriminator for what a cue represents and how it is decoded.
//
// MediaEngine uses this to choose the ffmpeg pipeline (or skip decode entirely
// for patterns). The UI uses it to show kind-specific inspector fields.
// Save/load writes it as an integer index — never reorder existing values.
// ---------------------------------------------------------------------------
enum class CueKind {
  Video,         // file-based video (decoded by ffmpeg pipe)
  Image,         // single still image (loaded as one decoded frame)
  Pattern,       // procedurally generated (bars, gradient, etc.) — no ffmpeg
  Browser,       // CEF/WebKit page rendered to texture (platform/browser.*)
  WindowSource,  // desktop window capture (platform/capture_backend.*)
  Camera,        // live camera input (v4l2 / dshow / avfoundation)
  Syphon,        // macOS Syphon / Windows Spout texture sharing
  SrtStream,     // live stream input (srt://, rtmp://, rtsp://, udp://)
  NdiSource,     // NDI receive input (ndi://SOURCE_NAME)
  Pip,           // picture-in-picture composite (references another cue)
  LowerThird,    // text overlay with optional background bar
  Composite,     // multi-slot layout (quad-split, side-by-side, etc.)
  Audio,         // audio-only cue (no video output)
  Timer          // stage/speaker countdown, generated like a pattern but with
                 // its own transport-linked state (see TimerState)
};

// Stage/speaker timer settings, per cue. Ported from the owner's SpeakerTimer
// (C#/WPF) -- see docs/TIMER_PLAN.md. Thresholds are SECONDS REMAINING, so
// amber 60 means "turn amber with a minute left".
// Timer display mode. Matches the three stagetimer.io offers, because they are
// the three a show actually needs: how long is left, how long you have run,
// and what time it is now.
enum class TimerMode {
  Countdown,   // duration -> 0, then overtime
  CountUp,     // 0 -> duration
  TimeOfDay,   // wall clock
};

// Which face the clock is drawn with. Seven-segment is the default because it
// never depends on installed fonts; the others use the UI faces already loaded.
// Which face the clock is drawn with. Both are BUNDLED GEOMETRY, not TTFs: the
// frame is built in the engine, which has no access to the app's fonts, and a
// stage screen must render identically wherever it runs regardless.
enum class TimerFace {
  SevenSegment,  // LED-panel look, chunky, maximum legibility at distance
  Blocky,        // 5x7 dot-matrix, squarer and more retro
};

struct TimerSettings {
  int durationSeconds = 300;    // 5:00
  int amberSeconds = 60;        // <= this many left: amber
  int redSeconds = 15;          // <= this many left: red
  bool countUpAfterZero = true; // keep counting as +m:ss instead of stopping
  bool blinkAtZero = true;      // flash once time is up
  TimerMode mode = TimerMode::Countdown;
  TimerFace face = TimerFace::SevenSegment;
  bool showProgressBar = true;  // length is readable from further back than digits
  bool messageIsUrgent = false; // red rather than white: the "wrap up NOW" state
  std::string message;          // optional line under the clock
};

// What happens when a cue reaches its end. "Inherit" defers to the deck-level default.
// Used by MediaEngine to decide post-playback behavior and by the cue list UI to
// show the end-action badge icon. Serialized as integer index.
enum class CueEndAction { Inherit, Stop, Loop, PauseOnLast, AutoNext };

// Current transport state of a deck's active cue. Drives the play/pause/stop
// buttons in app_render_control.ipp and the MediaEngine decode loop.
enum class TransportState {
  Stopped,   // no decode running; preview shows freeze frame or black
  Paused,    // decode paused; last frame held on screen
  Playing    // active decode; frames streaming from ffmpeg
};

// How one cue transitions into the next. Applied at the deck level
// (Deck::transitionStyle) or overridden per-cue (Cue::cueTransitionStyle).
// The output compositor in app_render_output.ipp blends layers accordingly.
enum class TransitionStyle {
  Cut,        // instant switch — no blending
  Crossfade,  // gradual alpha blend between outgoing and incoming
  DipBlack    // fade out to black, then fade in the new cue
};

// How a cue's source frame maps to the output resolution.
// Per-cue (Cue::scaleMode) and per-composite-slot (CompositeSlot::scaleMode).
// The output renderer reads this to compute the destination rect.
enum class ScaleMode {
  Fit,         // letterbox: fit entire image, maintain aspect ratio
  Fill,        // fill screen and crop, maintain aspect ratio
  Stretch,     // fill screen, ignore aspect ratio (distort)
  Unscaled     // 1:1 pixel mapping (no scaling)
};

// ---------------------------------------------------------------------------
// CompositeSlot — One sub-region inside a Composite cue layout.
//
// A Composite cue contains 1–4 CompositeSlots, each pointing to a media
// source and positioned in normalized coordinates (0–1) relative to the
// output frame. The compositor in app_render_output.ipp iterates these
// slots to blit each source into its designated rectangle.
// ---------------------------------------------------------------------------
struct CompositeSlot {
  std::string id;                           // unique slot identifier (UUID)
  std::string name;                         // operator-facing label ("Slot 1")
  std::string sourceType = "media";         // "media" | "camera" | "ndi" etc.
  std::string source;                       // file path, device name, or NDI source
  bool visible = true;                      // false = skip during composite render
  bool audioEnabled = false;                // route this slot's audio to output mix
  ScaleMode scaleMode = ScaleMode::Fit;     // how source maps into the slot rect
  float normX = 0.0f;                       // left edge (0–1 fraction of output width)
  float normY = 0.0f;                       // top edge  (0–1 fraction of output height)
  float normW = 0.5f;                       // width     (0–1 fraction of output width)
  float normH = 0.5f;                       // height    (0–1 fraction of output height)
};

// ---------------------------------------------------------------------------
// Cue — A single playback item in a deck's cue list.
//
// This is the central content unit. Each Cue holds everything needed to:
//   1. Decode media  → MediaEngine reads path, kind, fps, duration, codec info
//   2. Render output → compositor reads scale/offset/crop/rotation/chroma/color
//   3. Drive transport → end action, loop, pause points, speed, fade in/out
//   4. Serialize → all fields saved/loaded in app/app_project_state.ipp
//   5. Display in UI → name, colorTag, notes shown in cue list + inspector
//
// Fields are grouped by alignment to minimize struct padding.
// ---------------------------------------------------------------------------
struct Cue {
  // -- 8-byte aligned: strings -----------------------------------------------
  std::string id;                          // internal UUID (generated on import)
  std::string cueId;                       // operator-facing short ID (max 6 chars, shown in cue list)
  std::string path;                        // file path, URL, or device for media source
  std::string name;                        // display name (defaults to filename on import)
  std::string formatName;                  // ffprobe container format (e.g. "mov,mp4")
  std::string videoCodec;                  // ffprobe video codec name (e.g. "h264")
  std::string audioCodec;                  // ffprobe audio codec name (e.g. "aac")
  std::string gotoTarget;                  // cue ID to jump to on AutoNext end action
  std::string cueTransitionStyle;          // per-cue override: "cut"/"crossfade"/"dipblack" (empty=inherit)
  std::string lowerThirdText;              // primary text line for LowerThird cue kind
  std::string lowerThirdSubtext;           // secondary text line for LowerThird cue kind
  std::string pipTargetCue;               // cue ID whose output is the PiP background
  std::string pipSourceType;              // PiP source kind ("media"/"camera"/"ndi")
  std::string attachedLowerThirdCue;      // cue ID of an attached lower-third overlay
  std::string attachedPipCue;             // cue ID of an attached PiP overlay
  std::string compositeLayoutPreset;      // preset name ("2up"/"quad"/"7030") for Composite cue
  std::string compositeAudioSlotId;       // which CompositeSlot's audio to route to output
  std::string colorTag;                   // operator color label for cue list ("red","blue",etc.)
  std::string notes;                      // free-form operator notes shown in inspector
  std::string cueNumber;                  // traditional show cue number (e.g. "Q1.5")
  std::string subtitlePath;               // path to external .srt file (empty = use embedded)
  std::string subtitleStreamId;           // embedded subtitle stream index (e.g. "0:s:0")

  // -- 8-byte aligned: vectors ------------------------------------------------
  std::vector<CompositeSlot> compositeSlots; // sub-regions for Composite cue layout
  std::vector<double> pausePoints;           // timecodes (seconds) where playback auto-pauses

  // -- 8-byte aligned: doubles + uint64 ----------------------------------------
  double duration = 0.0;                   // total media duration in seconds (from ffprobe)
  double fps = 30.0;                       // frame rate (from ffprobe; default 30 for stills)
  double fadeInSeconds = 0.0;              // visual+audio fade-in duration at cue start
  double fadeOutSeconds = 0.0;             // visual+audio fade-out duration before cue end
  double inPointSeconds = 0.0;            // trim: playback starts here (0 = beginning)
  double outPointSeconds = 0.0;           // trim: playback ends here (0 = use full duration)
  double triggerTimecodeSeconds = -1.0;   // SMPTE timecode to auto-trigger this cue (-1 = disabled)
  double stillDurationSeconds = 0.0;      // display time for Image/Pattern/Browser cues
  double cueTransitionSeconds = -1.0;     // per-cue transition duration override (-1 = inherit)
  double playbackSpeed = 1.0;             // speed multiplier (0.25–4.0; 1.0 = normal)
  std::uintmax_t sizeBytes = 0;           // file size in bytes (from ffprobe, for display)

  // -- 4-byte aligned: floats -------------------------------------------------
  // Geometry transforms applied by the output compositor (normalized/degrees)
  float outputScaleX = 1.0f;              // horizontal scale (1.0 = 100%)
  float outputScaleY = 1.0f;              // vertical scale   (1.0 = 100%)
  float outputOffsetX = 0.0f;             // horizontal offset (fraction of output width)
  float outputOffsetY = 0.0f;             // vertical offset   (fraction of output height)
  float outputRotationDegrees = 0.0f;     // clockwise rotation in degrees
  float cropLeft = 0.0f;                  // crop fraction from left edge   (0–1)
  float cropRight = 0.0f;                 // crop fraction from right edge  (0–1)
  float cropTop = 0.0f;                   // crop fraction from top edge    (0–1)
  float cropBottom = 0.0f;               // crop fraction from bottom edge (0–1)
  // Chroma key (green-screen removal) parameters
  float chromaKeyTolerance = 60.0f;       // color distance threshold for key
  float chromaKeySoftness = 20.0f;        // edge softness gradient width
  // Color correction (applied per-pixel in the output compositor)
  float brightness = 1.0f;                // 0.0 (black) to 2.0 (overbright)
  float contrast = 1.0f;                  // 0.0 (flat gray) to 2.0 (high contrast)
  float saturation = 1.0f;                // 0.0 (grayscale) to 2.0 (hyper-saturated)
  float hueShift = 0.0f;                  // -180 to +180 degrees hue rotation

  // -- 4-byte aligned: ints + enums -------------------------------------------
  int width = 0;                           // source video width  (pixels, from ffprobe)
  int height = 0;                          // source video height (pixels, from ffprobe)
  int audioChannels = 0;                   // number of audio channels (from ffprobe)
  int audioSampleRate = 0;                 // audio sample rate in Hz (from ffprobe)
  int lowerThirdBgAlpha = 180;             // background bar opacity for LowerThird (0–255)
  int loopCount = 0;                       // number of times to loop (0 = infinite when loop=true)
  CueKind kind = CueKind::Video;           // discriminator — see CueKind enum above
  CueEndAction endAction = CueEndAction::Inherit; // what to do when playback finishes
  ScaleMode scaleMode = ScaleMode::Fit;    // how source maps to output — see ScaleMode enum

  // -- 4-byte aligned: SDL_Color (RGBA) ----------------------------------------
  SDL_Color color {48, 98, 48, 255};                 // cue list row tint (DMG green default)
  SDL_Color compositeBackgroundColor {18, 24, 18, 255}; // background fill for Composite cue
  SDL_Color chromaKeyColor {0, 255, 0, 255};          // target color for chroma key removal

  // -- 1-byte aligned: bools ---------------------------------------------------
  bool hasAudio = false;          // true if ffprobe detected an audio stream
  bool audioEnabled = true;       // operator toggle — mute this cue's audio
  // Per-cue gain trim in dB, applied live in the audio thread. Range is
  // kCueAudioGainMinDb..kCueAudioGainMaxDb — never hardcode it at a clamp site.
  float audioGainDb = 0.0f;
  float audioPan = 0.0f;          // stereo balance: -1 full left .. +1 full right (0 = center)
  bool audioMono = false;         // downmix this cue to mono (mono sources / mono PA)
  // Independent audio fades: -1 = follow the visual fadeIn/OutSeconds
  // (default), 0 = no audio fade, >0 = explicit seconds. Lets audio duck
  // early under a long video tail, or hold under a fast visual cut.
  float audioFadeInSeconds = -1.0f;
  float audioFadeOutSeconds = -1.0f;
  // Which pair of the deck audio device's outputs this cue's (post gain/pan/
  // mono) stereo lands on: 0 = outs 1-2, 1 = outs 3-4, ... Pairs beyond the
  // device's opened channel count clamp back to 1-2 at play time.
  int audioOutputPair = 0;
  bool loop = false;              // loop playback (respects loopCount if > 0)
  bool pauseAtBeginning = false;  // load cue paused on first frame (wait for manual play)
  bool pauseOnLastFrame = false;  // hold last frame instead of going to black
  bool transitionToNext = true;   // allow deck-level transition when this cue ends
  // Datamosh: play the prepared copy with keyframes withheld, so P-frames drag
  // the previous picture along their motion. moshPath is the prepared file
  // (Encoder tab -> Datamosh preset); empty means this cue has not been
  // prepared and the toggle should say so rather than silently doing nothing.
  // Timer cue settings. Only meaningful when kind == CueKind::Timer.
  TimerSettings timer;

  bool datamoshEnabled = false;
  std::string moshPath;
  bool chromaKeyEnabled = false;  // enable chroma key removal in the compositor
  bool subtitleEnabled = true;    // render subtitles (if subtitle track available)
  bool refreshOnTake = false;     // Browser cue: reload page each time cue is taken
  // Runtime-only (never serialized): set by scanProjectMediaPresence() when a
  // file-backed cue's media can't be found on disk. Drives the MISSING row
  // badge and the toolbar RELINK button.
  bool mediaMissing = false;
};

// ---------------------------------------------------------------------------
// Deck — A playlist of cues with transport state and output configuration.
//
// The application supports multiple decks (Project::decks). Each deck:
//   - Owns its own cue list and selection/active indices
//   - Has independent transport (play/pause/stop via app_cue_transport.ipp)
//   - Routes to one or more OutputTargets (via outputRouteDeckIndex or direct)
//   - Maintains its own timecode chase state (for external TC-triggered playback)
//   - Has warp/edge-blend geometry for projection mapping (per-output)
//   - Carries playlist-level defaults that apply to newly imported cues
//
// The "active" cue is what's currently on-air; "selected" is the UI cursor.
// overlayActiveIndices holds indices of cues playing as overlays (PiP, L3rd).
// ---------------------------------------------------------------------------
struct Deck {
  std::string name = "Deck 1";            // operator-facing deck label
  std::vector<Cue> cues;                  // ordered cue list for this deck
  int selectedIndex = -1;                 // UI cursor position (-1 = nothing selected)
  int activeIndex = -1;                   // currently playing/on-air cue (-1 = none)
  std::vector<int> overlayActiveIndices;  // overlay cues currently composited on top

  // -- Playlist behavior ------------------------------------------------------
  bool playlistLoop = false;               // wrap around to first cue after last
  bool shuffle = false;                    // randomize next-cue order
  float playlistOpacity = 1.0f;            // 0.0–1.0 deck contribution to final mix
  bool playlistAutoFade = false;           // auto-fade deck opacity in on take
  double playlistFadeSeconds = 0.8;        // duration of deck auto-fade
  double playlistTimebaseFps = 30.0;       // SMPTE display base (24/25/29.97/30)
  double playlistStartOffsetSeconds = 0.0; // timecode offset for playlist start
  // Defaults applied to newly imported cues in this deck:
  double playlistDefaultCueFadeSeconds = 1.5;           // default fade in/out duration
  double playlistDefaultStillDurationSeconds = 8.0;     // default hold time for stills
  bool playlistDefaultLoop = false;                     // default loop setting
  // New clips import with fades OFF; the operator turns them on per cue
  // (cue-row icon / inspector fade rows) or flips these deck defaults in
  // Settings → Show Flow. Old show files keep their saved values.
  bool playlistDefaultFadeInEnabled = false;            // apply fade-in to newly imported cues
  bool playlistDefaultFadeOutEnabled = false;           // apply fade-out to newly imported cues
  bool playlistDefaultAudioEnabled = true;              // enable audio by default
  bool playlistDefaultPauseAtBeginning = false;         // pause on load by default
  bool playlistDefaultPauseAtEnd = true;                // hold last frame by default
  bool playlistDefaultTransitionToNext = true;          // allow transitions by default

  // -- Multi-selection (for batch operations in the cue list UI) ---------------
  std::vector<int> selectedIndices;

  // -- Audio + output routing --------------------------------------------------
  std::string audioOutputDeviceName;       // SDL audio device name (empty = system default)
  // Channels to open the deck's audio device with (2/4/6/8). Cues route
  // their stereo onto a pair of these outs (Cue::audioOutputPair). When the
  // physical device has fewer channels, SDL folds the extra pairs down.
  int audioOutputChannels = 2;
  int outputDisplayIndex = 0;              // which display to open the output window on
  int outputRouteDeckIndex = -1;           // route this deck's output to another deck's window (-1=own)

  // -- NDI output (per-deck; also configurable per-OutputTarget) ---------------
  bool ndiEnabled = false;                 // enable NDI send for this deck
  std::string ndiSourceName;               // NDI source name visible on the network
  bool ndiKeyEnabled = false;              // enable NDI key (alpha) output
  std::string ndiKeySourceName;            // NDI key source name

  // -- Canvas viewport (for multi-output canvas mode) --------------------------
  int canvasViewX = 0;                     // viewport X offset in canvas pixels
  int canvasViewY = 0;                     // viewport Y offset in canvas pixels

  // -- Warp geometry (projection mapping per-output) ---------------------------
  bool warpEnabled = false;                // enable warp mesh
  std::string warpMode = "linear";         // "linear" (bilinear) | "perspective" (4-corner pin)
  float warpTopLeftX = 0.0f;              // corner offsets (normalized 0–1, relative to output)
  float warpTopLeftY = 0.0f;
  float warpTopRightX = 0.0f;
  float warpTopRightY = 0.0f;
  float warpBottomRightX = 0.0f;
  float warpBottomRightY = 0.0f;
  float warpBottomLeftX = 0.0f;
  float warpBottomLeftY = 0.0f;

  // -- Edge blending (for multi-projector soft-edge overlap) -------------------
  float edgeBlendLeft = 0.0f;             // blend gradient width from left   (0–1)
  float edgeBlendRight = 0.0f;            // blend gradient width from right  (0–1)
  float edgeBlendTop = 0.0f;              // blend gradient width from top    (0–1)
  float edgeBlendBottom = 0.0f;           // blend gradient width from bottom (0–1)

  // -- Overlays ----------------------------------------------------------------
  bool timeOverlayEnabled = false;         // show time/ID overlay on this deck's output

  // -- Deck-level transition defaults ------------------------------------------
  double transitionSeconds = 0.0;          // default transition duration for this deck
  std::string transitionStyle = "crossfade"; // "cut" | "crossfade" | "dipblack"

  // -- Timecode chase (external SMPTE timecode drives cue triggering) ----------
  bool timecodeChaseEnabled = false;       // arm timecode chase mode
  bool timecodeRunEnabled = false;         // timecode is actively running (set by ingest)
  bool timecodeTriggerEnabled = true;      // allow cues to auto-fire on TC match
  bool timecodeJamSyncEnabled = true;      // re-sync on TC discontinuity
  double timecodeFreewheelSeconds = 1.0;   // freewheel duration after TC dropout
  double timecodeFps = 30.0;              // TC frame rate for SMPTE display
  double timecodeCurrentSeconds = 0.0;    // latest received timecode value
  double timecodeLastSeconds = 0.0;       // previous frame's TC (for delta/freewheel)
  bool timecodeDirty = false;             // true if TC changed since last render frame
};

// ---------------------------------------------------------------------------
// OutputTarget — A single output destination (window, stream, or DeckLink).
//
// Decouples output routing intent from Deck internals. The "advanced output
// mode" (Project::advancedOutputMode) enables multiple OutputTargets.
// In simple mode, there is one OutputTarget per deck (hostDeckIndex maps 1:1).
//
// hostDeckIndex ties this output to its source deck for rendering.
// mirrorSourceOutputIndex lets one output mirror another (confidence monitor).
//
// Serialized in app/app_project_state.ipp with a 28+4 field layout.
// When adding fields, append to the end and bump the guard in loadProject().
// ---------------------------------------------------------------------------
struct OutputTarget {
  std::string name = "Output 1";           // operator-facing label
  int hostDeckIndex = 0;                   // which deck feeds this output (index into Project::decks)
  int displayIndex = 0;                    // OS display number for fullscreen window
  std::string displayName;                 // SDL display name recorded when the display was chosen.
                                           // SDL indices are enumeration-order-dependent and shuffle on
                                           // hot-plug/reboot — on topology change the display is re-matched
                                           // by this name first, index is only the fallback.
  bool enabled = false;                    // output is active (window open / stream running)
  std::string outputType = "window";       // "window" (SDL fullscreen) | "stream" (ffmpeg egress)
  int mirrorSourceOutputIndex = -1;        // mirror another output's frame (-1 = render own)

  // -- Streaming egress (ffmpeg SRT/RTMP) --------------------------------------
  bool streamEnabled = false;              // start streaming when output is enabled
  std::string streamProtocol = "srt";      // "srt" | "rtmp" | "rtmps"
  std::string streamUrl;                   // destination URL (e.g. "srt://host:port")
  std::string streamKey;                   // stream key (appended to RTMP URL as /key)
  int streamBitrateKbps = 6000;            // target video bitrate for encoder
  // -- SRT transport parameters ------------------------------------------------
  // Previously the ONLY way to set these was hand-typing a query string onto
  // streamUrl, which is not something to ask of an operator mid-show. They are
  // merged into the URL query by buildOutputStreamArgs; anything the operator
  // typed by hand still wins, so existing shows keep working.
  int srtLatencyMs = 120;                  // receiver buffer; the main WAN knob
  std::string srtPassphrase;               // AES encryption (>=10 chars or SRT rejects it)
  std::string srtStreamId;                 // routing hint for the receiver
  std::string srtMode = "caller";          // "caller" (dial out) | "listener" (accept)
  // -- Encoder -----------------------------------------------------------------
  int streamKeyframeSeconds = 2;           // GOP length; was hardcoded to 1s
  // Which deck's program this stream carries is implicit (it mirrors PGM).

  // -- NDI output (per-output, independent of deck-level NDI) ------------------
  bool ndiEnabled = false;                 // enable NDI send for this specific output
  std::string ndiSourceName;               // NDI source name visible on the network
  bool ndiKeyEnabled = false;              // enable NDI key (alpha channel) output
  std::string ndiKeySourceName;            // NDI key source name

  // -- Output properties -------------------------------------------------------
  std::string outputId;                    // unique ID (UUID) for remote-command targeting
  float outputAlpha = 1.0f;               // 0.0–1.0 master dimmer for this output
  int outputDelayMs = 0;                   // egress delay in ms (0–5000, for sync alignment)
  bool outputTimeOverlayEnabled = false;   // burn time/ID overlay onto this output
  std::string outputColorSpace = "auto";   // "auto" | "bt709" | "srgb"
  std::string outputLayoutMode = "span";   // "span" (portion of canvas) | "duplicate" (full copy)
  int outputOrientationDegrees = 0;        // rotation: 0 | 90 | 180 | 270 degrees
  bool outputTestCardEnabled = false;      // force test card (bars + label) on this output

  // -- DeckLink SDI output (Blackmagic hardware) -------------------------------
  bool deckLinkEnabled = false;            // route output to DeckLink card
  int deckLinkDeviceId = -1;               // DeckLink device index (-1 = not assigned)
  std::string deckLinkMode = "1080p60";    // output mode string (e.g. "1080p60", "720p50")
  bool deckLink10Bit = true;               // use 10-bit output (vs 8-bit)

  // -- Spout output (Windows interprocess texture sharing) ---------------------
  bool spoutEnabled = false;               // route output to Spout sender
  std::string spoutSenderName;             // Spout sender name visible to receivers

  // -- SMPTE ST 2110-20 output (uncompressed video over IP) --------------------
  // EXPERIMENTAL: no PTP lock and no ST 2110-21 narrow pacing — see
  // native/platform/st2110_output.hpp and docs/ST2110_FEASIBILITY.md.
  bool st2110Enabled = false;              // route output to the ST 2110-20 sender
  std::string st2110Address = "239.20.10.1";  // destination multicast group
  std::string st2110Interface;             // local NIC to send from ("" = default route)
  int st2110Port = 20000;                  // destination UDP port
  bool st2110TenBit = true;                // YCbCr-4:2:2 10-bit (vs 8-bit)

  // -- Area of Interest: per-output crop (fraction from each edge, 0–1) --------
  // Allows cropping the rendered output to show only a subregion.
  // All zeros = full output (no crop). Used for multi-display slicing.
  float aoiLeft = 0.0f;                    // crop fraction from left edge
  float aoiRight = 0.0f;                   // crop fraction from right edge
  float aoiTop = 0.0f;                     // crop fraction from top edge
  float aoiBottom = 0.0f;                  // crop fraction from bottom edge
};


// ---------------------------------------------------------------------------
// Project — Top-level state container for the entire show file.
//
// A .deckboy file serializes exactly one Project. Everything the operator
// configures is stored here: decks, outputs, integration enables, audio
// settings, master levels, and output resolution.
//
// Loaded/saved in app/app_project_state.ipp. The UI settings modal
// (app/app_render_settings.ipp) reads and writes most of these fields.
// Remote commands (app/app_remote_command.ipp) can also modify them.
// ---------------------------------------------------------------------------
struct Project {
  std::string title = std::string(kAppTitle); // show file title (displayed in title bar)
  std::vector<Deck> decks {Deck {}};          // all decks (at least one always exists)
  int focusedDeckIndex = 0;                   // which deck the UI is currently showing
  std::vector<OutputTarget> outputs {OutputTarget {}}; // all outputs (at least one)
  int focusedOutputIndex = 0;                 // which output is selected in settings UI

  // -- UI preferences ----------------------------------------------------------
  // -- SMPTE LTC generator (timecode OUT) --------------------------------------
  // Deckboy could always CHASE timecode but never generate it, so it could only
  // ever be a slave in a rig. This makes it a master: LTC is encoded to a real
  // audio device, which is how every other box on the floor expects to receive
  // it (feed it to a spare output pair, or an interface's dedicated TC out).
  bool ltcOutputEnabled = false;
  std::string ltcOutputDeviceName;         // empty = system default playback device
  double ltcOutputFps = 30.0;              // 24 / 25 / 29.97 / 30
  // LTC must be individually routable: it is a control signal, not programme
  // audio, and putting it in the show mix is how you end up broadcasting a
  // buzzsaw. It gets its own device AND its own channel on that device, with
  // every other channel held silent — so a spare pair on the interface can
  // carry timecode while the mix runs elsewhere.
  int ltcOutputChannel = 0;                // 0-based channel index LTC is placed on
  int ltcOutputChannelCount = 2;           // channels to open on that device

  // PTP domain for ST 2110 media-clock alignment. 127 is the SMPTE ST 2059-2
  // default; 0 is the generic IEEE 1588 default. Machine-wide rather than
  // per-output, because there is one clock and one PTP client.
  int ptpDomain = 127;

  // AMWA NMOS IS-04/IS-05. An ST 2110 flow is undiscoverable on its own — a
  // facility expects the node to register itself with a Registration &
  // Discovery System and to be connectable through IS-05, not for an operator
  // to hand-carry an SDP. Machine-wide for the same reason as ptpDomain: one
  // node, advertising every armed 2110 sender on the box.
  //
  // NOTE: there is no mDNS/DNS-SD here, so the registry cannot be discovered
  // automatically — it is configured by URL. Leaving the URL empty still serves
  // the Node API and IS-05 locally (useful on the bench) but registers nowhere.
  bool nmosEnabled = false;
  std::string nmosRegistryUrl;             // e.g. "http://192.168.1.50:8010"
  int nmosPort = 3210;                     // port the Node + Connection API serve on
  std::string nmosInterfaceName = "eth0";  // name reported in interface_bindings

  // PARKED — reserved for Super Deckboy, read by nothing today.
  // It is set (and forced true when a show has >1 deck), saved and loaded, but
  // no code branches on it: the "multi-output routing panel" it was meant to
  // reveal does not exist. Kept, rather than deleted, so existing shows keep
  // round-tripping their `advanced_mode` line and so the intent survives — but
  // do not treat it as a live flag. When Super Deckboy lands this is the switch
  // that pairs with kSuperDeckboySpanningUi (constants.hpp).
  bool advancedOutputMode = false;
  bool uiSoundsEnabled = true;     // play UI sound effects (navigate, take, etc.)
  bool uiTransitionsEnabled = true; // animate UI transitions (panel slides, fades)
  // Splash mascot identity. Maps to data/ui/.../splash/deckboy_splash_<name>.png.
  // Default is "deckbot"; "deckgirl" is the legacy v2-pack illustration.
  // An animated mascot path will reuse this field — load .gif/.mp4 instead of
  // .png when the matching file exists, keeping the swap surface stable.
  std::string splashCharacter = "deckbot";
  // UI color theme — directory name under data/themes/ (e.g. "gameboy",
  // "nebula", "switch-neon"). Empty means "leave the active theme untouched"
  // so opening an older, theme-less show doesn't override the operator's pick.
  // Saved with the show so a chosen colorway survives restarts.
  std::string theme = "";
  // Terrarium is the Konami-code secret: it only appears in pattern pickers
  // once unlocked, and the unlock belongs to the SAVE (cheeky secrets don't
  // leak across shows). Saved cues load fine either way.
  bool terrariumUnlocked = false;
  // UI scale factor — multiplies every font point size at load time so text
  // grows on HiDPI / 4K screens without ballooning the layout chrome. 1.0 is
  // the native baseline tuned for 1080p. 1.5–2.0 covers 4K desktops and
  // small high-DPI handhelds (the GPD Pocket 3 lands around 2.0). The full
  // layout-chrome scale lives downstream of this field — for now only fonts
  // pick it up. Persist in the project so a show authored on a 4K monitor
  // doesn't have to re-pick the scale every launch.
  double uiScale = 1.0;
  // Geometry aspect link: editing a cue's output width also scales its
  // height proportionally (and vice versa). Toggleable from the GEOMETRY
  // inspector section, like the chain-link in most media software.
  bool geometryAspectLinked = true;
  // Input model the operator expects. "mouse" is the default — full hover
  // affordances, splitter highlights, right-click menus. "touch" suppresses
  // hover-only feedback (a tap can't hover) and is the right pairing with
  // the Pocket 3 preset. Layout chrome still uses uiScale; this flag only
  // changes interaction feedback. Stored as a string so future modes can
  // land without a schema migration.
  std::string interactionMode = "mouse";

  // -- Network / integration enables -------------------------------------------
  // Each integration follows the pattern in platform/integration_backend.*:
  // enable flag here → runtime thread in main.cpp → settings toggle in UI.
  bool allowRemoteNetwork = false;       // false = listeners bind to localhost only; true = all interfaces
  bool oscQueryEnabled = false;          // OSC query server (Companion, TouchOSC, etc.)
  int oscQueryPort = 5511;               // TCP/UDP port for OSC
  bool oscFeedbackMirrorEnabled = false; // mirror OSC feedback to all connected clients
  int oscFeedbackRateMs = 120;           // throttle interval for OSC feedback packets
  bool atemTriggerEnabled = false;       // ATEM switcher tally/trigger integration
  bool ndiTriggerEnabled = false;        // NDI source discovery + trigger integration
  bool nmcSyncEnabled = false;           // NMC (Network Machine Control) time sync
  bool mtcIngestEnabled = false;         // MIDI Timecode ingest (for TC chase)
  bool ltcIngestEnabled = false;         // Linear Timecode (audio) ingest
  bool dmxArtNetEnabled = false;         // Art-Net DMX universe receive
  int artNetPort = 6454;                 // Art-Net UDP port (standard = 6454)
  bool tslTallyEnabled = false;          // TSL 3.1 tally sender (program/preview status)
  int tslTallyPort = 5800;              // TSL UDP port
  std::string tslTallyAddress = "255.255.255.255"; // broadcast or unicast target IP

  // -- Audio configuration -----------------------------------------------------
  int audioBufferSamples = 1024;  // SDL audio callback buffer: 256/512/1024/2048 samples
  int audioDelayMs = 0;           // chain A/V offset: delay ALL deck audio 0–1000 ms
                                  // (displays/PA DSP lag video — dial in with the
                                  // Pocket Test beacon until flash and pop align)
  double masterVolume = 1.0;      // 0.0–1.0 master audio volume
  double masterDimmer = 1.0;      // 0.0–1.0 master video dimmer (all outputs)

  // -- Transport behavior ------------------------------------------------------
  std::string jumpMode = "trigger";   // "trigger" (play immediately) | "load" (load paused)
  bool jumpTransitionEnabled = true;  // use transitions when jumping between cues
  // Panic button behavior (emergency stop):
  std::string panicProfile = "outputs_off"; // "outputs_off"|"fade_pause"|"fade_rewind"|"fade_load_next"
  double panicFadeSeconds = 0.9;            // panic fade-out duration
  bool panicAutoRestore = false;            // auto-restore after panic timeout

  // -- Output resolution and display -------------------------------------------
  bool outputFollowDisplay = true;    // auto-detect resolution from display
  int outputRenderWidth = 1920;       // render resolution width  (pixels)
  int outputRenderHeight = 1080;      // render resolution height (pixels)
  double outputRefreshRateHz = 0.0;   // target refresh rate (0 = auto-detect)
  int outputBitDepth = 0;             // 0=auto, 8=8-bit, 10=10-bit color depth
  bool outputCanvasEnabled = false;   // enable multi-output canvas mode
  int outputCanvasWidth = 3840;       // canvas width  (pixels, for multi-display span)
  int outputCanvasHeight = 2160;      // canvas height (pixels, for multi-display span)
};

// ---------------------------------------------------------------------------
// FramePixelFormat — How a DecodedFrame's pixel buffer is laid out.
//
// The live video decoder picks NV12 when a cue has no chroma key or color
// controls active: it cuts pipe bandwidth by ~62% (12 bpp vs 32 bpp), and
// the GPU samples the YUV→RGB conversion for free at blit time. Everything
// else — stills, thumbnails, browser frames, patterns, source-capture
// placeholders, captured output readbacks — stays RGBA32, because those
// paths either build pixels in CPU code (writePixel writes 4 bytes) or
// receive RGBA from an external producer.
//
// When this is RGBA32, `pixels` holds width*height*4 bytes, tightly packed.
// When this is NV12, `pixels` holds a Y plane of width*height bytes
// followed by an interleaved UV plane of (width/2)*(height/2)*2 bytes,
// total width*height*3/2. Helpers below compute the offsets and pitches.
// ---------------------------------------------------------------------------
enum class FramePixelFormat {
  RGBA32,  // default, 32 bpp interleaved — works with all CPU pixel paths
  NV12,    // 12 bpp planar Y + interleaved UV — live video decode only
};

// ---------------------------------------------------------------------------
// DecodedFrame — One decoded video/image/pattern frame.
//
// Filled by MediaEngine's decode thread (readExact from ffmpeg stdout, or
// CPU pattern builders), pushed into frameQueue_, then uploaded to an
// SDL_Texture. Pixel layout is described by `format`; see FramePixelFormat.
// ---------------------------------------------------------------------------
struct DecodedFrame {
  int width = 0;                       // frame width in pixels
  int height = 0;                      // frame height in pixels
  std::uint64_t index = 0;             // display-order index (time * fps) for scheduling
  double presentationSeconds = -1.0;   // decoded PTS in seconds (-1 = unknown); used so
                                       // telecined / variable-rate video schedules by its
                                       // real timestamps instead of a constant-fps counter
  FramePixelFormat format = FramePixelFormat::RGBA32;  // pixel layout for `pixels`
  std::vector<std::uint8_t> pixels;    // packed pixel data, layout per `format`

  // GPU-resident payload (in-process zero-copy decode, Windows/D3D11).
  // When gpuTexture is set the frame never touched the CPU: `pixels` is
  // empty and the video lives in a decoder-owned NV12 texture-array slice.
  // gpuFrameRef keeps the decoder surface (an AVFrame ref) alive for as long
  // as this DecodedFrame exists. Consumers compare gpuDevice against their
  // renderer's device and either GPU-copy the slice into a wrapped
  // SDL_Texture or fall back to a CPU download (libav_decoder.hpp helpers).
  std::shared_ptr<void> gpuFrameRef;   // opaque AVFrame ref (owns the surface)
  void* gpuTexture = nullptr;          // ID3D11Texture2D* (decoder array texture)
  int gpuSubresource = 0;              // array slice index within gpuTexture
  void* gpuDevice = nullptr;           // ID3D11Device* that owns gpuTexture
  bool isGpu() const { return gpuTexture != nullptr; }
};

// Byte count for a frame's pixel buffer at the given width/height/format.
// NV12 is rounded to even width/height because the chroma plane is at half
// resolution — odd dimensions would leave a partial UV sample at the edge.
inline std::size_t frameBufferSize(FramePixelFormat format, int width, int height) {
  if (width <= 0 || height <= 0) return 0;
  switch (format) {
    case FramePixelFormat::RGBA32:
      return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    case FramePixelFormat::NV12: {
      // NV12 requires even dimensions for the half-resolution chroma plane.
      int w = width & ~1;
      int h = height & ~1;
      std::size_t y = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
      std::size_t uv = y / 2u;
      return y + uv;
    }
  }
  return 0;
}

// SDL pixel-format constant for the given DecodedFrame layout. Used when
// creating the destination SDL_Texture so the renderer samples correctly.
inline Uint32 sdlPixelFormat(FramePixelFormat format) {
  switch (format) {
    case FramePixelFormat::RGBA32: return SDL_PIXELFORMAT_RGBA32;
    case FramePixelFormat::NV12:   return SDL_PIXELFORMAT_NV12;
  }
  return SDL_PIXELFORMAT_RGBA32;
}

// ---------------------------------------------------------------------------
// Button — A clickable rectangle in the control UI.
//
// Used by the cue list, toolbar, settings modal, and control bar.
// The primitives layer (render/primitives.*) draws these using fill/outline
// colors, and the text renderer places the label inside rect.
// ---------------------------------------------------------------------------
struct Button {
  std::string label;                              // button text (rendered centered)
  std::string tip;                                // tooltip shown on hover
  SDL_Rect rect {};                               // screen-space bounding box
  SDL_Color fill {48, 40, 31, 255};               // background fill color
  SDL_Color outline {255, 255, 255, 20};          // border/outline color
  SDL_Color text {245, 234, 215, 255};            // label text color
};

// ---------------------------------------------------------------------------
// QuickAction — Every action available from the inspector quick-action bar.
//
// Handled in app/app_quick_action.ipp. Each value maps to a button in the
// inspector panel (app/app_render_inspector.ipp). The handler modifies the
// selected cue's fields and triggers a re-render or engine reload as needed.
//
// Naming convention: Toggle* = bool flip, *Dec/*Inc = step value down/up,
// Edit* = open inline text editor, Cycle* = rotate through enum values.
// ---------------------------------------------------------------------------
enum class QuickAction {
  // -- Playback behavior toggles -------
  ToggleLoop, ToggleHold, TogglePauseBegin, ToggleCueAudio, ToggleNextTransition,
  EditGotoTarget, CycleEndAction,
  // -- Fade in/out ---------
  ToggleFadeIn, ToggleFadeOut,
  FadeInDec, FadeInInc, FadeOutDec, FadeOutInc,
  // -- Volume --------------
  VolDec, VolInc,
  // -- In/out points (trim) --
  InDec, InInc, OutDec, OutInc,
  // -- Per-cue transition --
  TransDec, TransInc, CycleTransStyle,
  // -- Lower third ---------
  LowerBgDec, LowerBgInc,
  // -- Duration (stills/browsers) --
  DurDec, DurInc,
  // -- Loop count ----------
  LoopCountDec, LoopCountInc,
  // -- Playback speed ------
  SpeedDec, SpeedInc,
  // -- Per-cue audio -------
  AudioGainDec, AudioGainInc,
  AudioPanDec, AudioPanInc,
  ToggleCueMono,
  NormalizeCueAudio,
  AudioFadeInDec, AudioFadeInInc,
  AudioFadeOutDec, AudioFadeOutInc,
  AudioOutPairDec, AudioOutPairInc,
  CueSectionAudioToggle,
  // -- Metadata / labels ---
  CycleColorTag,
  EditNotes,
  EditSourceRef,
  EditBrowserUrl,
  GotoMinus10, GotoMinus20, GotoMinus30,
  // -- Geometry: scale -----
  CycleScaleMode,
  ToggleAspectLink,
  ScaleXDec, ScaleXInc,
  ScaleYDec, ScaleYInc,
  EditScaleX, EditScaleY,
  // -- Geometry: offset ----
  OffsetXDec, OffsetXInc,
  OffsetYDec, OffsetYInc,
  EditOffsetX, EditOffsetY,
  // -- Geometry: rotation --
  RotDec, RotInc,
  EditRotation,
  // -- Geometry: crop ------
  CropLDec, CropLInc,
  CropRDec, CropRInc,
  CropTDec, CropTInc,
  CropBDec, CropBInc,
  // -- Chroma key ----------
  KeyToggle,
  KeyTolDec, KeyTolInc,
  KeySoftDec, KeySoftInc,
  EditKeyColor,
  PickKeyColor,
  // -- Cue number ----------
  EditCueNumber,
  // -- Copy/paste settings --
  CopyCueSettings, PasteCueSettings, ResetCueSettings, ConvertCueMedia,
  // -- Pause points --------
  AddPausePoint, ClearPausePoints,
  // -- Color correction ----
  BrightnessDec, BrightnessInc,
  ContrastDec, ContrastInc,
  SaturationDec, SaturationInc,
  HueShiftDec, HueShiftInc,
  // -- Pattern cue options --
  PatternTypePrev, PatternTypeNext,
  TogglePatternMotion,
  // -- Inspector section visibility toggles --
  CueSectionPlaybackToggle,
  CueSectionMetadataToggle,
  CueSectionGeometryToggle,
  CueSectionKeyToggle,
  // Per-cue effects section. Datamosh is the first member; the section exists
  // so future per-cue effects have an obvious home that is not "KEY".
  CueSectionEffectsToggle,
  DatamoshToggle,
  // Stage timer. Run/Reset/Nudge act on the CLOCK, not the transport.
  CueSectionTimerToggle,
  TimerRunToggle,
  TimerResetAction,
  TimerNudgeDown,
  TimerNudgeUp,
  TimerDurDec, TimerDurInc,
  TimerAmberDec, TimerAmberInc,
  TimerRedDec, TimerRedInc,
  TimerCycleMode,
  TimerCycleFace,
  TimerCountUpToggle,
  CueSectionRoutingToggle,
  // -- Overlays (PiP / lower third / composite) --
  ClearOverlay,
  EditLowerThirdText,
  EditLowerThirdSubtext,
  EditPipTarget,
  EditPipSourcePath,
  EditAttachedLowerThirdCue,
  EditAttachedPipCue,
  EditCompositeSlot1Source,
  EditCompositeSlot2Source,
  EditCompositeSlot3Source,
  EditCompositeSlot4Source,
  CompositePreset2Up,
  CompositePreset7030,
  CompositePresetQuad,
  CycleCompositeAudioSlot,
  // -- PiP position presets --
  PipPresetCornerTL,
  PipPresetCornerTR,
  PipPresetCornerBL,
  PipPresetCornerBR,
  PipPresetSmall,
  PipPresetBig,
  PipPreset7030,
  // -- Transport controls (from inspector) --
  TransportSkipStart,   // |<  seek to beginning of cue
  TransportSkipBack,    // <<  skip back 10 seconds
  TransportPlayPause,   // play/pause toggle
  TransportSkipForward, // >>  skip forward 10 seconds
  TransportSkipNext,    // >|  take the next cue now (".")
  TransportSkipPrev,    // <|  take the previous cue now (",")
  TransportSkipEnd,     // >|  seek to end of cue
  // -- Trim reset ----------
  TrimReset,            // clear in/out points back to defaults
  // -- Browser options -----
  ToggleRefreshOnTake   // toggle browser cue page reload on every take
};

// ---------------------------------------------------------------------------
// QuickButton — A clickable action button in the inspector quick-action bar.
// Maps a screen rect to a QuickAction for hit-testing in app_input.ipp.
// ---------------------------------------------------------------------------
struct QuickButton {
  SDL_Rect rect;          // screen-space bounding box (set during layout)
  QuickAction action;     // which action to fire on click
  std::string tip;        // tooltip text shown on hover
};

// ---------------------------------------------------------------------------
// DragState — Tracks an active cue drag-and-drop operation.
// Set in app_input.ipp on mouse-down over a cue row; cleared on mouse-up.
// The cue list renderer uses this to draw the drop indicator.
// ---------------------------------------------------------------------------
struct DragState {
  bool active = false;    // true while a drag is in progress
  int cueIndex = -1;      // index of the cue being dragged
  int deckIndex = 0;      // which deck the cue belongs to
};

// ---------------------------------------------------------------------------
// ToastState — Transient notification message shown to the operator.
// Triggered by triggerToast() in main.cpp; rendered as a floating bar.
// Auto-dismisses after durationMs milliseconds.
// ---------------------------------------------------------------------------
struct ToastState {
  bool active = false;                         // true while toast is visible
  Uint64 startedAt = 0;                       // SDL_GetPerformanceCounter() timestamp
  Uint32 durationMs = 1200;                   // how long to show (milliseconds)
  std::string message;                         // text content
  SDL_Color fill {155, 188, 15, 220};          // background bar color (DMG green)
  SDL_Color ink {15, 56, 15, 255};             // text color (DMG dark)
};

// ---------------------------------------------------------------------------
// UiSoundEffect — Logical sound effect identifiers for UI feedback.
// Played by the UI sound system when uiSoundsEnabled is true.
// The actual WAV files are loaded from the data/ directory at startup.
// ---------------------------------------------------------------------------
enum class UiSoundEffect {
  Navigate,   // cursor moved in cue list
  Import,     // media file imported
  Take,       // cue taken (put on air)
  Toggle,     // boolean toggled in inspector
  Stop,       // transport stopped
  Clear,      // cue cleared / output blacked out
  Delete,     // cue deleted from list
  Error,      // action refused (missing media, blocked take)
  Panic,      // panic — everything off
  Shuffle     // shuffle mode toggled on
};

#endif
