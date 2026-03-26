# Composite Cue Spec

## Summary

Deckboy should add a new cue kind: `Composite`.

A `Composite` cue is a single primary cue that renders multiple source slots
into one authored scene — a multi-source layout rendered as a single cue.

This is a better fit than introducing a general-purpose layer engine because
Deckboy is currently structured around:

- one primary live cue per deck
- a small overlay sidecar model (`Lower Third`, `PIP`)
- one compositor pass per output

`Composite` preserves that model:

- the operator still takes one cue
- transport still belongs to one primary cue
- output rendering still sees one main scene plus optional overlays

It does **not** turn the app into a full routed layer stack.

## Why This Fits Deckboy Better Than Generic Layers

### Current architecture

Today the app is effectively:

- one main live cue tracked by `Deck::activeIndex`
- optional overlay cues tracked by `Deck::overlayActiveIndices`
- output composition rendered in one pass over main content, then overlays

Relevant code:

- deck state: `Deck::activeIndex`, `Deck::overlayActiveIndices`
- take path: `takeSelected(...)`
- overlay activation: `activateOverlayCueIndex(...)`
- overlay runtime sync: `syncPipOverlayRuntimesForDeck(...)`
- output compositing: the overlay loop in `renderOutputWindow(...)`

That means Deckboy already has:

- one cue-driven transport model
- one output compositor
- special-cased overlay extras

It does **not** already have:

- multiple equal live layers with independent transport
- per-layer preview/take semantics
- general N-layer playlist sequencing

### Why generic layers are the wrong next step

If Deckboy adds true layers now, it would need to rewrite:

- `take`, `stop`, `rerack`, `clear`
- end-of-cue / next-cue logic
- preview semantics
- cue inspector assumptions
- selection/live state reporting
- save/load format for active layered scenes

That is a much larger structural change than it sounds.

### Why `Composite` is the right step

`Composite` is local and additive:

- add one new cue kind
- add one new authored scene runtime
- keep transport and output semantics intact

This gives operators the feature they actually want:

- "this cue is already a composed look"

without making the whole app become a layer router.

## Operator Model

The operator should experience `Composite` as:

- a normal cue in the main rundown
- one take action
- one preview
- one inspector
- multiple arranged picture slots inside that cue

This is different from overlays:

- `Overlay` means "bring this on top"
- `Composite` means "this cue is the scene"

That distinction should remain explicit in the UI.

## Source Model

`Composite` slots should **not** reference arbitrary normal cues in the
playlist by default.

That creates bad coupling with:

- trim
- loop / hold state
- cue naming / identity
- transport
- next-cue logic

Instead, each slot should reference its own source directly.

### Supported slot source types

V1 should support:

- `media` (video file)
- `image` (still image)
- `browser`
- `window`
- `camera`
- `syphon_spout`

V2 can optionally support:

- `deck_output`
- `ndi_source`
- shared `inputs` pool references

### Input pool recommendation

Longer-term, Deckboy should likely have a reusable `Inputs` pool:

- browser inputs
- window inputs
- camera inputs
- syphon/spout inputs

Then:

- normal source cues can reference that pool
- `Composite` slots can reference that pool

But V1 does not need that abstraction to ship.

## Data Model

Add a new cue kind:

```cpp
enum class CueKind {
  Video,
  Image,
  Pattern,
  Browser,
  WindowSource,
  Camera,
  Syphon,
  Pip,
  LowerThird,
  Audio,
  Composite
};
```

### New structs

```cpp
enum class CompositeSlotSourceKind {
  Media,
  Image,
  Browser,
  Window,
  Camera,
  SyphonSpout
};

enum class CompositeFitMode {
  Fit,
  Fill,
  Stretch
};

struct CompositeSlot {
  std::string id;
  std::string name;
  bool enabled = true;
  int zIndex = 0;

  CompositeSlotSourceKind sourceKind = CompositeSlotSourceKind::Media;
  std::string sourceValue;

  bool audioEnabled = false;
  float audioGain = 1.0f;

  float x = 0.0f;
  float y = 0.0f;
  float width = 1.0f;
  float height = 1.0f;
  float alpha = 1.0f;
  float rotationDegrees = 0.0f;

  float cropLeft = 0.0f;
  float cropRight = 0.0f;
  float cropTop = 0.0f;
  float cropBottom = 0.0f;

  CompositeFitMode fitMode = CompositeFitMode::Fit;
  bool borderEnabled = false;
  SDL_Color borderColor {155, 188, 15, 255};
  int borderThickness = 0;

  bool chromaKeyEnabled = false;
  SDL_Color chromaKeyColor {0, 255, 0, 255};
  float chromaKeyTolerance = 60.0f;
  float chromaKeySoftness = 20.0f;
};

struct CompositeCueState {
  std::vector<CompositeSlot> slots;
  std::string audioMasterSlotId;
  SDL_Color backgroundColor {0, 0, 0, 255};
};
```

### Cue storage options

There are two realistic storage approaches:

1. Put `CompositeCueState composite;` directly on `Cue`
2. Use `std::variant`-style subtype storage

For Deckboy as it stands, option 1 is better:

- it matches the current monolithic `Cue` design
- it minimizes save/load churn
- it avoids a large type-system refactor

So for now:

```cpp
struct Cue {
  ...
  CompositeCueState composite;
};
```

## Runtime Model

`Composite` should behave as one main cue, not as overlays.

### V1 runtime

When a `Composite` cue goes live:

- `Deck::activeIndex` points at the composite cue
- the main media engine remains responsible for the cue's transport state
- the composite runtime owns one child runtime per enabled slot

Suggested runtime shape:

```cpp
struct CompositeSlotRuntime {
  std::string slotId;
  Cue resolvedCue;
  bool resolvedCueValid = false;
  std::unique_ptr<MediaEngine> mediaEngine;
  SDL_Texture* bridgeTexture = nullptr;
};

struct CompositeRuntime {
  std::string cueRuntimeKey;
  std::vector<CompositeSlotRuntime> slots;
};
```

### Where it should live

Do **not** bolt `Composite` onto `overlayActiveIndices`.

It should live alongside the main cue path:

- a `Composite` is the main active cue
- its child slots are internal implementation details

Recommended ownership:

- `DeckRuntime` or app-level map keyed by `deckIndex + cueId`

### Slot resolution

Each slot should resolve into a cue-like runtime input, similar to how PIP
currently resolves self-contained sources.

That means the existing PIP source-resolution logic is reusable as a pattern,
but it should be generalized into something like:

- `buildResolvedSourceCue(...)`

and then used by:

- `PIP`
- `CompositeSlot`

## Rendering Model

### V1 render order

Inside `renderOutputWindow(...)`:

1. render main active cue
2. if main active cue is `Composite`, render its slots into the main scene
3. render overlays on top as usual

So the order becomes:

- base background
- composite slot 0..N, sorted by `zIndex`
- overlay bin items (`Lower Third`, `PIP`)
- output overlay/timecode if enabled

This keeps `Composite` as scene content and preserves overlays as operator
extras.

### Slot rendering

Each slot should render using the same geometry helper path already used for:

- cue geometry
- bridge textures
- PIP output rendering

That means `renderTextureWithCueGeometry(...)` is the right conceptual basis,
but slot-local geometry should be slot-owned, not cue-owned.

Recommended V1:

- render each slot to a bridge texture
- apply slot crop/fit/position/alpha on the output compositor
- ignore slot-level perspective warp in V1

### Background

Each composite cue should have a background fill:

- black by default
- optionally selectable color

This avoids undefined empty areas between slots.

## Audio Rules

Audio is where generic multiview systems become messy. Keep V1 strict.

### V1 rule

Only one slot is the active audio slot.

That audio slot:

- feeds the composite cue's audible program audio
- defaults to the first enabled slot with audio
- can be changed in the inspector

Other slots:

- are muted by default
- can be armed for future use, but not mixed in V1

### Why

This avoids immediately building:

- multi-source audio mixing
- per-slot latency compensation
- meters for multiple simultaneous internals

Deckboy can still ship a useful composite cue without becoming an audio mixer.

## Transport Rules

V1 transport should be simple and deterministic.

### Master rule

The composite cue itself owns transport.

Suggested behavior:

- composite `Play/Pause/Stop/Rerack` controls all slot runtimes
- slot start is synchronized on take
- composite duration is derived from a chosen timing mode

### Timing mode

V1 should support one timing mode only:

- `duration = longest enabled slot`

When the composite ends:

- normal cue end rules apply (`hold`, `next`, etc.)

### Looping

V1:

- composite loop means the composite loops as a scene
- all enabled slot runtimes rerack and restart together

Avoid per-slot loop policy in V1.

## Inspector Design

The cue inspector for `Composite` should replace the normal playback detail
rows with a scene-focused layout.

### Inspector sections

- `PLAYBACK`
- `SLOTS`
- `LAYOUT`
- `AUDIO`
- `NOTES`

### `PLAYBACK`

- fade in
- fade out
- hold
- end action
- duration mode

### `SLOTS`

For each slot row:

- enabled toggle
- slot name
- source type
- source picker / value
- visible badge
- audio badge
- z order

Actions:

- `ADD SLOT`
- `REMOVE SLOT`
- `DUPLICATE SLOT`
- `MOVE UP`
- `MOVE DOWN`

### `LAYOUT`

- layout preset buttons:
  - `PIP TL`
  - `PIP TR`
  - `PIP BL`
  - `PIP BR`
  - `50/50`
  - `70/30`
  - `TRIPLE`
  - `QUAD`
- per-slot geometry editor for the selected slot

### `AUDIO`

- `MASTER AUDIO SLOT`
- per-slot `audio on/off`
- per-slot gain, if enabled

## Monitor Editing

This feature only feels professional if the operator can edit slots visually.

### V1 monitor edit behavior

In preview/program edit mode:

- click slot to select it
- drag to move
- drag handles to resize
- modifier for aspect lock
- highlight selected slot

This should behave similarly to the existing warp and trim interaction model:

- direct manipulation
- visible handles
- inspector for exact values

### Selection model

When a composite cue is selected:

- cue remains selected in rundown
- one slot becomes the selected sub-item in the inspector/monitor

Do not let slot selection replace cue selection.

## Preview Behavior

Preview should show the composed scene, not just a placeholder.

When a composite cue is selected:

- Deckboy should resolve its slots
- render the composed result into Preview
- if sources are still loading, show the composed loading state

The preview should answer:

- what will TAKE put on screen?

not:

- what is slot 1 alone?

## Save / Load

### Project format

Composite slots should persist inline with the cue.

Required serialized fields:

- slot count
- slot ids
- source types and values
- geometry
- crop
- fit mode
- alpha
- z
- audio flags
- master audio slot
- composite background color

### Backward compatibility

Older projects:

- load with no composite data
- no migration needed

Newer projects in older Deckboy builds:

- should fail gracefully or ignore unknown cue subtype data

## Relationship To Existing Overlays

Composite should not replace overlays entirely.

Recommended final model:

- `Composite` = authored multi-source scene
- `Lower Third` / `PIP` overlay bin = reusable live extras

Examples:

- camera + slides side-by-side = `Composite`
- lower third over that scene = overlay
- quick emergency PIP over program = overlay

That is a cleaner operator story than making every picture-in-picture a scene.

## Relationship To Existing PIP Cue

Once `Composite` exists, `PIP` becomes less central.

Recommended direction:

- keep `PIP` for fast ad hoc overlay use
- do not expand `PIP` into a general scene system
- steer authored multi-box layouts toward `Composite`

Later, `PIP` could internally share slot/source code with `Composite`.

## Implementation Plan

### Phase 1: Data + Serialization

- add `CueKind::Composite`
- add `CompositeCueState` + `CompositeSlot`
- persist them in project save/load
- add stub inspector / empty state

### Phase 2: Slot Source Resolution

- generalize PIP-style source resolution into shared helpers
- resolve media/browser/window/camera/syphon slots
- preview one composed scene

### Phase 3: Runtime + Render

- add `CompositeRuntime`
- render composed slots into the main output path
- one audio master slot only

### Phase 4: Inspector + Presets

- slot list UI
- per-slot property editing
- layout preset buttons
- duplicate / reorder / remove slot actions

### Phase 5: Direct Manipulation

- drag/resize in preview/program editor
- selection handles
- snapping

## Explicit Non-Goals For V1

Do not add these in the first implementation:

- nested composites
- arbitrary cue-to-cue slot references
- per-slot transitions
- per-slot independent transport
- multi-source mixed audio
- deck-wide generic layer router
- per-slot perspective mesh warp

Those are how the feature turns into a full media server rewrite.

## Recommendation

Implement `Composite` as a first-class cue type.

Do **not** implement general layers first.

That gives Deckboy the operator benefit of authored multi-source layouts
while staying aligned with the codebase it actually has today.
