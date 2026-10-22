// Included inside App, after the scope state. Renderer work stays on the main
// thread; one coordinator and a reusable analysis pool own the CPU work.

  void invalidateScopes() {
    ++scopesGeneration_;
    std::lock_guard lock(scopesMutex_);
    scopesPending_.reset();
    scopesDrawPending_.reset();
    scopesFilteredValid_ = false;
    scopesFilteredAtNs_ = 0;
    scopesUpdateTimes_.clear();
    scopesLastFingerprint_ = 0;
    scopesConfirmedNs_ = 0;
    scopesNextProbeNs_ = 0;
    scopesPictureTimes_.clear();
  }

  void releaseScopesCapture(SDL_Renderer* renderer = nullptr) {
    if (renderer && renderer != scopesCaptureRenderer_) return;
    if (scopesReadback_) deckboy::gpu::destroyStagingReadback(scopesReadback_);
    scopesReadback_ = nullptr;
    if (scopesCaptureTexture_) SDL_DestroyTexture(scopesCaptureTexture_);
    scopesCaptureTexture_ = nullptr;
    scopesCaptureRenderer_ = nullptr;
    scopesCaptureKey_.clear();
    scopesReadbackUnavailable_ = false;
    scopesReadbackMisses_ = 0;
    scopesLastWireCaptureNs_ = 0;
    invalidateScopes();
  }

  void setScopesFrozen(bool frozen) {
    if (scopesFrozen_ == frozen) return;
    scopesFrozen_ = frozen;
    releaseScopesCapture();
  }

  std::shared_ptr<std::vector<std::uint8_t>> scopeBuffer(std::size_t size) {
    for (auto& buffer : scopesBuffers_) if (buffer.use_count() == 1) {
      buffer->resize(size);
      return buffer;
    }
    // Current capture, queued latest frame, worker, slow gamut check. No queue
    // can grow behind a slow analyser; exhausted buffers simply skip capture.
    if (scopesBuffers_.size() >= 4) return {};
    auto buffer = std::make_shared<std::vector<std::uint8_t>>(size);
    scopesBuffers_.push_back(buffer);
    return buffer;
  }

  void queueScopeJob(ScopeJob job) {
    if (!scopesWorker_.joinable()) {
      scopesWorker_ = std::thread([this] {
        namespace s = deckboy::core::scopes;
        // A quarter of the machine, 2 to 6 threads: enough for 1080p at the
        // source's rate, and a 4K picture never takes the cores the decoders
        // and the outputs need.
        const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
        s::ParallelAnalyzer analyzer(std::clamp(cores / 4, 2u, 6u));
        std::unique_ptr<deckboy::core::hdr::HdrEncoder> encoder;
        int white = 0;
        for (;;) {
          ScopeJob next;
          {
            std::unique_lock lock(scopesMutex_);
            scopesWake_.wait(lock, [&] { return scopesStopping_ || scopesPending_.has_value(); });
            if (scopesStopping_) return;
            next = std::move(*scopesPending_);
            scopesPending_.reset();
            scopesAnalysing_ = true;
          }
          struct Idle { std::atomic<bool>& flag; ~Idle() { flag = false; } } analysingIdle{scopesAnalysing_};
          try {
            auto& outcome = next.outcome;
            const auto started = SDL_GetTicksNS();
            s::Options options = scopesOptions(outcome.width, outcome.matrix == s::Matrix::Bt2020Ncl);
            const auto w = static_cast<std::size_t>(outcome.width);
            const auto h = static_cast<std::size_t>(outcome.height);
            if (outcome.hdr) {
              if (!encoder || white != outcome.hdrWhiteNits) {
                white = outcome.hdrWhiteNits;
                encoder = std::make_unique<deckboy::core::hdr::HdrEncoder>(white);
              }
              std::vector<std::uint16_t> pq(w * h * 3);
              if (next.halfFloat) {
                encoder->convertHalf(reinterpret_cast<const std::uint16_t*>(next.pixels->data()),
                  outcome.width, outcome.height, outcome.width * 4, pq.data());
              } else if (next.raw->format == SDL_PIXELFORMAT_RGBA64_FLOAT) {
                encoder->convertHalf(static_cast<const std::uint16_t*>(next.raw->pixels),
                  outcome.width, outcome.height, next.raw->pitch / 2, pq.data());
              } else {
                std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> floating(
                  SDL_ConvertSurfaceAndColorspace(next.raw.get(), SDL_PIXELFORMAT_RGBA128_FLOAT,
                    nullptr, SDL_COLORSPACE_SRGB_LINEAR, 0), SDL_DestroySurface);
                if (!floating) continue;
                encoder->convert(static_cast<const float*>(floating->pixels), outcome.width,
                  outcome.height, floating->pitch / 4, pq.data());
              }
              options.histogramBins = 1024;
              outcome.result = std::make_shared<const s::Result>(analyzer.analyze(s::Rgb16View{
                {reinterpret_cast<const std::uint8_t*>(pq.data()), pq.size() * 2}, w, h}, options));
              double peak = 0;
              for (const auto& histogram : outcome.result->rgbHistogram)
                for (std::size_t i = histogram.bins.size(); i-- > 0;) if (histogram.bins[i]) {
                  peak = std::max(peak, (static_cast<double>(i) + 0.5) / histogram.bins.size());
                  break;
                }
              outcome.peakNits = peak > 0 ? s::pqToNits(peak) : 0;
            } else if (outcome.sentDepth) {
              outcome.result = std::make_shared<const s::Result>(analyzer.analyze(
                s::Bgra422View{*next.pixels, w, h, 0, outcome.sentDepth}, options));
            } else {
              outcome.result = std::make_shared<const s::Result>(analyzer.analyze(
                s::Rgba8View{*next.pixels, w, h, 0, true}, options));
            }
            outcome.analysisMs = (SDL_GetTicksNS() - started) / 1000000.0;
            {
              std::lock_guard lock(scopesMutex_);
              if (outcome.generation != scopesGeneration_.load()) continue;
              scopesDrawPending_ = std::move(outcome); // the newest analysis wins
            }
            scopesDrawWake_.notify_one();
          } catch (const std::exception& error) {
            std::cerr << "scopes: " << error.what() << '\n';
          }
        }
      });
      scopesDrawWorker_ = std::thread([this] {
        for (;;) {
          ScopesOutcome outcome;
          {
            std::unique_lock lock(scopesMutex_);
            scopesDrawWake_.wait(lock, [&] { return scopesStopping_ || scopesDrawPending_.has_value(); });
            if (scopesStopping_) return;
            outcome = std::move(*scopesDrawPending_);
            scopesDrawPending_.reset();
            scopesDrawing_ = true;
          }
          struct Idle { std::atomic<bool>& flag; ~Idle() { flag = false; } } drawingIdle{scopesDrawing_};
          try {
            const auto started = SDL_GetTicksNS();
            outcome.images = buildScopeImages(outcome.result);
            outcome.completedNs = SDL_GetTicksNS();
            outcome.prepareMs = (outcome.completedNs - started) / 1000000.0;
            std::lock_guard lock(scopesMutex_);
            if (outcome.generation != scopesGeneration_.load()) continue;
            if (!outcome.sentDepth) {
              scopesFiltered_ = outcome.result->filteredGamut;
              scopesFilteredValid_ = true;
              scopesFilteredAtNs_ = outcome.capturedNs;
            }
            scopesUpdateTimes_.push_back(outcome.completedNs);
            while (!scopesUpdateTimes_.empty() && outcome.completedNs - scopesUpdateTimes_.front() > 2000000000ULL)
              scopesUpdateTimes_.pop_front();
            scopesLatest_ = std::move(outcome);
            ++scopesSerial_;
          } catch (const std::exception& error) {
            std::cerr << "scopes: " << error.what() << '\n';
          }
        }
      });
    }
    {
      std::lock_guard lock(scopesMutex_);
      scopesPending_ = std::move(job); // replace an unprocessed frame; never build latency
    }
    scopesWake_.notify_one();
  }

  void offerScopeFilter(const ScopeJob& job) {
    const Uint64 now = SDL_GetTicks();
    if (!job.outcome.sentDepth || scopesFilterBusy_.load() || now - scopesLastFilterMs_ < 2000) return;
    scopesLastFilterMs_ = now;
    if (scopesFilterWorker_.joinable()) scopesFilterWorker_.join();
    scopesFilterBusy_.store(true);
    scopesFilterWorker_ = std::thread([this, pixels = job.pixels, outcome = job.outcome] {
      namespace s = deckboy::core::scopes;
      try {
        s::Options options = scopesOptions(outcome.width, false);
        options.columns = options.levelBins = options.vectorBins = options.histogramBins = 2;
        options.gamutFilter = s::GamutFilter::Progressive;
        const s::Bgra422View view{*pixels, static_cast<std::size_t>(outcome.width),
          static_cast<std::size_t>(outcome.height), 0, outcome.sentDepth};
        const auto filtered = s::filteredGamutRows(view, options, 0, view.height);
        std::lock_guard lock(scopesMutex_);
        if (outcome.generation == scopesGeneration_.load()) {
          scopesFiltered_ = filtered;
          scopesFilteredValid_ = true;
          scopesFilteredAtNs_ = outcome.capturedNs;
        }
      } catch (const std::exception&) {}
      scopesFilterBusy_.store(false);
    });
  }

  // A full-raster private tap: never changes a recording/network sender's
  // rate or pixel buffer. Every presented frame is looked at; only a new picture is analysed.
  // Is a picture already on its way through analysis or drawing? Then reading
  // another back now would only be thrown away.
  bool scopesPipelineBusy() {
    if (scopesAnalysing_ || scopesDrawing_) return true;
    std::lock_guard lock(scopesMutex_);
    return scopesPending_.has_value() || scopesDrawPending_.has_value();
  }

  void scopesCaptureFrame(int index, OutputRuntime& runtime, const SDL_Rect& rect) {
    if (!scopesWatching(index) || !runtime.outputRenderer) return;
    ScopeJob job;
    auto& outcome = job.outcome;
    outcome.output = index;
    outcome.outputId = project_.outputs[index].outputId;
    outcome.sentDepth = scopesSentDepth(index, &outcome.sentSdi);
    outcome.hdr = scopesReadsHdr(index, runtime);
    outcome.hdrWhiteNits = std::clamp(project_.hdrSdrWhiteNits, 80, 500);
    outcome.matrix = (outcome.hdr || (scopesBt2020_ && !outcome.sentDepth))
      ? deckboy::core::scopes::Matrix::Bt2020Ncl : deckboy::core::scopes::Matrix::Bt709;
    outcome.width = outcome.hdr ? runtime.compositorWidth : rect.w;
    outcome.height = outcome.hdr ? runtime.compositorHeight : rect.h;
    if (outcome.width <= 0 || outcome.height <= 0) return;
    const int orientation = normalizeOutputOrientationDegrees(project_.outputs[index].outputOrientationDegrees);
    const auto key = outcome.outputId + ":" + std::to_string(index) + ":" +
      std::to_string(outcome.width) + ":" + std::to_string(outcome.height) + ":" +
      std::to_string(rect.x) + ":" + std::to_string(rect.y) + ":" + std::to_string(orientation) + ":" +
      std::to_string(outcome.hdr) + ":" + std::to_string(outcome.hdrWhiteNits) + ":" +
      std::to_string(outcome.sentDepth) + ":" + std::to_string(scopesBt2020_);
    if (key != scopesCaptureKey_ || runtime.outputRenderer != scopesCaptureRenderer_) {
      releaseScopesCapture();
      scopesCaptureKey_ = key;
      scopesCaptureRenderer_ = runtime.outputRenderer;
    }
    const auto now = SDL_GetTicksNS();
    scopesOutputHz_ = runtime.fpsMeasured;
    outcome.generation = scopesGeneration_.load();
    outcome.capturedNs = now;
    const auto bytes = static_cast<std::size_t>(outcome.width) * outcome.height * (outcome.hdr ? 8 : 4);
    // Look only when the analyser can take the picture, and at a still one
    // only every 250 ms. Otherwise the GPU ring is kept moving (a copy on the
    // GPU, nothing read) so the frame read next time is a recent one.
    const bool want = now >= scopesNextProbeNs_ && !scopesPipelineBusy();
    if (want) {
      job.pixels = scopeBuffer(bytes);
      if (!job.pixels) return;
    } else if (!scopesReadback_) {
      return;
    }

    SDL_Texture* source = runtime.compositorTexture;
    // Float-to-SDR wire scopes use the egress path's explicit colour conversion.
    const bool hdrWire = runtime.hdr && !outcome.hdr;
    if (!outcome.hdr && !hdrWire && source) {
      if (!scopesCaptureTexture_) scopesCaptureTexture_ = deckboyCreateTexture(runtime.outputRenderer,
        SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_TARGET, rect.w, rect.h);
      if (scopesCaptureTexture_) {
        SDL_Texture* previous = SDL_GetRenderTarget(runtime.outputRenderer);
        SDL_BlendMode blend;
        SDL_GetTextureBlendMode(source, &blend);
        SDL_SetTextureBlendMode(source, SDL_BLENDMODE_NONE);
        SDL_SetRenderTarget(runtime.outputRenderer, scopesCaptureTexture_);
        const SDL_FRect region{static_cast<float>(rect.x), static_cast<float>(rect.y),
          static_cast<float>(rect.w), static_cast<float>(rect.h)};
        SDL_RenderTexture(runtime.outputRenderer, source, &region, nullptr);
        SDL_SetRenderTarget(runtime.outputRenderer, previous);
        SDL_SetTextureBlendMode(source, blend);
        source = scopesCaptureTexture_;
      }
    }
    if (!scopesReadback_ && !scopesReadbackUnavailable_ && source && !hdrWire &&
        (outcome.hdr ? source->format == SDL_PIXELFORMAT_RGBA64_FLOAT : source == scopesCaptureTexture_) &&
        egressReadbackMode() == kEgressReadbackAuto) {
      scopesReadback_ = deckboy::gpu::createStagingReadback(runtime.outputRenderer,
        outcome.width, outcome.height, outcome.hdr);
      scopesReadbackUnavailable_ = !scopesReadback_;
    }
    bool captured = false;
    if (scopesReadback_) {
      SDL_FlushRenderer(runtime.outputRenderer);
      if (!want) {
        deckboy::gpu::stagingReadbackFrame(scopesReadback_, source, nullptr, 0,
                                           outcome.width, outcome.height, now, nullptr);
        return;
      }
      captured = deckboy::gpu::stagingReadbackFrame(scopesReadback_, source, job.pixels->data(),
        job.pixels->size(), outcome.width, outcome.height, now, &outcome.capturedNs);
      if (captured) scopesReadbackMisses_ = 0;
      else if (++scopesReadbackMisses_ < 60) return;
      else {
        deckboy::gpu::destroyStagingReadback(scopesReadback_);
        scopesReadback_ = nullptr;
        scopesReadbackUnavailable_ = true;
      }
      job.halfFloat = outcome.hdr;
    }
    if (!captured && outcome.hdr) {
      job.raw.reset(readHdrCompositorRaw(runtime), SDL_DestroySurface);
      if (!job.raw) return;
      job.halfFloat = false;
      captured = true;
    } else if (!captured) {
      // Portable fallback, including the explicit HDR-to-SDR wire transform.
      if (hdrWire) {
        const auto& frame = runtime.latestCapturedFrame;
        if (frame.width <= 0 || frame.height <= 0 || frame.pixels.empty()) return;
        *job.pixels = frame.pixels;
        outcome.width = frame.width; outcome.height = frame.height;
        outcome.capturedNs = frame.capturedAtMs * 1000000ULL;
        if (outcome.capturedNs <= scopesLastWireCaptureNs_) return;
        scopesLastWireCaptureNs_ = outcome.capturedNs;
        captured = true; // egress already applies orientation
      } else {
        SDL_Texture* previous = SDL_GetRenderTarget(runtime.outputRenderer);
        SDL_SetRenderTarget(runtime.outputRenderer, source);
        const SDL_Rect area = source == scopesCaptureTexture_ ? SDL_Rect{0, 0, rect.w, rect.h} : rect;
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
          SDL_RenderReadPixels(runtime.outputRenderer, &area), SDL_DestroySurface);
        SDL_SetRenderTarget(runtime.outputRenderer, previous);
        if (surface && surface->w == outcome.width && surface->h == outcome.height)
          captured = SDL_ConvertPixels(surface->w, surface->h, surface->format,
          surface->pixels, surface->pitch, SDL_PIXELFORMAT_BGRA32, job.pixels->data(), outcome.width * 4);
      }
    }
    if (!captured) return;
    // Analyse a picture once. A 25 fps clip on a 60 Hz output presents each
    // frame two or three times; only the first is new.
    const std::uint64_t fingerprint = job.raw
      ? scopePictureFingerprint(static_cast<const std::uint8_t*>(job.raw->pixels),
          static_cast<std::size_t>(job.raw->pitch), static_cast<std::size_t>(job.raw->h))
      : scopePictureFingerprint(job.pixels->data(), job.pixels->size() /
          std::max(1, outcome.height), static_cast<std::size_t>(outcome.height));
    if (fingerprint == scopesLastFingerprint_) {
      // Unchanged: the last reading is still the reading of what is on air.
      // Without this a still picture was reported STALE half a second in.
      scopesConfirmedNs_ = now;
      scopesNextProbeNs_ = now + 250000000ULL;
      // The slow 4:2:2 gamut check paces itself (every two seconds, never
      // while busy). A still picture is offered once otherwise, and if the
      // check was busy then, its verdict never arrived.
      offerScopeFilter(job);
      return;
    }
    scopesLastFingerprint_ = fingerprint;
    scopesNextProbeNs_ = 0;
    notePictureChanged(now);
    if (!outcome.hdr && !hdrWire && orientation) {
      std::vector<std::uint8_t> rotated;
      int w = outcome.width, h = outcome.height;
      if (!rotateCapturedFramePixels(*job.pixels, w, h, orientation, rotated, outcome.width, outcome.height)) return;
      job.pixels->swap(rotated);
    }
    offerScopeFilter(job);
    scopesCaptureMs_ = (SDL_GetTicksNS() - now) / 1000000.0;
    queueScopeJob(std::move(job));
  }

  const char* scopesReadingState(const ScopesOutcome& outcome) const {
    const int index = scopesOutputIndex();
    if (index < 0 || index >= static_cast<int>(project_.outputs.size()) || !project_.outputs[index].enabled)
      return "OUTPUT OFF";
    if (!outcome.result || outcome.outputId != project_.outputs[index].outputId) return "WAITING";
    if (scopesFrozen_) return "FROZEN";
    if (monitorView_ == MonitorView::Program) return "HIDDEN";
    const Uint64 seen = std::max(outcome.capturedNs, scopesConfirmedNs_);
    if (outcome.generation != scopesGeneration_.load() || SDL_GetTicksNS() - seen > 500000000ULL)
      return "STALE";
    return "LIVE";
  }

  // A sampled hash: every third row, every fifth 32-bit word, plus the size.
  // Any change bigger than a few pixels moves it; it costs a fraction of a
  // millisecond at 4K, which matters because it runs on the render thread.
  static std::uint64_t scopePictureFingerprint(const std::uint8_t* data, std::size_t rowBytes,
                                               std::size_t rows) {
    std::uint64_t hash = 1469598103934665603ULL ^ (rowBytes * 0x9E3779B97F4A7C15ULL) ^ rows;
    if (!data || rowBytes < 4) return hash;
    const std::size_t words = rowBytes / 4;
    for (std::size_t y = 0; y < rows; y += 3) {
      const std::uint8_t* row = data + y * rowBytes;
      for (std::size_t x = (y / 3) % 5; x < words; x += 5) {
        std::uint32_t word;
        std::memcpy(&word, row + x * 4, 4);
        hash = (hash ^ word) * 1099511628211ULL;
      }
    }
    return hash ? hash : 1;
  }

  void notePictureChanged(Uint64 now) {
    scopesPictureTimes_.push_back(now);
    while (!scopesPictureTimes_.empty() && now - scopesPictureTimes_.front() > 2000000000ULL)
      scopesPictureTimes_.pop_front();
  }

  // How often the scoped picture changed: the source's frame rate as it
  // reaches the wire (or the output's rate while something animates).
  double scopesPictureRate() {
    const auto now = SDL_GetTicksNS();
    while (!scopesPictureTimes_.empty() && now - scopesPictureTimes_.front() > 2000000000ULL)
      scopesPictureTimes_.pop_front();
    if (scopesPictureTimes_.size() < 2) return 0;
    return 1000000000.0 * (scopesPictureTimes_.size() - 1) /
      std::max<Uint64>(1, now - scopesPictureTimes_.front());
  }

  double scopesUpdateRate() {
    std::lock_guard lock(scopesMutex_);
    const auto now = SDL_GetTicksNS();
    while (!scopesUpdateTimes_.empty() && now - scopesUpdateTimes_.front() > 2000000000ULL)
      scopesUpdateTimes_.pop_front();
    if (scopesUpdateTimes_.size() < 2) return 0;
    return 1000000000.0 * (scopesUpdateTimes_.size() - 1) /
      std::max<Uint64>(1, now - scopesUpdateTimes_.front());
  }

  void stopScopesWorkers() {
    {
      std::lock_guard lock(scopesMutex_);
      scopesStopping_ = true;
      scopesPending_.reset();
      scopesDrawPending_.reset();
    }
    scopesWake_.notify_all();
    scopesDrawWake_.notify_all();
    if (scopesWorker_.joinable()) scopesWorker_.join();
    if (scopesDrawWorker_.joinable()) scopesDrawWorker_.join();
    if (scopesFilterWorker_.joinable()) scopesFilterWorker_.join();
    releaseScopesCapture();
    for (auto*& texture : scopeTex_) {
      if (texture) SDL_DestroyTexture(texture);
      texture = nullptr;
    }
  }
