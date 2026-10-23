// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// app_scopes.ipp -- the picture scopes, measured on the OUTPUT.
//
// WHAT IS MEASURED: the programme output (or the one SOURCE picks), its own
// finished frame at its full raster, through a private 60 Hz capture tap. On an
// HDR output that frame is the SDR copy, with every highlight clipped at
// white, so the scopes read the float compositor instead, as PQ BT.2020
// (readHdrCompositorRaw + a PQ HdrEncoder, the HDR recording's picture), graduated in nits. Not the desk's preview, which is a
// scaled-down copy: a scope fed from that reads wrong numbers, and a camera
// shaded against wrong numbers is wrong on air. Every pixel of the frame is
// counted; only the waveform's horizontal resolution is reduced, which loses
// no level information because each column still accumulates every pixel
// under it.
//
// The analysis is the SDL-free core in core/scopes.hpp. Reusable workers
// analyse vertical strips aligned to waveform columns and chroma pairs.
// The coordinator also builds the trace images off the renderer thread.
// EBU R 103's FILTERED gamut count needs neighbouring rows,
// so it runs separately on the whole frame every two seconds; the raw count is
// exact on every update.
//
// Drawn on scope black with fixed scope colours in every theme: a waveform
// read against a theme's tint is a waveform read wrong.

  enum class MonitorView : int { Program = 0, Scopes = 1, Both = 2 };
  enum ScopePanel : int {
    kScopeWaveform = 0, kScopeParade, kScopeVector, kScopeDiamond, kScopeHistogram, kScopePanelCount
  };

  struct ScopeImages {
    std::array<std::vector<std::uint8_t>, kScopeDiamond + 1> rgba;
    std::array<int, kScopeDiamond + 1> width{}, height{};
  };
  struct ScopesOutcome {
    std::shared_ptr<const ScopeImages> images;
    std::string outputId;
    Uint64 capturedNs = 0, completedNs = 0;
    std::uint64_t generation = 0;
    std::shared_ptr<const deckboy::core::scopes::Result> result;
    int width = 0, height = 0, output = -1;
    double analysisMs = 0.0, prepareMs = 0.0;
    deckboy::core::scopes::Matrix matrix = deckboy::core::scopes::Matrix::Bt709;
    unsigned sentDepth = 0;  // 0: the R'G'B' frame; 8/10: the Y'CbCr 4:2:2 SDI or ST 2110 sends
    bool sentSdi = false;
    bool hdr = false;        // the float compositor as 16-bit PQ BT.2020
    int hdrWhiteNits = 0;    // where SDR material sits on it
    double peakNits = 0.0;   // the brightest component in the picture
  };

  MonitorView monitorView_ = MonitorView::Program;
  int scopesBig_ = -1;           // a panel shown alone, or -1 for all of them
  bool scopesFrozen_ = false;
  bool scopesBt2020_ = false;
  int scopesSource_ = -1;        // -1: the programme; otherwise an output
  bool scopesDrawHdr_ = false;   // the graticule in nits, for the result drawn
  struct ScopeJob {
    ScopesOutcome outcome;
    std::shared_ptr<std::vector<std::uint8_t>> pixels;
    bool halfFloat = false;
    std::shared_ptr<SDL_Surface> raw;
  };
  std::mutex scopesMutex_;
  std::condition_variable scopesWake_;
  std::thread scopesWorker_, scopesFilterWorker_;
  std::optional<ScopeJob> scopesPending_;
  // TWO STAGES: one thread analyses frame N+1 while the other draws frame N,
  // so the rate is set by the slower stage, not the sum of both.
  std::thread scopesDrawWorker_;
  std::condition_variable scopesDrawWake_;
  std::optional<ScopesOutcome> scopesDrawPending_;   // scopesMutex_
  bool scopesStopping_ = false;
  std::atomic<bool> scopesFilterBusy_ {false};
  std::atomic<std::uint64_t> scopesGeneration_ {1};
  ScopesOutcome scopesLatest_;                         // scopesMutex_
  std::uint64_t scopesSerial_ = 0;                     // scopesMutex_
  std::deque<Uint64> scopesUpdateTimes_;                // completion times, scopesMutex_
  deckboy::core::scopes::GamutCounts scopesFiltered_;  // scopesMutex_
  bool scopesFilteredValid_ = false;                   // scopesMutex_
  Uint64 scopesFilteredAtNs_ = 0;
  std::uint64_t scopesDrawnSerial_ = 0;
  Uint64 scopesLastFilterMs_ = 0;
  Uint64 scopesLastWireCaptureNs_ = 0;
  double scopesCaptureMs_ = 0, scopesUploadMs_ = 0;
  // ONE ANALYSIS PER NEW PICTURE (James, 2026-10-21: "the scopes should
  // reflect the refresh rate and fps of the source material"). Main thread.
  std::uint64_t scopesLastFingerprint_ = 0;  // the last picture sent for analysis
  std::deque<Uint64> scopesPictureTimes_;    // when the scoped picture changed
  double scopesOutputHz_ = 0;                // the scoped output's present rate
  Uint64 scopesConfirmedNs_ = 0;             // last time the picture was seen unchanged
  // SMOOTH AT 4K (James, 2026-10-21: "massive performance issues ... on my 4k
  // monitor"). Measured: reading back and fingerprinting EVERY presented 4K
  // frame cost the render thread 5.6 ms a frame, and the analyser kept ~11
  // cores busy. Now a frame is read back only when the analyser can take it,
  // an unchanged picture is re-checked 4 times a second rather than 60, and
  // the analyser has a fixed share of the machine.
  Uint64 scopesNextProbeNs_ = 0;             // main thread: earliest next look at a still picture
  // WHICH PRESENTS CAN HOLD A NEW PICTURE (audit 2026-10-22, #5). A 25 fps clip
  // on a 60 Hz output presents each frame two or three times; the first repeat
  // used to read as "still" and hold the scopes off for 250 ms, so they measured
  // 64 of 250 pictures. The engines count the frames they display: a change in
  // that count is a new source picture, found without reading anything back.
  std::uint64_t scopesLastSourceKey_ = 0;
  bool scopesPictureAnimating_ = false;      // changing with no new source frame
  std::atomic<bool> scopesAnalysing_ {false};
  std::atomic<bool> scopesDrawing_ {false};
  std::string scopesCaptureKey_;
  SDL_Renderer* scopesCaptureRenderer_ = nullptr;
  SDL_Texture* scopesCaptureTexture_ = nullptr;
  void* scopesReadback_ = nullptr;
  bool scopesReadbackUnavailable_ = false;
  int scopesReadbackMisses_ = 0;
  std::vector<std::shared_ptr<std::vector<std::uint8_t>>> scopesBuffers_;
  SDL_Texture* scopeTex_[kScopeDiamond + 1] {};
  int scopeTexW_[kScopeDiamond + 1] {};
  int scopeTexH_[kScopeDiamond + 1] {};
  SDL_Rect scopesViewBtnRect_ {};
  SDL_Rect scopesFreezeBtnRect_ {};
  SDL_Rect scopesSourceBtnRect_ {};
  SDL_Rect scopesPanelRects_[kScopePanelCount] {};

  static const char* scopePanelName(int panel) {
    switch (panel) {
      case kScopeWaveform: return "waveform";
      case kScopeParade: return "parade";
      case kScopeVector: return "vector";
      case kScopeDiamond: return "diamond";
      case kScopeHistogram: return "histogram";
      default: return "";
    }
  }

  static const char* monitorViewName(MonitorView view) {
    return view == MonitorView::Scopes ? "SCOPES" : view == MonitorView::Both ? "BOTH" : "PROGRAM";
  }

  // The programme -- the output the audience sees -- unless SOURCE picked one.
  int scopesOutputIndex() const {
    if (scopesSource_ >= 0 && scopesSource_ < static_cast<int>(project_.outputs.size())) return scopesSource_;
    return std::max(0, primaryProgrammeOutputIndex(-1));
  }

  std::string scopesSourceLabel() const {
    return scopesSource_ < 0 || scopesSource_ >= static_cast<int>(project_.outputs.size())
      ? std::string("PROGRAMME") : "OUTPUT " + std::to_string(scopesSource_ + 1);
  }

  void cycleScopesSource() {
    const int count = static_cast<int>(project_.outputs.size());
    scopesSource_ = scopesSource_ < 0 ? (count > 0 ? 0 : -1) : scopesSource_ + 1 >= count ? -1 : scopesSource_ + 1;
    releaseScopesCapture();
    triggerToast("scopes: " + scopesSourceLabel() + " (" + outputLabel(scopesOutputIndex()) + ")");
  }

  // The output render loop asks this before the independent scopes capture.
  bool scopesWatching(int outputIndex) const {
    return monitorView_ != MonitorView::Program && !scopesFrozen_ &&
           outputIndex == scopesOutputIndex();
  }

  static deckboy::core::scopes::Options scopesOptions(int width, bool bt2020) {
    deckboy::core::scopes::Options options;
    options.matrix = bt2020 ? deckboy::core::scopes::Matrix::Bt2020Ncl
                            : deckboy::core::scopes::Matrix::Bt709;
    options.columns = static_cast<std::size_t>(std::clamp(width, 1, 960));
    options.levelBins = 256;
    options.vectorBins = 256;
    options.histogramBins = 256;
    options.gamutBitDepth = 10;
    options.gamutFilter = deckboy::core::scopes::GamutFilter::None;
    return options;
  }

  // The signal an output actually sends. SDI and ST 2110 carry the frame as
  // BT.709 Y'CbCr 4:2:2, where a saturated edge can leave gamut; the 8-bit
  // R'G'B' frame every other route sends cannot. Asked of the OPEN sender, at
  // the depth it opened (DeckLink drops to 8-bit when the card has no 10):
  // a switch that is on with no card behind it sends nothing. SDI first.
  unsigned scopesSentDepth(int outputIndex, bool* sdi = nullptr) const {
    if (sdi) *sdi = false;
    if (outputIndex < 0 || outputIndex >= static_cast<int>(outputRuntimes_.size())) return 0;
    const OutputRuntime& rt = outputRuntimes_[static_cast<std::size_t>(outputIndex)];
#if defined(DECKBOY_HAS_DECKLINK)
    if (rt.deckLinkOutput && rt.deckLinkOutput->isInitialized()) {
      if (sdi) *sdi = true;
      return rt.deckLinkOutput->isTenBit() ? 10u : 8u;
    }
#endif
    if (rt.st2110Sender && rt.st2110Sender->isOpen()) {
      return rt.st2110Sender->config().sampling == deckboy::platform::video::St2110Sampling::YCbCr422_10bit
        ? 10u : 8u;
    }
    return 0;
  }

  // An HDR output with nothing sending SDI or ST 2110 from it: its picture is
  // the float compositor, and the scopes read that.
  bool scopesReadsHdr(int outputIndex, const OutputRuntime& runtime) const {
    return runtime.hdr && runtime.compositorTexture && runtime.outputRenderer &&
           scopesSentDepth(outputIndex) == 0;
  }

#include "app_scopes_pipeline.ipp"

  // Density -> an RGBA texture. Brightness follows log(count) so a single
  // pixel's trace is visible next to a flat field of millions.
  static void uploadScopeDensity(int slot, const deckboy::core::scopes::Density& density, int columns,
                          int xOffset, int textureW, int textureH, bool flipY, SDL_Color tint,
                          std::vector<std::uint8_t>& rgba) {
    std::uint64_t peak = 1;
    for (const auto count : density.bins) peak = std::max(peak, count);
    const double scale = 1.0 / std::log1p(static_cast<double>(peak));
    const int rows = static_cast<int>(density.height);
    for (int row = 0; row < rows; ++row) {
      const int y = flipY ? (rows - 1 - row) : row;
      for (int x = 0; x < columns; ++x) {
        const std::uint64_t count = density.bins[static_cast<std::size_t>(row) * density.width + x];
        const double level = count ? std::pow(std::log1p(static_cast<double>(count)) * scale, 0.55) : 0.0;
        const double lift = count ? 0.18 + 0.82 * level : 0.0;  // a lone pixel still shows
        std::uint8_t* px = &rgba[(static_cast<std::size_t>(y) * textureW + (x + xOffset)) * 4];
        px[0] = static_cast<std::uint8_t>(std::min(255.0, tint.r * lift));
        px[1] = static_cast<std::uint8_t>(std::min(255.0, tint.g * lift));
        px[2] = static_cast<std::uint8_t>(std::min(255.0, tint.b * lift));
        px[3] = 255;
      }
    }
    (void)textureH;
    (void)slot;
  }

  void ensureScopeTexture(int slot, int width, int height) {
    if (scopeTex_[slot] && scopeTexW_[slot] == width && scopeTexH_[slot] == height) return;
    if (scopeTex_[slot]) SDL_DestroyTexture(scopeTex_[slot]);
    scopeTex_[slot] = deckboyCreateTexture(controlRenderer_, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_STATIC, width, height);
    if (scopeTex_[slot]) SDL_SetTextureScaleMode(scopeTex_[slot], SDL_SCALEMODE_LINEAR);
    scopeTexW_[slot] = width;
    scopeTexH_[slot] = height;
  }

  static std::shared_ptr<const ScopeImages> buildScopeImages(
      const std::shared_ptr<const deckboy::core::scopes::Result>& result) {
    auto images = std::make_shared<ScopeImages>();
    // THE FOUR PICTURES ARE INDEPENDENT, so they are drawn at once. One after
    // another they cost about 9 ms a frame at 1080p, which with the analysis
    // capped the scopes near 47 updates a second -- under a 59.94 source.
    std::function<void()> tasks[4];
    const auto keep = [&](int slot, int w, int h, std::vector<std::uint8_t>& pixels) {
      images->width[slot] = w;
      images->height[slot] = h;
      images->rgba[slot].swap(pixels);
    };
    const SDL_Color trace {150, 255, 150, 255};
    tasks[0] = [&] {  // luma waveform
      std::vector<std::uint8_t> rgba;
      const auto& d = result->lumaWaveform();
      const int w = static_cast<int>(d.width), h = static_cast<int>(d.height);
      rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
      uploadScopeDensity(kScopeWaveform, d, w, 0, w, h, true, trace, rgba);
      keep(kScopeWaveform, w, h, rgba);
    };
    tasks[1] = [&] {  // RGB parade, three side by side
      std::vector<std::uint8_t> rgba;
      const int w = static_cast<int>(result->rgbParade[0].width);
      const int h = static_cast<int>(result->rgbParade[0].height);
      rgba.assign(static_cast<std::size_t>(w) * 3 * h * 4, 0);
      const SDL_Color tints[3] {{255, 110, 110, 255}, {120, 255, 120, 255}, {120, 160, 255, 255}};
      for (int c = 0; c < 3; ++c) {
        uploadScopeDensity(kScopeParade, result->rgbParade[c], w, c * w, w * 3, h, true, tints[c], rgba);
      }
      keep(kScopeParade, w * 3, h, rgba);
    };
    tasks[2] = [&] {  // vectorscope, Cr up
      std::vector<std::uint8_t> rgba;
      const auto& d = result->vectorscope;
      const int w = static_cast<int>(d.width), h = static_cast<int>(d.height);
      rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
      uploadScopeDensity(kScopeVector, d, w, 0, w, h, true, trace, rgba);
      keep(kScopeVector, w, h, rgba);
    };
    tasks[3] = [&] {  // diamond: upper half B/G with white at the top, lower
      // half R/G with white at the bottom, the two meeting at black in the middle
      std::vector<std::uint8_t> rgba;
      const auto& up = result->diamond[0];
      const auto& low = result->diamond[1];
      const int w = static_cast<int>(up.width), h = static_cast<int>(up.height);
      rgba.assign(static_cast<std::size_t>(w) * h * 2 * 4, 0);
      std::vector<std::uint8_t> half(static_cast<std::size_t>(w) * h * 4, 0);
      uploadScopeDensity(kScopeDiamond, up, w, 0, w, h, true, trace, half);
      std::copy(half.begin(), half.end(), rgba.begin());
      std::fill(half.begin(), half.end(), 0);
      uploadScopeDensity(kScopeDiamond, low, w, 0, w, h, false, trace, half);
      std::copy(half.begin(), half.end(), rgba.begin() + static_cast<std::ptrdiff_t>(half.size()));
      keep(kScopeDiamond, w, h * 2, rgba);
    };
    std::future<void> running[3];
    for (int i = 0; i < 3; ++i) running[i] = std::async(std::launch::async, tasks[i]);
    tasks[3]();
    for (auto& task : running) task.get();
    return images;
  }

  // Only texture upload stays on the renderer thread.
  std::shared_ptr<const deckboy::core::scopes::Result> refreshScopeTextures(ScopesOutcome& outcome) {
    std::uint64_t serial;
    { std::lock_guard lock(scopesMutex_); outcome = scopesLatest_; serial = scopesSerial_; }
    if (outcome.images && serial != scopesDrawnSerial_) {
      const auto started = SDL_GetTicksNS();
      scopesDrawnSerial_ = serial;
      for (int slot = 0; slot <= kScopeDiamond; ++slot) {
        const int w = outcome.images->width[slot], h = outcome.images->height[slot];
        ensureScopeTexture(slot, w, h);
        if (scopeTex_[slot]) SDL_UpdateTexture(scopeTex_[slot], nullptr,
          outcome.images->rgba[slot].data(), w * 4);
      }
      scopesUploadMs_ = (SDL_GetTicksNS() - started) / 1000000.0;
    }
    return outcome.result;
  }

  // ── Drawing ────────────────────────────────────────────────────────────────

  static constexpr SDL_Color kScopeBlack {8, 10, 8, 255};
  static constexpr SDL_Color kScopeGrat {150, 140, 96, 150};
  static constexpr SDL_Color kScopeLabel {205, 195, 150, 255};
  static constexpr SDL_Color kScopeAlarm {255, 90, 70, 255};

  void scopeLine(int x1, int y1, int x2, int y2, SDL_Color color) {
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, color.r, color.g, color.b, color.a);
    SDL_RenderLine(controlRenderer_, static_cast<float>(x1), static_cast<float>(y1),
                   static_cast<float>(x2), static_cast<float>(y2));
  }

  void scopeDashed(int x1, int y1, int x2, int y2, SDL_Color color) {
    const double length = std::hypot(x2 - x1, y2 - y1);
    const int dashes = std::max(1, static_cast<int>(length / uiScaled(6)));
    for (int i = 0; i < dashes; i += 2) {
      const double a = static_cast<double>(i) / dashes, b = std::min(1.0, (i + 1.0) / dashes);
      scopeLine(static_cast<int>(x1 + (x2 - x1) * a), static_cast<int>(y1 + (y2 - y1) * a),
                static_cast<int>(x1 + (x2 - x1) * b), static_cast<int>(y1 + (y2 - y1) * b), color);
    }
  }

  void scopeText(int x, int y, const std::string& text, SDL_Color color, bool right = false) {
    TTF_Font* font = fontSmall_;
    int w = 0;
    if (font) textSizeShown(font, text.c_str(), text.size(), &w, nullptr);
    const int h = textLineHeight(font);
    drawTextSafe(controlRenderer_, font, SDL_Rect {right ? x - w - uiScaled(4) : x, y - h / 2, w + uiScaled(8), h},
                 text, color);
  }

  // A panel: black field, a title, and the plot rect inside its margins.
  SDL_Rect scopePanel(const SDL_Rect& rect, const char* title, int panel, int leftMargin, int rightMargin) {
    scopesPanelRects_[panel] = rect;
    Primitives::fillRect(controlRenderer_, rect, kScopeBlack);
    Primitives::strokeRect(controlRenderer_, rect, SDL_Color {60, 70, 60, 255});
    const int titleH = textLineHeight(fontSmall_);
    scopeText(rect.x + uiScaled(6), rect.y + titleH / 2 + uiScaled(2), title, kScopeLabel);
    return SDL_Rect {rect.x + leftMargin, rect.y + titleH + uiScaled(6),
                     std::max(1, rect.w - leftMargin - rightMargin),
                     std::max(1, rect.h - titleH - uiScaled(12))};
  }

  void drawScopeTexture(int slot, const SDL_Rect& plot) {
    if (!scopeTex_[slot]) return;
    if (slot == kScopeVector || slot == kScopeDiamond) {
      // A flat colour lands in ONE bin; drawn once it is a pinpoint. Four
      // offset copies make it a dot without moving where it is.
      SDL_SetTextureBlendMode(scopeTex_[slot], SDL_BLENDMODE_ADD);
      for (int d = 0; d < 4; ++d) {
        const float dx = (d % 2) ? 1.0f : -1.0f, dy = (d / 2) ? 1.0f : -1.0f;
        const SDL_FRect spot {plot.x + dx, plot.y + dy, static_cast<float>(plot.w), static_cast<float>(plot.h)};
        SDL_RenderTexture(controlRenderer_, scopeTex_[slot], nullptr, &spot);
      }
    }
    const SDL_FRect dst {static_cast<float>(plot.x), static_cast<float>(plot.y),
                         static_cast<float>(plot.w), static_cast<float>(plot.h)};
    SDL_SetTextureBlendMode(scopeTex_[slot], SDL_BLENDMODE_ADD);
    SDL_RenderTexture(controlRenderer_, scopeTex_[slot], nullptr, &dst);
  }

  // Levels graticule shared by the waveform and each parade column: % on the
  // left, mV on the right, and the EBU R 103 preferred limits dashed.
  void drawLevelGraticule(const SDL_Rect& plot, bool labels, bool millivolts = true) {
    if (scopesDrawHdr_) {
      drawNitsGraticule(plot, labels, millivolts);
      return;
    }
    for (int pct = 0; pct <= 100; pct += 10) {
      const int y = plot.y + plot.h - static_cast<int>(std::lround(plot.h * pct / 100.0));
      const bool major = pct % 25 == 0 || pct == 100;
      scopeLine(plot.x, y, plot.x + plot.w, y, major ? kScopeGrat : SDL_Color {150, 140, 96, 70});
      if (labels && (pct % 25 == 0)) {
        scopeText(plot.x, y, std::to_string(pct), kScopeLabel, true);
        if (millivolts) scopeText(plot.x + plot.w + uiScaled(2), y, std::to_string(pct * 7), kScopeLabel);
      }
    }
  }

  // The HDR graticule: PQ is absolute, so each line IS a brightness. Nits on
  // the left, the PQ signal in % on the right, and SDR white dashed where SDR
  // material sits (Settings' "SDR material at", 203 by default).
  void drawNitsGraticule(const SDL_Rect& plot, bool labels, bool pqPercent) {
    namespace s = deckboy::core::scopes;
    const auto yAt = [&](double nits) {
      return plot.y + plot.h - static_cast<int>(std::lround(plot.h * s::nitsToPq(nits)));
    };
    struct NitsLine { double nits; const char* label; };
    static constexpr NitsLine kLines[] = {
      {0.0, "0"}, {1.0, "1"}, {10.0, "10"}, {100.0, "100"}, {1000.0, "1k"}, {4000.0, "4k"}, {10000.0, "10k"}};
    for (const auto& line : kLines) {
      const int y = yAt(line.nits);
      scopeLine(plot.x, y, plot.x + plot.w, y, kScopeGrat);
      if (labels) scopeText(plot.x, y, line.label, kScopeLabel, true);
      if (labels && pqPercent) {
        scopeText(plot.x + plot.w + uiScaled(2), y,
                  std::to_string(static_cast<int>(std::lround(100.0 * s::nitsToPq(line.nits)))), kScopeLabel);
      }
    }
    for (const double nits : {400.0, 600.0, 2000.0}) {
      const int y = yAt(nits);
      scopeLine(plot.x, y, plot.x + plot.w, y, SDL_Color {150, 140, 96, 70});
    }
    const int white = yAt(static_cast<double>(std::clamp(project_.hdrSdrWhiteNits, 80, 500)));
    scopeDashed(plot.x, white, plot.x + plot.w, white, SDL_Color {120, 200, 255, 220});
    if (labels) {
      scopeText(plot.x + uiScaled(2), white - textLineHeight(fontSmall_) / 2, "SDR",
                SDL_Color {120, 200, 255, 255});
    }
  }

  void drawWaveformPanel(const SDL_Rect& rect) {
    const SDL_Rect plot = scopePanel(rect, scopesDrawHdr_ ? "WAVEFORM Y'  nits  |  PQ %" : "WAVEFORM Y'  %  |  mV",
                                     kScopeWaveform, uiScaled(30), uiScaled(34));
    drawScopeTexture(kScopeWaveform, plot);
    drawLevelGraticule(plot, true);
  }

  void drawParadePanel(const SDL_Rect& rect) {
    const SDL_Rect plot = scopePanel(rect, "RGB PARADE", kScopeParade, uiScaled(30), uiScaled(6));
    drawScopeTexture(kScopeParade, plot);
    const int third = plot.w / 3;
    for (int c = 0; c < 3; ++c) {
      SDL_Rect column {plot.x + c * third, plot.y, third, plot.h};
      drawLevelGraticule(column, c == 0, false);
      if (c > 0) scopeLine(column.x, plot.y, column.x, plot.y + plot.h, kScopeGrat);
    }
  }

  void drawVectorPanel(const SDL_Rect& rect, deckboy::core::scopes::Matrix matrix) {
    namespace s = deckboy::core::scopes;
    SDL_Rect plot = scopePanel(rect, "VECTOR  75% []  100% +", kScopeVector, uiScaled(6), uiScaled(6));
    const int side = std::min(plot.w, plot.h);
    plot = SDL_Rect {plot.x + (plot.w - side) / 2, plot.y + (plot.h - side) / 2, side, side};
    drawScopeTexture(kScopeVector, plot);
    const double cx = plot.x + side / 2.0, cy = plot.y + side / 2.0;
    auto at = [&](double cb, double cr) {  // chroma -0.5..0.5 fills the square
      return SDL_Point {static_cast<int>(std::lround(cx + cb * side)),
                        static_cast<int>(std::lround(cy - cr * side))};
    };
    for (int i = 0; i < 72; ++i) {  // the 100% saturation circle
      const double a0 = i * 2 * 3.14159265358979 / 72, a1 = (i + 1) * 2 * 3.14159265358979 / 72;
      const SDL_Point p0 = at(0.5 * std::cos(a0), 0.5 * std::sin(a0));
      const SDL_Point p1 = at(0.5 * std::cos(a1), 0.5 * std::sin(a1));
      scopeLine(p0.x, p0.y, p1.x, p1.y, SDL_Color {150, 140, 96, 90});
    }
    scopeLine(plot.x, static_cast<int>(cy), plot.x + side, static_cast<int>(cy), SDL_Color {150, 140, 96, 90});
    scopeLine(static_cast<int>(cx), plot.y, static_cast<int>(cx), plot.y + side, SDL_Color {150, 140, 96, 90});
    const int box = std::max(4, side / 28);
    for (const auto& target : s::vectorTargets(matrix, s::TargetLevel::Percent75)) {
      const SDL_Point p = at(target.point.cb, target.point.cr);
      Primitives::strokeRect(controlRenderer_, SDL_Rect {p.x - box, p.y - box, box * 2, box * 2}, kScopeGrat);
      scopeText(p.x + box + uiScaled(2), p.y - box, target.label, kScopeLabel);
    }
    for (const auto& target : s::vectorTargets(matrix, s::TargetLevel::Percent100)) {
      const SDL_Point p = at(target.point.cb, target.point.cr);
      scopeLine(p.x - box / 2, p.y, p.x + box / 2, p.y, kScopeGrat);
      scopeLine(p.x, p.y - box / 2, p.x, p.y + box / 2, kScopeGrat);
    }
    const s::VectorPoint skin = s::skinToneLine(matrix);
    const SDL_Point edge = at(skin.cb, skin.cr);
    scopeDashed(static_cast<int>(cx), static_cast<int>(cy), edge.x, edge.y, SDL_Color {230, 190, 150, 200});
  }

  void drawDiamondPanel(const SDL_Rect& rect) {
    SDL_Rect plot = scopePanel(rect, "DIAMOND  B'G' / R'G'", kScopeDiamond, uiScaled(14), uiScaled(14));
    // Each half is a square (x -1..1, y 0..2), so the whole is twice as tall as wide.
    const int halfSide = std::min(plot.w, plot.h / 2);
    plot = SDL_Rect {plot.x + (plot.w - halfSide) / 2, plot.y + (plot.h - halfSide * 2) / 2, halfSide, halfSide * 2};
    drawScopeTexture(kScopeDiamond, plot);
    const int cx = plot.x + halfSide / 2, mid = plot.y + halfSide;
    const int top = plot.y, bottom = plot.y + halfSide * 2, left = plot.x, right = plot.x + halfSide;
    const SDL_Color edge {150, 140, 96, 200};
    // Upper diamond: black at the middle, white at the top, G left, B right.
    scopeLine(cx, mid, right, mid - halfSide / 2, edge);
    scopeLine(right, mid - halfSide / 2, cx, top, edge);
    scopeLine(cx, top, left, mid - halfSide / 2, edge);
    scopeLine(left, mid - halfSide / 2, cx, mid, edge);
    // Lower diamond: black at the middle, white at the bottom, G left, R right.
    scopeLine(cx, mid, right, mid + halfSide / 2, edge);
    scopeLine(right, mid + halfSide / 2, cx, bottom, edge);
    scopeLine(cx, bottom, left, mid + halfSide / 2, edge);
    scopeLine(left, mid + halfSide / 2, cx, mid, edge);
    scopeText(right + uiScaled(2), mid - halfSide / 2, "B", kScopeLabel);
    scopeText(right + uiScaled(2), mid + halfSide / 2, "R", kScopeLabel);
    scopeText(left - uiScaled(12), mid - halfSide / 2, "G", kScopeLabel);
    scopeText(left - uiScaled(12), mid + halfSide / 2, "G", kScopeLabel);
  }

  void drawHistogramPanel(const SDL_Rect& rect, const deckboy::core::scopes::Result& result) {
    const SDL_Rect plot = scopePanel(rect, "HISTOGRAM Y'", kScopeHistogram, uiScaled(6), uiScaled(6));
    const auto& bins = result.lumaHistogram.bins;
    std::uint64_t peak = 1;
    for (const auto count : bins) peak = std::max(peak, count);
    const double scale = 1.0 / std::log1p(static_cast<double>(peak));
    const int n = static_cast<int>(bins.size());
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 150, 255, 150, 220);
    for (int i = 0; i < n; ++i) {
      if (!bins[static_cast<std::size_t>(i)]) continue;
      const double v = std::log1p(static_cast<double>(bins[static_cast<std::size_t>(i)])) * scale;
      const int x0 = plot.x + plot.w * i / n, x1 = plot.x + plot.w * (i + 1) / n;
      const int hgt = std::max(1, static_cast<int>(plot.h * v));
      const SDL_FRect bar {static_cast<float>(x0), static_cast<float>(plot.y + plot.h - hgt),
                           static_cast<float>(std::max(1, x1 - x0)), static_cast<float>(hgt)};
      SDL_RenderFillRect(controlRenderer_, &bar);
    }
    const double total = std::max<double>(1.0, static_cast<double>(result.validPixels));
    char crushed[48], clipped[48];
    std::snprintf(crushed, sizeof(crushed), "0%%: %.2f%%", 100.0 * bins.front() / total);
    std::snprintf(clipped, sizeof(clipped), "100%%: %.2f%%", 100.0 * bins.back() / total);
    scopeText(plot.x, plot.y + textLineHeight(fontSmall_) / 2, crushed, kScopeLabel);
    scopeText(plot.x + plot.w, plot.y + textLineHeight(fontSmall_) / 2, clipped, kScopeLabel, true);
  }

  // The R 103 verdict: raw on every update, filtered every two seconds.
  std::string scopesGamutText(const deckboy::core::scopes::Result& result, bool& alarm) {
    deckboy::core::scopes::GamutCounts filtered;
    bool filteredValid = false;
    {
      std::lock_guard<std::mutex> lock(scopesMutex_);
      filtered = scopesFiltered_;
      filteredValid = scopesFilteredValid_;
    }
    const double total = std::max<double>(1.0, static_cast<double>(result.rawGamut.validPixels));
    char text[160];
    if (filteredValid) {
      const double ftotal = std::max<double>(1.0, static_cast<double>(filtered.validPixels));
      alarm = filtered.outOfGamut();
      std::snprintf(text, sizeof(text), "R 103 %s  raw %.2f%%  filtered %.2f%%", alarm ? "OUT" : "OK",
                    100.0 * result.rawGamut.preferred.pixels / total,
                    100.0 * filtered.preferred.pixels / ftotal);
    } else {
      // The verdict is the FILTERED count's; until it lands (two seconds at
      // most) the raw one is shown as raw, never as a verdict. 4:2:2 edge
      // overshoot alone read "OUT" here on a legal picture.
      alarm = false;
      std::snprintf(text, sizeof(text), "R 103 measuring  raw %.2f%%",
                    100.0 * result.rawGamut.preferred.pixels / total);
    }
    return text;
  }

  // The whole scope area, inside the programme monitor.
  void drawScopesArea(const SDL_Rect& area) {
    for (auto& rect : scopesPanelRects_) rect = {};
    scopesFreezeBtnRect_ = {};
    scopesSourceBtnRect_ = {};
    if (area.w < uiScaled(160) || area.h < uiScaled(120)) return;
    Primitives::fillRect(controlRenderer_, area, kScopeBlack);
    ScopesOutcome outcome;
    const auto result = refreshScopeTextures(outcome);
    const int footerH = textLineHeight(fontSmall_) + uiScaled(8);
    const SDL_Rect body {area.x, area.y, area.w, area.h - footerH};
    const SDL_Rect footer {area.x, area.y + area.h - footerH, area.w, footerH};
    scopesDrawHdr_ = outcome.hdr;
    const int source = scopesOutputIndex();
    const bool matches = result && source < static_cast<int>(project_.outputs.size()) &&
      outcome.outputId == project_.outputs[source].outputId;
    if (!matches) {
      const std::string why = project_.outputs.empty()
        ? std::string("add an output to scope it")
        : "waiting for " + outputLabel(scopesOutputIndex()) + " - arm it to scope its signal";
      drawCenteredTextSafe(controlRenderer_, fontSmall_, body, why, kScopeLabel);
    } else if (scopesBig_ >= 0) {
      const int panel = scopesBig_;
      if (panel == kScopeWaveform) drawWaveformPanel(body);
      else if (panel == kScopeParade) drawParadePanel(body);
      else if (panel == kScopeVector) drawVectorPanel(body, outcome.matrix);
      else if (panel == kScopeDiamond) drawDiamondPanel(body);
      else drawHistogramPanel(body, *result);
    } else if (monitorView_ == MonitorView::Both) {
      const int half = body.h / 2;
      drawWaveformPanel(SDL_Rect {body.x, body.y, body.w, half});
      drawVectorPanel(SDL_Rect {body.x, body.y + half, body.w, body.h - half}, outcome.matrix);
    } else {
      // Diamond tall on the left; waveform and parade, vector and histogram.
      const int gap = uiScaled(4);
      const int diamondW = std::max(uiScaled(90), body.w / 5);
      const int restW = body.w - diamondW - gap;
      const int colW = (restW - gap) / 2;
      const int rowH = (body.h - gap) / 2;
      drawDiamondPanel(SDL_Rect {body.x, body.y, diamondW, body.h});
      const int x1 = body.x + diamondW + gap, x2 = x1 + colW + gap;
      drawWaveformPanel(SDL_Rect {x1, body.y, colW, rowH});
      drawParadePanel(SDL_Rect {x1, body.y + rowH + gap, colW, body.h - rowH - gap});
      drawVectorPanel(SDL_Rect {x2, body.y, restW - colW - gap, rowH}, outcome.matrix);
      drawHistogramPanel(SDL_Rect {x2, body.y + rowH + gap, restW - colW - gap, body.h - rowH - gap}, *result);
    }
    // Footer: what is measured, how fresh, the gamut verdict, FREEZE.
    Primitives::fillRect(controlRenderer_, footer, SDL_Color {20, 24, 20, 255});
    const int freezeW = uiScaled(84);
    scopesFreezeBtnRect_ = SDL_Rect {footer.x + footer.w - freezeW - uiScaled(4), footer.y + uiScaled(2),
                                     freezeW, footer.h - uiScaled(4)};
    drawUIPanel(scopesFreezeBtnRect_, paletteToggleFill(scopesFrozen_), pal.deep, pal.light);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, scopesFreezeBtnRect_,
                         scopesFrozen_ ? "FROZEN" : "FREEZE", paletteToggleInk(scopesFrozen_));
    // SOURCE: the programme, or one output, cycled.
    const int sourceW = uiScaled(110);
    scopesSourceBtnRect_ = SDL_Rect {scopesFreezeBtnRect_.x - sourceW - uiScaled(4), scopesFreezeBtnRect_.y,
                                     sourceW, scopesFreezeBtnRect_.h};
    drawUIPanel(scopesSourceBtnRect_, paletteToggleFill(scopesSource_ >= 0), pal.deep, pal.light);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, scopesSourceBtnRect_, scopesSourceLabel(),
                         paletteToggleInk(scopesSource_ >= 0));
    {
      bool alarm = false;
      std::string gamut;
      if (matches && outcome.hdr) {
        // R 103 is an SDR rule; on PQ the reading that matters is how bright.
        char peak[96];
        std::snprintf(peak, sizeof(peak), "peak %.0f nits  SDR white %d", outcome.peakNits, outcome.hdrWhiteNits);
        gamut = peak;
      } else if (matches) {
        gamut = scopesGamutText(*result, alarm);
      }
      const std::string state = scopesReadingState(outcome);
      const bool live = state == "LIVE";
      const double rate = live ? scopesUpdateRate() : 0;
      // The rate says what the scopes kept up with: "25.0/s" when every new
      // picture was analysed, "41.3 of 59.9/s" when some were not. Then the
      // source's own frame rate and the output's, so both can be checked.
      // What it should have kept up with: the source's own frame rate (no
      // faster than the output shows it) when the cue says, else how often
      // the picture was seen to change. The scopes look only when their
      // analyser is free, so the rate they SAW is not the source's rate.
      double expected = live ? scopesPictureRate() : 0;
      char sourceText[48] = "";
      if (source >= 0 && source < static_cast<int>(project_.outputs.size()) && !project_.decks.empty()) {
        const int deck = std::clamp(project_.outputs[source].hostDeckIndex, 0,
                                    static_cast<int>(project_.decks.size()) - 1);
        const Cue* cue = activeCuePtr(deck);
        if (cue && cue->kind == CueKind::Video && cue->fps > 1.0) {
          std::snprintf(sourceText, sizeof(sourceText), "src %.2f fps  ", cue->fps);
          if (live) expected = scopesOutputHz_ > 1.0 ? std::min(cue->fps, scopesOutputHz_) : cue->fps;
        }
      }
      char rateText[48];
      if (expected > 0.5 && rate < expected - 0.5)
        std::snprintf(rateText, sizeof(rateText), "%.1f of %.1f/s", rate, expected);
      else
        std::snprintf(rateText, sizeof(rateText), "%.1f/s", rate);
      char outText[32] = "";
      if (scopesOutputHz_ > 1.0) std::snprintf(outText, sizeof(outText), "out %.2f Hz  ", scopesOutputHz_);
      char info[300];
      std::snprintf(info, sizeof(info), "%s  %s  %s%s%s  %dx%d  %s  %s  ", state.c_str(), rateText,
                    sourceText, outText, outputLabel(source).c_str(),
                    outcome.width, outcome.height, scopesSignalLabel(outcome).c_str(),
                    outcome.matrix == deckboy::core::scopes::Matrix::Bt2020Ncl ? "BT.2020" : "BT.709");
      SDL_Rect infoRect {footer.x + uiScaled(6), footer.y, footer.w - freezeW - sourceW - uiScaled(20), footer.h};
      drawTextSafe(controlRenderer_, fontSmall_, infoRect,
                   matches ? std::string(info) + (live ? gamut : "last reading") : state,
                   alarm && live ? kScopeAlarm : kScopeLabel);
    }
  }

  // What the footer says was measured: the wire signal, and which wire.
  static std::string scopesSignalLabel(const ScopesOutcome& outcome) {
    if (outcome.hdr) return "HDR PQ 16-bit";
    if (!outcome.sentDepth) return "RGB 8-bit";
    return std::string(outcome.sentSdi ? "SDI" : "ST 2110") + " YCbCr 4:2:2 " +
           std::to_string(outcome.sentDepth) + "-bit";
  }

  // ── Control ────────────────────────────────────────────────────────────────

  void cycleMonitorView() {
    monitorView_ = monitorView_ == MonitorView::Program ? MonitorView::Scopes
                 : monitorView_ == MonitorView::Scopes ? MonitorView::Both
                                                       : MonitorView::Program;
    scopesBig_ = -1;
    releaseScopesCapture();
    triggerToast(std::string("monitor: ") + monitorViewName(monitorView_));
  }

  void toggleScopeBig(int panel) {
    scopesBig_ = (scopesBig_ == panel || panel < 0 || panel >= kScopePanelCount) ? -1 : panel;
  }

  // A click inside the scope area: FREEZE, or a panel to show alone (and back).
  bool handleScopesClick(int x, int y) {
    if (monitorView_ == MonitorView::Program) return false;
    if (scopesFreezeBtnRect_.w > 0 && pointInRect(x, y, scopesFreezeBtnRect_)) {
      dispatchQuickAction(QuickAction::ScopesFreeze);
      return true;
    }
    if (scopesSourceBtnRect_.w > 0 && pointInRect(x, y, scopesSourceBtnRect_)) {
      dispatchQuickAction(QuickAction::ScopesSourceCycle);
      return true;
    }
    for (int panel = 0; panel < kScopePanelCount; ++panel) {
      if (scopesPanelRects_[panel].w > 0 && pointInRect(x, y, scopesPanelRects_[panel])) {
        dispatchQuickAction(QuickAction::ScopesToggleBig, panel);
        return true;
      }
    }
    return false;
  }

  // SCOPES STATS: the numbers behind the picture, for tests and for anyone
  // checking a signal without looking at the desk.
  std::string scopesStats() {
    namespace s = deckboy::core::scopes;
    ScopesOutcome outcome;
    std::uint64_t serial;
    {
      std::lock_guard<std::mutex> lock(scopesMutex_);
      outcome = scopesLatest_;
      serial = scopesSerial_;
    }
    const std::string state = scopesReadingState(outcome);
    char telemetry[320];
    std::snprintf(telemetry, sizeof(telemetry), " state=%s serial=%llu hz=%.1f age_ms=%.1f picture_hz=%.1f out_hz=%.2f prepare_ms=%.1f capture_ms=%.1f upload_ms=%.1f",
      state == "OUTPUT OFF" ? "off" : toLower(state).c_str(), static_cast<unsigned long long>(serial),
      state == "LIVE" ? scopesUpdateRate() : 0,
      outcome.capturedNs ? (SDL_GetTicksNS() - outcome.capturedNs) / 1000000.0 : -1.0,
      state == "LIVE" ? scopesPictureRate() : 0, scopesOutputHz_,
      outcome.prepareMs, scopesCaptureMs_, scopesUploadMs_);
    if (!outcome.result) return std::string(telemetry + 1) + " no frame yet";
    const s::Result& r = *outcome.result;
    const auto& hist = r.lumaHistogram.bins;
    int lo = -1, hi = -1;
    for (int i = 0; i < static_cast<int>(hist.size()); ++i) {
      if (hist[static_cast<std::size_t>(i)]) { if (lo < 0) lo = i; hi = i; }
    }
    auto hits = [&](s::TargetLevel level) {
      int hit = 0;
      const auto& v = r.vectorscope;
      for (const auto& target : s::vectorTargets(outcome.matrix, level)) {
        std::uint64_t closeBy = 0;
        for (std::size_t row = 0; row < v.height; ++row) {
          for (std::size_t col = 0; col < v.width; ++col) {
            const double cb = v.xRange.low + (col + 0.5) * (v.xRange.high - v.xRange.low) / v.width;
            const double cr = v.yRange.low + (row + 0.5) * (v.yRange.high - v.yRange.low) / v.height;
            if (std::hypot(cb - target.point.cb, cr - target.point.cr) <= 0.012) closeBy += v.bins[row * v.width + col];
          }
        }
        if (static_cast<double>(closeBy) > 0.01 * static_cast<double>(r.validPixels)) ++hit;
      }
      return hit;
    };
    std::uint64_t diamondOut = 0;
    for (const auto& d : r.diamond) {
      diamondOut += d.xExcursions.below + d.xExcursions.above + d.yExcursions.below + d.yExcursions.above;
    }
    // Levels that hold at least 2% of the picture, as %, and how even the
    // histogram is over the bins in use (1.0 for a ramp covering every code).
    std::string peaks;
    std::uint64_t binMin = 0, binMax = 0;
    int binsUsed = 0;
    for (int i = 0; i < static_cast<int>(hist.size()); ++i) {
      const std::uint64_t count = hist[static_cast<std::size_t>(i)];
      if (!count) continue;
      ++binsUsed;
      binMin = binMin ? std::min(binMin, count) : count;
      binMax = std::max(binMax, count);
      if (static_cast<double>(count) >= 0.02 * static_cast<double>(r.validPixels)) {
        char one[16];
        std::snprintf(one, sizeof(one), "%s%.1f", peaks.empty() ? "" : ",",
                      100.0 * (i + 0.5) / static_cast<double>(hist.size()));
        peaks += one;
      }
    }
    bool alarm = false;
    std::string gamut = scopesGamutText(r, alarm);
    if (outcome.hdr) {
      char hdr[64];
      std::snprintf(hdr, sizeof(hdr), "peak_nits=%.0f sdr_white=%d", outcome.peakNits, outcome.hdrWhiteNits);
      gamut = hdr;
    }
    char spread[96];
    std::snprintf(spread, sizeof(spread), " luma_peaks=%s hist_bins=%d hist_ratio=%.2f",
                  peaks.empty() ? "-" : peaks.c_str(), binsUsed,
                  binMin ? static_cast<double>(binMax) / static_cast<double>(binMin) : 0.0);
    char text[400];
    std::snprintf(text, sizeof(text),
                  "output=%d frame=%dx%d pixels=%llu ms=%.1f luma=%.1f..%.1f%% targets75=%d/6 targets100=%d/6 "
                  "diamond_out=%llu signal=%s %s",
                  outcome.output + 1, outcome.width, outcome.height,
                  static_cast<unsigned long long>(r.validPixels), outcome.analysisMs,
                  lo < 0 ? 0.0 : 100.0 * lo / std::max<std::size_t>(1, hist.size() - 1),
                  hi < 0 ? 0.0 : 100.0 * hi / std::max<std::size_t>(1, hist.size() - 1),
                  hits(s::TargetLevel::Percent75), hits(s::TargetLevel::Percent100),
                  static_cast<unsigned long long>(diamondOut),
                  outcome.hdr ? "pq16"
                    : outcome.sentDepth ? (outcome.sentDepth == 10 ? "ycbcr422-10" : "ycbcr422-8") : "rgb8",
                  gamut.c_str());
    return std::string(text) + spread + telemetry;
  }
