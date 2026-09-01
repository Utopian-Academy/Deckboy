// ═══════════════════════════════════════════════════════════════════════════
// project_file.ipp — reading and writing a .deckboy show.
//
// Lifted out of main.cpp, which held the application class, the CLI, the
// inspector and this. The show file is the part hardest to work on and easiest
// to get wrong, and it was in the middle of the largest file in the project.
//
// INCLUDED, not compiled separately: these are free functions in main.cpp's
// anonymous namespace and they lean on the cue and colour helpers declared
// above the include point. The include sits exactly where they used to be, so
// every name they see is the one they saw before.
//
// THE RECORD IS POSITIONAL. Fields are addressed as `base + N`, so a field
// inserted into the middle of the writer shifts every column after it and the
// reader goes on addressing the old positions -- silently, because the wrong
// values clamp back into range and look plausible. APPEND to the end of a
// record, never insert into the middle, and run tools/audit_record_layout.py,
// which compares the writer's order against the reader's and is the only
// check that catches this.
// ═══════════════════════════════════════════════════════════════════════════
bool saveProject(const fs::path& projectFile, const Project& project) {
  fs::path resolved = projectFile.empty() ? Paths::defaultProjectFile() : projectFile;
  if (resolved.has_parent_path()) {
    fs::create_directories(resolved.parent_path());
  }

  // WRITE BESIDE IT, THEN RENAME OVER IT.
  //
  // This opened the operator's show with ios::trunc and wrote in place, so the
  // file was empty from the instant the save began until it finished. Anything
  // interrupting the write -- a crash, a power cut, a full disk, a USB stick
  // pulled -- left a truncated show, and the previous good copy was already
  // gone. That is not a rare window: the app writes this file 300ms after any
  // edit, and after unattended metadata repairs, so it saves constantly during
  // a show.
  //
  // A rename within one directory is atomic, so a reader sees either the whole
  // old show or the whole new one, never a half-written file. The temp sits
  // beside the target rather than in the system temp dir because a rename
  // across volumes is a copy, which reopens the same window.
  const fs::path temp = resolved.parent_path().empty()
    ? fs::path(resolved.string() + ".saving")
    : (resolved.parent_path() / (resolved.filename().string() + ".saving"));
  std::ofstream output(temp, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }

  output << "title\t" << escapeField(project.title) << '\n';
  output << "focused_deck\t" << project.focusedDeckIndex << '\n';
  output << "focused_output\t" << project.focusedOutputIndex << '\n';
  output << "advanced_mode\t" << (project.advancedOutputMode ? 1 : 0) << '\n';
  output << "ptp_domain\t" << project.ptpDomain << '\n';
  output << "nmos_enabled\t" << (project.nmosEnabled ? 1 : 0) << '\n';
  output << "nmos_registry\t" << escapeField(project.nmosRegistryUrl) << '\n';
  output << "nmos_port\t" << project.nmosPort << '\n';
  output << "nmos_interface\t" << escapeField(project.nmosInterfaceName) << '\n';
  output << "ltc_out\t" << (project.ltcOutputEnabled ? 1 : 0) << '\n';
  output << "ltc_out_device\t" << escapeField(project.ltcOutputDeviceName) << '\n';
  output << "midi_device\t" << escapeField(project.midiDeviceName) << '\n';
  output << "update_check\t" << (project.updateCheckEnabled ? 1 : 0) << '\n';
  output << "ltc_out_fps\t" << project.ltcOutputFps << '\n';
  output << "ui_sounds\t" << (project.uiSoundsEnabled ? 1 : 0) << '\n';
  output << "ui_transitions\t" << (project.uiTransitionsEnabled ? 1 : 0) << '\n';
  output << "splash_character\t" << escapeField(project.splashCharacter) << '\n';
  output << "recording_dir\t" << escapeField(project.recordingDir) << '\n';
  // The recording FORMAT is part of the show. An operator who set 1080p25
  // ProRes with drop-frame timecode must get it back tomorrow, not the
  // defaults — a setting that does not round-trip is a setting that does not
  // exist, which is exactly what the cue-kind bug taught.
  output << "recording_width\t" << project.recordingWidth << '\n';
  output << "recording_height\t" << project.recordingHeight << '\n';
  output << "recording_fps\t" << project.recordingFps << '\n';
  output << "recording_codec\t" << escapeField(project.recordingCodec) << '\n';
  output << "recording_tc_mode\t" << escapeField(project.recordingTimecodeMode) << '\n';
  output << "recording_tc_start\t" << escapeField(project.recordingTimecodeStart) << '\n';
  output << "recording_tc_df\t" << escapeField(project.recordingTimecodeDropFrame) << '\n';
  output << "recording_segment_minutes\t" << project.recordingSegmentMinutes << '\n';
  output << "recording_segment_mb\t" << project.recordingSegmentMegabytes << '\n';
  output << "recording_remux\t" << (project.recordingRemuxOnStop ? 1 : 0) << '\n';
  output << "asio_driver\t" << escapeField(project.asioDriverName) << '\n';
  output << "audio_input_device\t" << escapeField(project.audioInputDeviceName) << '\n';
  output << "synth_keyboard\t" << (project.synthKeyboardEnabled ? 1 : 0) << '\n';
  output << "synth_octave\t" << project.synthKeyboardOctave << '\n';
  output << "midi_to_synth\t" << (project.midiToSynth ? 1 : 0) << '\n';
  output << "audio_input_enabled\t" << (project.audioInputEnabled ? 1 : 0) << '\n';
  output << "audio_input_gain_db\t" << project.audioInputGainDb << '\n';
  output << "audio_input_to_program\t" << (project.audioInputToProgram ? 1 : 0) << '\n';
  output << "audio_input_mono\t" << (project.audioInputMono ? 1 : 0) << '\n';
  output << "asio_channels\t" << project.asioChannels << '\n';
  output << "hap_suggestion_dismissed\t" << (project.hapSuggestionDismissed ? 1 : 0) << '\n';
  output << "theme\t" << escapeField(project.theme) << '\n';
  output << "terrarium_unlocked\t" << (project.terrariumUnlocked ? 1 : 0) << '\n';
  output << "geometry_aspect_link\t" << (project.geometryAspectLinked ? 1 : 0) << '\n';
  output << "ui_scale\t" << project.uiScale << '\n';
  output << "creatures\t" << (project.creaturesEnabled ? 1 : 0) << '\n';
  output << "creatures_while_live\t" << (project.creaturesWhileLive ? 1 : 0) << '\n';
  output << "show_control_device\t" << project.showControlDeviceId << '\n';
  // VJ mode. Written as project scalars so a show that never turns it on
  // carries the defaults and behaves exactly as it always did.
  output << "vj_mode\t" << (project.vjModeEnabled ? 1 : 0) << '\n';
  output << "vj_deck_a\t" << project.vjDeckA << '\n';
  output << "vj_deck_b\t" << project.vjDeckB << '\n';
  output << "vj_mix\t" << project.vjMixPosition << '\n';
  output << "vj_blend\t" << project.vjBlendMode << '\n';
  output << "vj_bpm\t" << project.vjTempoBpm << '\n';
  output << "vj_quantise\t" << (project.vjQuantiseTakes ? 1 : 0) << '\n';
  output << "interaction_mode\t" << escapeField(project.interactionMode) << '\n';
  output << "allow_remote_network\t" << (project.allowRemoteNetwork ? 1 : 0) << '\n';
  output << "osc_query_enabled\t" << (project.oscQueryEnabled ? 1 : 0) << '\n';
  output << "osc_query_port\t" << project.oscQueryPort << '\n';
  output << "osc_feedback_mirror\t" << (project.oscFeedbackMirrorEnabled ? 1 : 0) << '\n';
  output << "osc_feedback_rate_ms\t" << project.oscFeedbackRateMs << '\n';
  output << "integration_atem_trigger\t" << (project.atemTriggerEnabled ? 1 : 0) << '\n';
  output << "integration_ndi_trigger\t" << (project.ndiTriggerEnabled ? 1 : 0) << '\n';
  output << "integration_nmc_sync\t" << (project.nmcSyncEnabled ? 1 : 0) << '\n';
  output << "integration_mtc_ingest\t" << (project.mtcIngestEnabled ? 1 : 0) << '\n';
  output << "integration_ltc_ingest\t" << (project.ltcIngestEnabled ? 1 : 0) << '\n';
  output << "integration_dmx_artnet\t" << (project.dmxArtNetEnabled ? 1 : 0) << '\n';
  output << "integration_artnet_port\t" << project.artNetPort << '\n';
  output << "integration_tsl_tally\t" << (project.tslTallyEnabled ? 1 : 0) << '\n';
  output << "integration_tsl_port\t" << project.tslTallyPort << '\n';
  output << "integration_tsl_address\t" << escapeField(project.tslTallyAddress) << '\n';
  output << "audio_buffer_samples\t" << project.audioBufferSamples << '\n';
  output << "audio_delay_ms\t" << project.audioDelayMs << '\n';
  output << "jump_mode\t" << escapeField(project.jumpMode) << '\n';
  output << "jump_transition\t" << (project.jumpTransitionEnabled ? 1 : 0) << '\n';
  output << "panic_profile\t" << escapeField(project.panicProfile) << '\n';
  output << "panic_fade_seconds\t" << project.panicFadeSeconds << '\n';
  output << "panic_auto_restore\t" << (project.panicAutoRestore ? 1 : 0) << '\n';
  output << "master_volume\t" << project.masterVolume << '\n';
  output << "master_dimmer\t" << project.masterDimmer << '\n';
  output << "output_follow_display\t" << (project.outputFollowDisplay ? 1 : 0) << '\n';
  output << "output_render_width\t" << project.outputRenderWidth << '\n';
  output << "output_render_height\t" << project.outputRenderHeight << '\n';
  output << "output_refresh_hz\t" << project.outputRefreshRateHz << '\n';
  output << "output_bit_depth\t" << project.outputBitDepth << '\n';
  output << "output_canvas_enabled\t" << (project.outputCanvasEnabled ? 1 : 0) << '\n';
  output << "output_canvas_width\t" << project.outputCanvasWidth << '\n';
  output << "output_canvas_height\t" << project.outputCanvasHeight << '\n';
  for (size_t outputIndex = 0; outputIndex < project.outputs.size(); ++outputIndex) {
    const auto& outputTarget = project.outputs[outputIndex];
    output
      << "output_target\t"
      << outputIndex << '\t'
      << escapeField(outputTarget.name) << '\t'
      << outputTarget.hostDeckIndex << '\t'
      << outputTarget.displayIndex << '\t'
      << (outputTarget.enabled ? 1 : 0) << '\t'
      << (outputTarget.streamEnabled ? 1 : 0) << '\t'
      << escapeField(outputTarget.streamProtocol) << '\t'
      << escapeField(outputTarget.streamUrl) << '\t'
      << outputTarget.streamBitrateKbps << '\t'
      << (outputTarget.ndiEnabled ? 1 : 0) << '\t'
      << escapeField(outputTarget.ndiSourceName) << '\t'
      << (outputTarget.ndiKeyEnabled ? 1 : 0) << '\t'
      << escapeField(outputTarget.ndiKeySourceName) << '\t'
      << escapeField(outputTarget.outputType) << '\t'
      << outputTarget.mirrorSourceOutputIndex << '\t'
      << escapeField(outputTarget.outputId) << '\t'
      << outputTarget.outputAlpha << '\t'
      << outputTarget.outputDelayMs << '\t'
      << (outputTarget.outputTimeOverlayEnabled ? 1 : 0) << '\t'
      << escapeField(outputTarget.outputColorSpace) << '\t'
      << escapeField(outputTarget.outputLayoutMode) << '\t'
      << outputTarget.outputOrientationDegrees << '\t'
      << (outputTarget.outputTestCardEnabled ? 1 : 0) << '\t'
      << (outputTarget.deckLinkEnabled ? 1 : 0) << '\t'
      << outputTarget.deckLinkDeviceId << '\t'
      << escapeField(outputTarget.deckLinkMode) << '\t'
      << (outputTarget.deckLink10Bit ? 1 : 0)
      << '\t' << outputTarget.aoiLeft
      << '\t' << outputTarget.aoiRight
      << '\t' << outputTarget.aoiTop
      << '\t' << outputTarget.aoiBottom
      << '\t' << (outputTarget.spoutEnabled ? 1 : 0)
      << '\t' << escapeField(outputTarget.spoutSenderName)
      << '\t' << escapeField(outputTarget.streamKey)
      << '\t' << escapeField(outputTarget.displayName)
      // ST 2110-20 (fields 36-40) — appended, guarded on load as always
      << '\t' << (outputTarget.st2110Enabled ? 1 : 0)
      << '\t' << escapeField(outputTarget.st2110Address)
      << '\t' << escapeField(outputTarget.st2110Interface)
      << '\t' << outputTarget.st2110Port
      << '\t' << (outputTarget.st2110TenBit ? 1 : 0)
      // SRT transport + encoder (fields 41-45)
      << '\t' << outputTarget.srtLatencyMs
      << '\t' << escapeField(outputTarget.srtPassphrase)
      << '\t' << escapeField(outputTarget.srtStreamId)
      << '\t' << escapeField(outputTarget.srtMode)
      << '\t' << outputTarget.streamKeyframeSeconds
      << '\t' << outputTarget.streamAudioBitrateKbps
      << '\n';
  }
  for (size_t deckIndex = 0; deckIndex < project.decks.size(); ++deckIndex) {
    const auto& deck = project.decks[deckIndex];
    output
      << "deck\t"
      << deckIndex << '\t'
      << escapeField(deck.name) << '\t'
      << deck.selectedIndex << '\t'
      << deck.activeIndex << '\t'
      << 0 << '\t' // legacy auto-advance placeholder: cue endings are now per-cue
      << (deck.playlistLoop ? 1 : 0) << '\t'
      << escapeField(deck.audioOutputDeviceName) << '\t'
      << deck.outputDisplayIndex << '\t'
      << (deck.ndiEnabled ? 1 : 0) << '\t'
      << escapeField(deck.ndiSourceName) << '\t'
      << (deck.timeOverlayEnabled ? 1 : 0) << '\t'
      << deck.transitionSeconds << '\t'
      << escapeField(deck.transitionStyle) << '\t'
      << (deck.timecodeChaseEnabled ? 1 : 0) << '\t'
      << (deck.timecodeRunEnabled ? 1 : 0) << '\t'
      << (deck.timecodeTriggerEnabled ? 1 : 0) << '\t'
      << deck.timecodeFps << '\t'
      << deck.timecodeCurrentSeconds << '\t'
      << (deck.shuffle ? 1 : 0) << '\t'
      << (deck.ndiKeyEnabled ? 1 : 0) << '\t'
      << escapeField(deck.ndiKeySourceName) << '\t'
      << deck.canvasViewX << '\t'
      << deck.canvasViewY << '\t'
      << (deck.warpEnabled ? 1 : 0) << '\t'
      << escapeField(deck.warpMode) << '\t'
      << deck.warpTopLeftX << '\t'
      << deck.warpTopLeftY << '\t'
      << deck.warpTopRightX << '\t'
      << deck.warpTopRightY << '\t'
      << deck.warpBottomRightX << '\t'
      << deck.warpBottomRightY << '\t'
      << deck.warpBottomLeftX << '\t'
      << deck.warpBottomLeftY << '\t'
      << deck.edgeBlendLeft << '\t'
      << deck.edgeBlendRight << '\t'
      << deck.edgeBlendTop << '\t'
      << deck.edgeBlendBottom << '\t'
      << deck.outputRouteDeckIndex << '\t'
      << 0 << '\t'
      << deck.timecodeFreewheelSeconds << '\t'
      << (deck.timecodeJamSyncEnabled ? 1 : 0) << '\t'
      << deck.playlistOpacity << '\t'
      << (deck.playlistAutoFade ? 1 : 0) << '\t'
      << deck.playlistFadeSeconds << '\t'
      << deck.playlistTimebaseFps << '\t'
      << deck.playlistStartOffsetSeconds << '\t'
      << deck.playlistDefaultCueFadeSeconds << '\t'
      << deck.playlistDefaultStillDurationSeconds << '\t'
      << (deck.playlistDefaultLoop ? 1 : 0) << '\t'
      << (deck.playlistDefaultFadeInEnabled ? 1 : 0) << '\t'
      << (deck.playlistDefaultFadeOutEnabled ? 1 : 0) << '\t'
      << (deck.playlistDefaultAudioEnabled ? 1 : 0) << '\t'
      << (deck.playlistDefaultPauseAtBeginning ? 1 : 0) << '\t'
      << (deck.playlistDefaultPauseAtEnd ? 1 : 0) << '\t'
      << (deck.playlistDefaultTransitionToNext ? 1 : 0) << '\t'
      << deck.audioOutputChannels
      << '\n';

    for (const auto& cue : deck.cues) {
      output
        << "cue\t"
        << deckIndex << '\t'
        << escapeField(cue.path) << '\t'
        << escapeField(cue.name) << '\t'
        << cueKindToken(cue.kind) << '\t'
        << cue.duration << '\t'
        << cue.width << '\t'
        << cue.height << '\t'
        << cue.fps << '\t'
        << escapeField(cue.formatName) << '\t'
        << escapeField(cue.videoCodec) << '\t'
        << escapeField(cue.audioCodec) << '\t'
        << (cue.hasAudio ? "1" : "0") << '\t'
        << cue.sizeBytes << '\t'
        << colorToHex(cue.color) << '\t'
        << cue.fadeInSeconds << '\t'
        << cue.fadeOutSeconds << '\t'
        << (cue.loop ? "1" : "0") << '\t'
        << (cue.pauseOnLastFrame ? "1" : "0") << '\t'
        << escapeField(cue.id) << '\t'
        << cue.inPointSeconds << '\t'
        << cue.outPointSeconds << '\t'
        << cue.triggerTimecodeSeconds << '\t'
        << cueEndActionToken(cue.endAction) << '\t'
        << cue.cueTransitionSeconds << '\t'
        << escapeField(cue.cueTransitionStyle) << '\t'
        << escapeField(cue.lowerThirdText) << '\t'
        << escapeField(cue.lowerThirdSubtext) << '\t'
        << cue.lowerThirdBgAlpha << '\t'
        << cue.stillDurationSeconds << '\t'
        << cue.loopCount << '\t'
        << cue.playbackSpeed << '\t'
        << escapeField(cue.colorTag)
        << '\t' << escapeField(cue.notes)
        << '\t' << cue.outputScaleX
        << '\t' << cue.outputScaleY
        << '\t' << static_cast<int>(cue.scaleMode)
        << '\t' << cue.outputOffsetX
        << '\t' << cue.outputOffsetY
        << '\t' << escapeField(cue.cueNumber)
        << '\t' << [&]() {
             std::ostringstream pp;
             for (size_t i = 0; i < cue.pausePoints.size(); ++i) {
               if (i) pp << ',';
               pp << cue.pausePoints[i];
             }
             return pp.str();
           }()
        << '\t' << cue.outputRotationDegrees
        << '\t' << cue.cropLeft
        << '\t' << cue.cropRight
        << '\t' << cue.cropTop
        << '\t' << cue.cropBottom
        << '\t' << (cue.chromaKeyEnabled ? "1" : "0")
        << '\t' << colorToHex(cue.chromaKeyColor)
        << '\t' << cue.chromaKeyTolerance
        << '\t' << cue.chromaKeySoftness
        << '\t' << cue.brightness
        << '\t' << cue.contrast
        << '\t' << cue.saturation
        << '\t' << cue.hueShift
        << '\t' << escapeField(cue.cueId)
        << '\t' << (cue.audioEnabled ? "1" : "0")
        << '\t' << (cue.pauseAtBeginning ? "1" : "0")
        << '\t' << (cue.transitionToNext ? "1" : "0")
        << '\t' << escapeField(cue.gotoTarget)
        << '\t' << cue.audioChannels
        << '\t' << cue.audioSampleRate
        << '\t' << escapeField(cue.pipTargetCue)
        << '\t' << escapeField(cue.pipSourceType)
        << '\t' << escapeField(cue.attachedLowerThirdCue)
        << '\t' << escapeField(cue.attachedPipCue)
        << '\t' << escapeField(cue.compositeLayoutPreset)
        << '\t' << escapeField(cue.compositeAudioSlotId)
        << '\t' << colorToHex(cue.compositeBackgroundColor)
        << '\t' << cue.compositeSlots.size();
      for (const CompositeSlot& slot : cue.compositeSlots) {
        output
          << '\t' << escapeField(slot.id)
          << '\t' << escapeField(slot.name)
          << '\t' << escapeField(slot.sourceType)
          << '\t' << escapeField(slot.source)
          << '\t' << (slot.visible ? "1" : "0")
          << '\t' << (slot.audioEnabled ? "1" : "0")
          << '\t' << static_cast<int>(slot.scaleMode)
          << '\t' << slot.normX
          << '\t' << slot.normY
          << '\t' << slot.normW
          << '\t' << slot.normH;
      }
      // Subtitle fields (appended after composite slots for backward compat)
      output
        << '\t' << escapeField(cue.subtitlePath)
        << '\t' << escapeField(cue.subtitleStreamId)
        << '\t' << (cue.subtitleEnabled ? "1" : "0")
        << '\t' << (cue.refreshOnTake ? "1" : "0")
        << '\t' << cue.audioGainDb
        << '\t' << cue.audioPan
        << '\t' << (cue.audioMono ? "1" : "0")
        << '\t' << cue.audioFadeInSeconds
        << '\t' << cue.audioFadeOutSeconds
        << '\t' << cue.audioOutputPair
        << '\t' << (cue.datamoshEnabled ? "1" : "0")
        << '\t' << escapeField(cue.moshPath)
        << '\t' << cue.timer.durationSeconds
        << '\t' << cue.timer.amberSeconds
        << '\t' << cue.timer.redSeconds
        << '\t' << (cue.timer.countUpAfterZero ? "1" : "0")
        << '\t' << (cue.timer.blinkAtZero ? "1" : "0")
        << '\t' << escapeField(cue.timer.message)
        << '\t' << static_cast<int>(cue.timer.mode)
        << '\t' << static_cast<int>(cue.timer.face)
        << '\t' << (cue.timer.showProgressBar ? "1" : "0")
        << '\t' << (cue.timer.messageIsUrgent ? "1" : "0")
        << '\t' << cue.scheduledStartSeconds
        << '\t' << joinMarkerTimes(cue)
        << '\t' << escapeField(joinMarkerNames(cue))
        << '\t' << cue.datamoshLook
        << '\t' << cue.timer.colorNormal
        << '\t' << cue.timer.colorAmber
        << '\t' << cue.timer.colorRed
        << '\t' << cue.timer.colorBackground
        << '\t' << (cue.timer.chimeAtAmber ? "1" : "0")
        << '\t' << (cue.timer.chimeAtRed ? "1" : "0")
        << '\t' << (cue.timer.chimeAtZero ? "1" : "0")
        << '\t' << cue.timer.chimeSound
        << '\t' << static_cast<int>(cue.tone.waveform)
        << '\t' << cue.tone.frequencyHz
        << '\t' << cue.tone.levelDbfs
        << '\t' << cue.tone.channel
        << '\t' << static_cast<int>(cue.tone.visual)
        << '\t' << (cue.tone.visualEnabled ? "1" : "0")
        << '\t' << static_cast<int>(cue.tone.synth.chip)
        << '\t' << cue.tone.synth.noteHz
        << '\t' << cue.tone.synth.attackSeconds
        << '\t' << cue.tone.synth.releaseSeconds
        << '\t' << cue.tone.synth.retriggerSeconds
        << '\t' << static_cast<int>(cue.tone.synth.carrier)
        << '\t' << static_cast<int>(cue.tone.synth.modulator)
        << '\t' << cue.tone.synth.modDepth
        << '\t' << cue.tone.synth.modRatio
        << '\t' << static_cast<int>(cue.tone.synth.nesVoice)
        << '\t' << static_cast<int>(cue.tone.synth.nesDuty)
        << '\t' << (cue.tone.synth.nesNoiseShort ? "1" : "0")
        << '\t' << (cue.tone.synth.nesQuantise ? "1" : "0")
        << '\t' << static_cast<int>(cue.tone.synth.tuning)
        << '\t' << cue.tone.synth.referenceHz
        << '\t' << static_cast<int>(cue.videoSynth.shape)
        << '\t' << static_cast<int>(cue.videoSynth.mirror)
        << '\t' << static_cast<int>(cue.videoSynth.palette)
        << '\t' << cue.videoSynth.speed
        << '\t' << cue.videoSynth.scale
        << '\t' << cue.videoSynth.warp
        << '\t' << cue.videoSynth.feedbackAmount
        << '\t' << cue.videoSynth.feedbackZoom
        << '\t' << cue.videoSynth.feedbackRotate
        << '\t' << cue.videoSynth.audioReactivity
        << '\t' << cue.videoSynth.resolution
        << '\t' << cue.videoSynth.pixelSort
        << '\t' << cue.videoSynth.glitch
        << '\t' << (cue.videoSynth.ascii ? "1" : "0")
        << '\t' << cue.videoSynth.asciiCols
        << '\t' << (cue.videoSynth.asciiGreen ? "1" : "0")
        << '\t' << cue.videoSynth.crt
        << '\t' << cue.videoSynth.asciiCharSet
        << '\t' << cue.videoSynth.asciiShuffle
        << '\t' << cue.videoSynth.asciiInk
        << '\t' << escapeField(cue.videoSynth.spriteSheetPath)
        << '\t' << cue.videoSynth.spriteTileW
        << '\t' << cue.videoSynth.spriteTileH
        << '\t' << cue.videoSynth.spriteRotate
        << '\t' << cue.videoSynth.spriteFreeAngle
        << '\t' << cue.videoSynth.spriteFlip
        << '\t' << cue.videoSynth.spriteJitter
        << '\t' << cue.videoSynth.spriteChaos
        << '\t' << escapeField(cue.timer.logoPath)
        << '\t' << cue.timer.logoHeightPercent
        // Effect stack, appended at the END like every other addition.
        // Packed into ONE field as "token:amount:a:b|..." so a stack of any
        // length costs a single column -- a variable number of columns would
        // shift every positional index after it, which is the trap the loader
        // offsets already carry scars from.
        << '\t' << escapeField(serializeCueEffects(cue.effects))
        << '\t' << escapeField(cue.motionDriverPath)
        << '\t' << cue.motionDriverSpeed
        << '\t' << (cue.motionDriverPaused ? 1 : 0)
        << '\t' << (cue.motionDriverRestartOnTake ? 1 : 0)
        << '\t' << escapeField(cue.codeExpression)
        // Appended after the expression, so every show saved before text mode
        // could carry words still loads and simply has none.
        << '\t' << escapeField(cue.videoSynth.asciiGlyphs)
        << '\t' << escapeField(cue.videoSynth.asciiPhrases)
        << '\t' << cue.videoSynth.asciiPhraseHold
        << '\t' << cue.videoSynth.asciiZalgoUp
        << '\t' << cue.videoSynth.asciiZalgoDown
        << '\t' << cue.videoSynth.asciiZalgoMid
        << '\t' << cue.videoSynth.asciiZalgoReach
        << '\t' << cue.videoSynth.asciiZalgoDrift
        // APPENDED, never inserted: the record is positional.
        << '\t' << cue.videoSynth.asciiChaos
        << '\n';
    }
  }

  // Everything above is buffered. Close BEFORE the rename and check the
  // stream: a disk that filled up reports it here, and the operator's
  // existing show has to survive that rather than be replaced by a short
  // file.
  output.close();
  if (!output) {
    std::error_code ignored;
    fs::remove(temp, ignored);
    return false;
  }
  std::error_code renameError;
  fs::rename(temp, resolved, renameError);
  if (renameError) {
    std::error_code ignored;
    fs::remove(temp, ignored);
    return false;
  }
  return true;
}

// onProgress (optional) receives 0..1 as the file is consumed, so a caller can
// draw a loading overlay while this blocks. Progress is measured in BYTES READ
// rather than lines, because a show's line count isn't known until it has been
// read — and byte position is exact and free.
// The project-level scalars: one key per line, `key<TAB>value`.
//
// LIFTED OUT OF loadProject because MSVC counts every `else if` as a nested
// block and refuses past a limit -- C1061, the same wall handleSettingsClick
// hit and was split for. There were a hundred and twelve of these and adding
// one more broke the build, which is a poor reason for a show file to stop
// gaining settings.
//
// Returns true when the line was a scalar it knows, so the caller can tell a
// key it does not recognise from one it simply has nothing to do for.
template <typename EnsureDeck>
bool applyProjectScalarLine(Project& project, const std::vector<std::string>& fields,
                            EnsureDeck&& ensureDeck) {
  if (fields[0] == "title") {
    project.title = safeString(fields, 1);
  } else if (fields[0] == "focused_deck") {
    project.focusedDeckIndex = safeInt(fields, 1, 0);
  } else if (fields[0] == "focused_output") {
    project.focusedOutputIndex = safeInt(fields, 1, 0);
  } else if (fields[0] == "focused_group" || fields[0] == "layer_names") {
    // Legacy fields — ignored (single-deck, no layer assignments or group presets).
  } else if (fields[0] == "advanced_mode") {
    project.advancedOutputMode = safeBool(fields, 1, false);
  } else if (fields[0] == "ptp_domain") {
    project.ptpDomain = std::clamp(safeInt(fields, 1, 127), 0, 127);
  } else if (fields[0] == "nmos_enabled") {
    project.nmosEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "nmos_registry") {
    project.nmosRegistryUrl = safeString(fields, 1);
  } else if (fields[0] == "nmos_port") {
    project.nmosPort = std::clamp(safeInt(fields, 1, 3210), 1, 65535);
  } else if (fields[0] == "nmos_interface") {
    project.nmosInterfaceName = safeString(fields, 1);
  } else if (fields[0] == "ltc_out") {
    project.ltcOutputEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "ltc_out_device") {
    project.ltcOutputDeviceName = safeString(fields, 1);
  } else if (fields[0] == "ltc_out_fps") {
    project.ltcOutputFps = std::clamp(safeDouble(fields, 1, 30.0), 23.0, 60.0);
  } else if (fields[0] == "ltc_out_channel") {
    project.ltcOutputChannel = std::clamp(safeInt(fields, 1, 0), 0, 7);
  } else if (fields[0] == "ltc_out_channels") {
    project.ltcOutputChannelCount = std::clamp(safeInt(fields, 1, 2), 1, 8);
  } else if (fields[0] == "selected") {
    ensureDeck(0).selectedIndex = safeInt(fields, 1, -1);
  } else if (fields[0] == "active") {
    ensureDeck(0).activeIndex = safeInt(fields, 1, -1);
  } else if (fields[0] == "auto_advance") {
    // Legacy field: cue endings now follow per-cue hold/end settings.
  } else if (fields[0] == "playlist_loop") {
    ensureDeck(0).playlistLoop = safeBool(fields, 1, false);
  } else if (fields[0] == "ui_sounds") {
    project.uiSoundsEnabled = safeBool(fields, 1, true);
  } else if (fields[0] == "ui_transitions") {
    project.uiTransitionsEnabled = safeBool(fields, 1, true);
  } else if (fields[0] == "hap_suggestion_dismissed") {
    project.hapSuggestionDismissed = safeBool(fields, 1, false);
  } else if (fields[0] == "synth_keyboard") {
    project.synthKeyboardEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "synth_octave") {
    project.synthKeyboardOctave = std::clamp(safeInt(fields, 1, 4), 0, 8);
  } else if (fields[0] == "midi_to_synth") {
    project.midiToSynth = safeBool(fields, 1, false);
  } else if (fields[0] == "audio_input_device") {
    project.audioInputDeviceName = safeString(fields, 1);
  } else if (fields[0] == "audio_input_enabled") {
    project.audioInputEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "audio_input_mono") {
    project.audioInputMono = safeBool(fields, 1, true);
  } else if (fields[0] == "audio_input_to_program") {
    project.audioInputToProgram = safeBool(fields, 1, true);
  } else if (fields[0] == "audio_input_gain_db") {
    project.audioInputGainDb = std::clamp(safeDouble(fields, 1, 0.0), -40.0, 40.0);
  } else if (fields[0] == "asio_driver") {
    project.asioDriverName = safeString(fields, 1);
  } else if (fields[0] == "asio_channels") {
    project.asioChannels = std::clamp(safeInt(fields, 1, 2), 2, 64);
  } else if (fields[0] == "recording_dir") {
    project.recordingDir = safeString(fields, 1);
  } else if (fields[0] == "recording_width") {
    project.recordingWidth = safeInt(fields, 1, 0);
  } else if (fields[0] == "recording_height") {
    project.recordingHeight = safeInt(fields, 1, 0);
  } else if (fields[0] == "recording_fps") {
    project.recordingFps = safeDouble(fields, 1, 0.0);
  } else if (fields[0] == "recording_codec") {
    project.recordingCodec = safeString(fields, 1);
  } else if (fields[0] == "recording_tc_mode") {
    project.recordingTimecodeMode = safeString(fields, 1);
  } else if (fields[0] == "recording_tc_start") {
    project.recordingTimecodeStart = safeString(fields, 1);
  } else if (fields[0] == "recording_tc_df") {
    project.recordingTimecodeDropFrame = safeString(fields, 1);
  } else if (fields[0] == "recording_segment_minutes") {
    project.recordingSegmentMinutes = safeInt(fields, 1, 0);
  } else if (fields[0] == "recording_segment_mb") {
    project.recordingSegmentMegabytes = safeInt(fields, 1, 0);
  } else if (fields[0] == "recording_remux") {
    project.recordingRemuxOnStop = safeBool(fields, 1, true);
  } else if (fields[0] == "splash_character") {
    std::string v = safeString(fields, 1);
    project.splashCharacter = v.empty() ? std::string("deckbot") : v;
  } else if (fields[0] == "update_check") {
    project.updateCheckEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "midi_device") {
    project.midiDeviceName = safeString(fields, 1);
  } else if (fields[0] == "theme") {
    project.theme = safeString(fields, 1);
  } else if (fields[0] == "terrarium_unlocked") {
    project.terrariumUnlocked = safeBool(fields, 1, false);
  } else if (fields[0] == "geometry_aspect_link") {
    project.geometryAspectLinked = safeBool(fields, 1, true);
  } else if (fields[0] == "vj_mode") {
    project.vjModeEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "vj_deck_a") {
    project.vjDeckA = std::max(0, safeInt(fields, 1, 0));
  } else if (fields[0] == "vj_deck_b") {
    project.vjDeckB = std::max(0, safeInt(fields, 1, 1));
  } else if (fields[0] == "vj_mix") {
    project.vjMixPosition = std::clamp(safeDouble(fields, 1, 0.0), 0.0, 1.0);
  } else if (fields[0] == "vj_blend") {
    const std::string mode = safeString(fields, 1);
    project.vjBlendMode = (mode == "add" || mode == "multiply") ? mode : "dissolve";
  } else if (fields[0] == "vj_bpm") {
    project.vjTempoBpm = std::clamp(safeDouble(fields, 1, 120.0), 20.0, 300.0);
  } else if (fields[0] == "vj_quantise") {
    project.vjQuantiseTakes = safeBool(fields, 1, false);
  } else if (fields[0] == "creatures") {
    project.creaturesEnabled = safeBool(fields, 1, true);
  } else if (fields[0] == "creatures_while_live") {
    project.creaturesWhileLive = safeBool(fields, 1, false);
  } else if (fields[0] == "show_control_device") {
    project.showControlDeviceId = std::clamp(safeInt(fields, 1, 0), 0, 127);
  } else if (fields[0] == "ui_scale") {
    project.uiScale = std::clamp(safeDouble(fields, 1, 1.0), 0.75, 3.0);
  } else if (fields[0] == "interaction_mode") {
    std::string v = safeString(fields, 1);
    project.interactionMode = (v == "touch") ? "touch" : "mouse";
  } else if (fields[0] == "allow_remote_network") {
    project.allowRemoteNetwork = safeBool(fields, 1, false);
  } else if (fields[0] == "osc_query_enabled") {
    project.oscQueryEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "osc_query_port") {
    project.oscQueryPort = safeInt(fields, 1, 5511);
  } else if (fields[0] == "osc_feedback_mirror") {
    project.oscFeedbackMirrorEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "osc_feedback_rate_ms") {
    project.oscFeedbackRateMs = safeInt(fields, 1, 120);
  } else if (fields[0] == "integration_atem_trigger") {
    project.atemTriggerEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "integration_ndi_trigger") {
    project.ndiTriggerEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "integration_nmc_sync") {
    project.nmcSyncEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "integration_mtc_ingest") {
    project.mtcIngestEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "integration_ltc_ingest") {
    project.ltcIngestEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "integration_dmx_artnet") {
    project.dmxArtNetEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "integration_artnet_port") {
    project.artNetPort = safeInt(fields, 1, 6454);
  } else if (fields[0] == "integration_tsl_tally") {
    project.tslTallyEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "integration_tsl_port") {
    project.tslTallyPort = std::clamp(safeInt(fields, 1, 5800), 1, 65535);
  } else if (fields[0] == "integration_tsl_address") {
    { std::string v = safeString(fields, 1); project.tslTallyAddress = v.empty() ? "255.255.255.255" : v; }
  } else if (fields[0] == "audio_buffer_samples") {
    int v = safeInt(fields, 1, 1024);
    // Snap to valid sizes only
    if (v <= 256) v = 256; else if (v <= 512) v = 512; else if (v <= 1024) v = 1024; else v = 2048;
    project.audioBufferSamples = v;
  } else if (fields[0] == "audio_delay_ms") {
    project.audioDelayMs = std::clamp(safeInt(fields, 1, 0), 0, 1000);
  } else if (fields[0] == "jump_mode") {
    project.jumpMode = normalizeJumpModeToken(safeString(fields, 1));
  } else if (fields[0] == "jump_transition") {
    project.jumpTransitionEnabled = safeBool(fields, 1, true);
  } else if (fields[0] == "panic_profile") {
    project.panicProfile = normalizePanicProfileToken(safeString(fields, 1));
  } else if (fields[0] == "panic_fade_seconds") {
    project.panicFadeSeconds = safeDouble(fields, 1, 0.9);
  } else if (fields[0] == "panic_auto_restore") {
    project.panicAutoRestore = safeBool(fields, 1, false);
  } else if (fields[0] == "master_volume") {
    // Range is 0..2 (values above 1.0 are boost) — the old 0..1 clamp here
    // silently flattened saved boost levels on load.
    project.masterVolume = std::clamp(safeDouble(fields, 1, 1.0), 0.0, 2.0);
  } else if (fields[0] == "master_dimmer") {
    project.masterDimmer = std::clamp(safeDouble(fields, 1, 1.0), 0.0, 1.0);
  } else if (fields[0] == "output_follow_display") {
    project.outputFollowDisplay = safeBool(fields, 1, true);
  } else if (fields[0] == "output_render_width") {
    project.outputRenderWidth = safeInt(fields, 1, 1920);
  } else if (fields[0] == "output_render_height") {
    project.outputRenderHeight = safeInt(fields, 1, 1080);
  } else if (fields[0] == "output_refresh_hz") {
    project.outputRefreshRateHz = std::max(0.0, safeDouble(fields, 1, 0.0));
  } else if (fields[0] == "output_bit_depth") {
    project.outputBitDepth = safeInt(fields, 1, 0);
  } else if (fields[0] == "output_canvas_enabled") {
    project.outputCanvasEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "output_canvas_width") {
    project.outputCanvasWidth = safeInt(fields, 1, 3840);
  } else if (fields[0] == "output_canvas_height") {
    project.outputCanvasHeight = safeInt(fields, 1, 2160);
  } else if (fields[0] == "output_target") {
    int outputIndex = safeInt(fields, 1, static_cast<int>(project.outputs.size()));
    int normalizedIndex = std::clamp(outputIndex, 0, kMaxOutputs - 1);
    while (normalizedIndex >= static_cast<int>(project.outputs.size())) {
      project.outputs.push_back(OutputTarget {});
    }
    OutputTarget& outputTarget = project.outputs[normalizedIndex];
    outputTarget.name = safeString(fields, 2);
    outputTarget.hostDeckIndex = safeInt(fields, 3, 0);
    outputTarget.displayIndex = safeInt(fields, 4, 0);
    outputTarget.enabled = safeBool(fields, 5, false);
    outputTarget.streamEnabled = safeBool(fields, 6, false);
    outputTarget.streamProtocol = safeString(fields, 7);
    outputTarget.streamUrl = safeString(fields, 8);
    outputTarget.streamBitrateKbps = safeInt(fields, 9, 6000);
    if (fields.size() >= 17) {
      outputTarget.ndiEnabled = safeBool(fields, 10, false);
      outputTarget.ndiSourceName = safeString(fields, 11);
      outputTarget.ndiKeyEnabled = safeBool(fields, 12, false);
      outputTarget.ndiKeySourceName = safeString(fields, 13);
      outputTarget.outputType = safeString(fields, 14);
      outputTarget.mirrorSourceOutputIndex = safeInt(fields, 15, -1);
      outputTarget.outputId = safeString(fields, 16);
      if (fields.size() >= 21) {
        outputTarget.outputAlpha = static_cast<float>(safeDouble(fields, 17, 1.0));
        outputTarget.outputDelayMs = safeInt(fields, 18, 0);
        outputTarget.outputTimeOverlayEnabled = safeBool(fields, 19, false);
        outputTarget.outputColorSpace = safeString(fields, 20);
        if (fields.size() >= 24) {
          outputTarget.outputLayoutMode = safeString(fields, 21);
          outputTarget.outputOrientationDegrees = safeInt(fields, 22, 0);
          outputTarget.outputTestCardEnabled = safeBool(fields, 23, false);
          if (fields.size() >= 28) {
            outputTarget.deckLinkEnabled = safeBool(fields, 24, false);
            outputTarget.deckLinkDeviceId = safeInt(fields, 25, -1);
            outputTarget.deckLinkMode = safeString(fields, 26);
            outputTarget.deckLink10Bit = safeBool(fields, 27, true);
            if (fields.size() >= 32) {
              outputTarget.aoiLeft   = static_cast<float>(safeDouble(fields, 28, 0.0));
              outputTarget.aoiRight  = static_cast<float>(safeDouble(fields, 29, 0.0));
              outputTarget.aoiTop    = static_cast<float>(safeDouble(fields, 30, 0.0));
              outputTarget.aoiBottom = static_cast<float>(safeDouble(fields, 31, 0.0));
              if (fields.size() >= 34) {
                outputTarget.spoutEnabled = safeBool(fields, 32, false);
                outputTarget.spoutSenderName = safeString(fields, 33);
                if (fields.size() >= 35) {
                  outputTarget.streamKey = safeString(fields, 34);
                  if (fields.size() >= 36) {
                    outputTarget.displayName = safeString(fields, 35);
                    if (fields.size() >= 41) {
                      outputTarget.st2110Enabled = safeBool(fields, 36, false);
                      outputTarget.st2110Address = safeString(fields, 37);
                      outputTarget.st2110Interface = safeString(fields, 38);
                      outputTarget.st2110Port = safeInt(fields, 39, 20000);
                      outputTarget.st2110TenBit = safeBool(fields, 40, true);
                      if (trim(outputTarget.st2110Address).empty()) {
                        outputTarget.st2110Address = "239.20.10.1";
                      }
                      if (fields.size() >= 46) {
                        outputTarget.srtLatencyMs = std::clamp(safeInt(fields, 41, 120), 20, 8000);
                        outputTarget.srtPassphrase = safeString(fields, 42);
                        outputTarget.srtStreamId = safeString(fields, 43);
                        outputTarget.srtMode =
                          (safeString(fields, 44) == "listener") ? "listener" : "caller";
                        outputTarget.streamKeyframeSeconds =
                          std::clamp(safeInt(fields, 45, 2), 1, 10);
                        // Appended after the keyframe field; older
                        // shows take the previous hardcoded 160.
                        outputTarget.streamAudioBitrateKbps =
                          std::clamp(safeInt(fields, 46, 160), 32, 512);
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    } else {
      // Backward compatibility with older 13-column output_target lines.
      outputTarget.outputType = safeString(fields, 10);
      outputTarget.mirrorSourceOutputIndex = safeInt(fields, 11, -1);
      outputTarget.outputId = safeString(fields, 12);
    }
  } else if (fields[0] == "layer_assignment" || fields[0] == "group_preset" ||
             fields[0] == "group_slot" || fields[0] == "group_preset_meta") {
    // Legacy fields — ignored (single-deck, no layer assignments or group presets).
  } else if (fields[0] == "audio_output") {
    ensureDeck(0).audioOutputDeviceName = safeString(fields, 1);
  } else if (fields[0] == "display_index") {
    ensureDeck(0).outputDisplayIndex = safeInt(fields, 1, 0);
  } else if (fields[0] == "ndi_enabled") {
    ensureDeck(0).ndiEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "ndi_name") {
    ensureDeck(0).ndiSourceName = safeString(fields, 1);
  } else if (fields[0] == "ndi_key_enabled") {
    ensureDeck(0).ndiKeyEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "ndi_key_name") {
    ensureDeck(0).ndiKeySourceName = safeString(fields, 1);
  } else if (fields[0] == "time_overlay") {
    ensureDeck(0).timeOverlayEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "transition_seconds") {
    ensureDeck(0).transitionSeconds = std::max(0.0, safeDouble(fields, 1, 0.0));
  } else if (fields[0] == "transition_style") {
    ensureDeck(0).transitionStyle = safeString(fields, 1);
  } else if (fields[0] == "timecode_chase") {
    ensureDeck(0).timecodeChaseEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "timecode_run") {
    ensureDeck(0).timecodeRunEnabled = safeBool(fields, 1, false);
  } else if (fields[0] == "timecode_trigger") {
    ensureDeck(0).timecodeTriggerEnabled = safeBool(fields, 1, true);
  } else if (fields[0] == "timecode_jam") {
    ensureDeck(0).timecodeJamSyncEnabled = safeBool(fields, 1, true);
  } else if (fields[0] == "timecode_freewheel") {
    ensureDeck(0).timecodeFreewheelSeconds = safeDouble(fields, 1, 1.0);
  } else if (fields[0] == "timecode_fps") {
    ensureDeck(0).timecodeFps = safeDouble(fields, 1, 30.0);
  } else if (fields[0] == "timecode_current") {
    ensureDeck(0).timecodeCurrentSeconds = std::max(0.0, safeDouble(fields, 1, 0.0));
  } else {
    return false;
  }
  return true;
}

Project loadProject(const fs::path& projectFile,
                    const std::function<void(double)>& onProgress = {}) {
  Project project;
  fs::path resolved = projectFile.empty() ? Paths::defaultProjectFile() : projectFile;
  std::ifstream input(resolved, std::ios::binary);
  if (!input) {
    return project;
  }
  std::uintmax_t totalBytes = 0;
  {
    std::error_code ec;
    totalBytes = fs::file_size(resolved, ec);
    if (ec) {
      totalBytes = 0;
    }
  }
  // DID THE FILE END WHERE A FILE SHOULD END?
  //
  // Every record the writer emits is terminated, so a complete show ends with a
  // newline. One that does not was cut off mid-record -- a power cut during the
  // old in-place save, a USB stick pulled, a half-finished copy.
  //
  // This is the check that matters, because the OTHER signal is not enough on
  // its own: a truncation landing inside a `cue` line still matches the cue
  // branch and simply reads short fields, so nothing downstream notices. That
  // is precisely the file that got silently rewritten during testing.
  if (totalBytes > 0) {
    std::ifstream tail(resolved, std::ios::binary);
    if (tail) {
      tail.seekg(static_cast<std::streamoff>(totalBytes) - 1);
      char lastByte = 0;
      if (tail.get(lastByte) && lastByte != '\n') {
        project.loadedCleanly = false;
      }
    }
  }

  std::size_t lineCounter = 0;

  project.decks.clear();
  project.decks.push_back(Deck {});
  project.outputs.clear();

  auto ensureDeck = [&](int deckIndex) -> Deck& {
    // Clamped, not grown to fit: see kMaxDecks. A real show never comes near
    // this, and a corrupt one no longer asks the machine for thousands of
    // audio devices.
    int normalizedIndex = std::clamp(deckIndex, 0, kMaxDecks - 1);
    while (normalizedIndex >= static_cast<int>(project.decks.size())) {
      Deck deck;
      deck.name = deckDefaultName(static_cast<int>(project.decks.size()));
      project.decks.push_back(deck);
    }
    return project.decks[normalizedIndex];
  };

  std::string line;
  while (std::getline(input, line)) {
    // Report every 64 lines: often enough to animate, rare enough that tellg()
    // and the callback cost nothing on a small show.
    if (onProgress && totalBytes > 0 && (++lineCounter & 63u) == 0u) {
      const std::streampos pos = input.tellg();
      if (pos >= 0) {
        onProgress(static_cast<double>(pos) / static_cast<double>(totalBytes));
      }
    }
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    auto fields = splitEscapedTabs(line);
    if (fields.empty()) {
      continue;
    }

    if (applyProjectScalarLine(project, fields, ensureDeck)) {
      // handled above
    } else if (fields[0] == "deck") {
      int deckIndex = safeInt(fields, 1, static_cast<int>(project.decks.size()) - 1);
      Deck& deck = ensureDeck(deckIndex);
      deck.name = safeString(fields, 2);
      deck.selectedIndex = safeInt(fields, 3, -1);
      deck.activeIndex = safeInt(fields, 4, -1);
      deck.playlistLoop = safeBool(fields, 6, false);
      deck.audioOutputDeviceName = safeString(fields, 7);
      deck.outputDisplayIndex = safeInt(fields, 8, 0);
      deck.ndiEnabled = safeBool(fields, 9, false);
      deck.ndiSourceName = safeString(fields, 10);
      deck.timeOverlayEnabled = safeBool(fields, 11, false);
      deck.transitionSeconds = std::max(0.0, safeDouble(fields, 12, 0.0));
      deck.transitionStyle = safeString(fields, 13);
      deck.timecodeChaseEnabled = safeBool(fields, 14, false);
      deck.timecodeRunEnabled = safeBool(fields, 15, false);
      deck.timecodeTriggerEnabled = safeBool(fields, 16, true);
      deck.timecodeFps = safeDouble(fields, 17, 30.0);
      deck.timecodeCurrentSeconds = std::max(0.0, safeDouble(fields, 18, 0.0));
      deck.timecodeLastSeconds = deck.timecodeCurrentSeconds;
      deck.shuffle = safeBool(fields, 19, false);
      deck.ndiKeyEnabled = safeBool(fields, 20, false);
      deck.ndiKeySourceName = safeString(fields, 21);
      deck.canvasViewX = safeInt(fields, 22, 0);
      deck.canvasViewY = safeInt(fields, 23, 0);
      deck.warpEnabled = safeBool(fields, 24, false);
      size_t warpFieldOffset = 0;
      if (fields.size() >= 56) {
        deck.warpMode = safeString(fields, 25);
        warpFieldOffset = 1;
      }
      deck.warpTopLeftX = static_cast<float>(safeDouble(fields, 25 + warpFieldOffset, 0.0));
      deck.warpTopLeftY = static_cast<float>(safeDouble(fields, 26 + warpFieldOffset, 0.0));
      deck.warpTopRightX = static_cast<float>(safeDouble(fields, 27 + warpFieldOffset, 0.0));
      deck.warpTopRightY = static_cast<float>(safeDouble(fields, 28 + warpFieldOffset, 0.0));
      deck.warpBottomRightX = static_cast<float>(safeDouble(fields, 29 + warpFieldOffset, 0.0));
      deck.warpBottomRightY = static_cast<float>(safeDouble(fields, 30 + warpFieldOffset, 0.0));
      deck.warpBottomLeftX = static_cast<float>(safeDouble(fields, 31 + warpFieldOffset, 0.0));
      deck.warpBottomLeftY = static_cast<float>(safeDouble(fields, 32 + warpFieldOffset, 0.0));
      deck.edgeBlendLeft = static_cast<float>(safeDouble(fields, 33 + warpFieldOffset, 0.0));
      deck.edgeBlendRight = static_cast<float>(safeDouble(fields, 34 + warpFieldOffset, 0.0));
      deck.edgeBlendTop = static_cast<float>(safeDouble(fields, 35 + warpFieldOffset, 0.0));
      deck.edgeBlendBottom = static_cast<float>(safeDouble(fields, 36 + warpFieldOffset, 0.0));
      deck.outputRouteDeckIndex = safeInt(fields, 37 + warpFieldOffset, deckIndex);
      (void)safeInt(fields, 38 + warpFieldOffset, 0);
      deck.timecodeFreewheelSeconds = safeDouble(fields, 39 + warpFieldOffset, 1.0);
      deck.timecodeJamSyncEnabled = safeBool(fields, 40 + warpFieldOffset, true);
      deck.playlistOpacity = static_cast<float>(safeDouble(fields, 41 + warpFieldOffset, 1.0));
      deck.playlistAutoFade = safeBool(fields, 42 + warpFieldOffset, false);
      deck.playlistFadeSeconds = safeDouble(fields, 43 + warpFieldOffset, 0.8);
      deck.playlistTimebaseFps = safeDouble(fields, 44 + warpFieldOffset, deck.timecodeFps);
      deck.playlistStartOffsetSeconds = safeDouble(fields, 45 + warpFieldOffset, 0.0);
      deck.playlistDefaultCueFadeSeconds = safeDouble(fields, 46 + warpFieldOffset, 1.5);
      deck.playlistDefaultStillDurationSeconds = safeDouble(fields, 47 + warpFieldOffset, 8.0);
      deck.playlistDefaultLoop = safeBool(fields, 48 + warpFieldOffset, false);
      deck.playlistDefaultFadeInEnabled = safeBool(fields, 49 + warpFieldOffset, true);
      deck.playlistDefaultFadeOutEnabled = safeBool(fields, 50 + warpFieldOffset, true);
      deck.playlistDefaultAudioEnabled = safeBool(fields, 51 + warpFieldOffset, true);
      deck.playlistDefaultPauseAtBeginning = safeBool(fields, 52 + warpFieldOffset, false);
      deck.playlistDefaultPauseAtEnd = safeBool(fields, 53 + warpFieldOffset, true);
      deck.playlistDefaultTransitionToNext = safeBool(fields, 54 + warpFieldOffset, true);
      deck.audioOutputChannels = std::clamp(safeInt(fields, 55 + warpFieldOffset, 2), 2, 8);
    } else if (fields[0] == "cue") {
      int deckIndex = 0;
      size_t offset = 1;
      if (fields.size() >= 19) {
        try {
          deckIndex = std::stoi(fields[1]);
          offset = 2;
        } catch (...) {
          deckIndex = 0;
          offset = 1;
        }
      }

      Cue cue;
      cue.path = safeString(fields, offset + 0);
      cue.name = safeString(fields, offset + 1);
      std::string kind = safeString(fields, offset + 2);
      cue.kind =
        kind == "image" ? CueKind::Image :
        kind == "pattern" ? CueKind::Pattern :
        kind == "browser" ? CueKind::Browser :
        (kind == "window_source" || kind == "window") ? CueKind::WindowSource :
        kind == "camera" ? CueKind::Camera :
        (kind == "syphon" || kind == "spout") ? CueKind::Syphon :
        kind == "srt_stream" ? CueKind::SrtStream :
        kind == "ndi_source" ? CueKind::NdiSource :
        kind == "decklink_source" ? CueKind::DeckLinkSource :
        kind == "pip" ? CueKind::Pip :
        kind == "lower_third" ? CueKind::LowerThird :
        kind == "composite" ? CueKind::Composite :
        kind == "audio" ? CueKind::Audio :
        // Timer, Tone and VideoSynth were missing here as well as in
        // cueKindToken, so even a correctly-written show came back with all
        // three demoted to Video. Keep this list in step with that function --
        // a kind that only one side knows about does not survive a round trip.
        kind == "timer" ? CueKind::Timer :
        kind == "tone" ? CueKind::Tone :
        (kind == "video_synth" || kind == "vsynth") ? CueKind::VideoSynth :
        CueKind::Video;
      // Repair shows written while the round trip was broken. cueKindToken was
      // missing SEVEN kinds, so each was saved as "video" while keeping its
      // real path -- and a Video cue pointed at timer:// can never play, it
      // just racks. v0.84.0 shipped this way, which means every Timer, Pip,
      // Composite, Camera, Window and Syphon cue in a show saved by it comes
      // back dead. The path is the surviving evidence of what the cue was, and
      // every affected kind has its own scheme, so all of them recover.
      if (cue.kind == CueKind::Video) {
        const std::string& p = cue.path;
        if (p.rfind("tone://", 0) == 0)               cue.kind = CueKind::Tone;
        else if (p.rfind("timer://", 0) == 0)         cue.kind = CueKind::Timer;
        else if (p.rfind("vsynth://", 0) == 0)        cue.kind = CueKind::VideoSynth;
        else if (p.rfind("graphic://pip", 0) == 0)    cue.kind = CueKind::Pip;
        else if (p.rfind("graphic://composite", 0) == 0) cue.kind = CueKind::Composite;
        else if (p.rfind("source://camera/", 0) == 0) cue.kind = CueKind::Camera;
        else if (p.rfind("source://window/", 0) == 0) cue.kind = CueKind::WindowSource;
        else if (p.rfind("source://syphon/", 0) == 0) cue.kind = CueKind::Syphon;
      }
      cue.duration = safeDouble(fields, offset + 3, 0.0);
      cue.width = safeInt(fields, offset + 4, 0);
      cue.height = safeInt(fields, offset + 5, 0);
      cue.fps = safeDouble(fields, offset + 6, cue.kind == CueKind::Video ? 30.0 : 0.0);
      cue.formatName = safeString(fields, offset + 7);
      cue.videoCodec = safeString(fields, offset + 8);
      cue.audioCodec = safeString(fields, offset + 9);
      cue.hasAudio = safeBool(fields, offset + 10, false);
      cue.sizeBytes = safeSize(fields, offset + 11, 0);
      cue.color = parseColor(safeString(fields, offset + 12));
      cue.fadeInSeconds = std::max(0.0, safeDouble(fields, offset + 13, 0.0));
      cue.fadeOutSeconds = std::max(0.0, safeDouble(fields, offset + 14, 0.0));
      cue.loop = safeBool(fields, offset + 15, false);
      cue.pauseOnLastFrame = safeBool(fields, offset + 16, false);
      cue.id = safeString(fields, offset + 17);
      cue.inPointSeconds = std::max(0.0, safeDouble(fields, offset + 18, 0.0));
      cue.outPointSeconds = std::max(0.0, safeDouble(fields, offset + 19, 0.0));
      cue.triggerTimecodeSeconds = safeDouble(fields, offset + 20, -1.0);
      cue.endAction = parseCueEndAction(safeString(fields, offset + 21));
      cue.cueTransitionSeconds = safeDouble(fields, offset + 22, -1.0);
      cue.cueTransitionStyle = safeString(fields, offset + 23);
      cue.lowerThirdText = safeString(fields, offset + 24);
      cue.lowerThirdSubtext = safeString(fields, offset + 25);
      cue.lowerThirdBgAlpha = safeInt(fields, offset + 26, 180);
      cue.stillDurationSeconds = std::max(0.0, safeDouble(fields, offset + 27, 0.0));
      cue.loopCount = safeInt(fields, offset + 28, 0);
      cue.playbackSpeed = std::clamp(safeDouble(fields, offset + 29, 1.0), 0.25, 4.0);
      cue.colorTag = safeString(fields, offset + 30);
      cue.notes = safeString(fields, offset + 31);
      cue.outputScaleX = static_cast<float>(std::clamp(safeDouble(fields, offset + 32, 1.0), 0.25, 4.0));
      cue.outputScaleY = static_cast<float>(std::clamp(safeDouble(fields, offset + 33, 1.0), 0.25, 4.0));
      cue.scaleMode = static_cast<ScaleMode>(safeInt(fields, offset + 34, 0));
      cue.outputOffsetX = static_cast<float>(safeDouble(fields, offset + 35, 0.0));
      cue.outputOffsetY = static_cast<float>(safeDouble(fields, offset + 36, 0.0));
      cue.cueNumber = safeString(fields, offset + 37);
      {
        std::string ppStr = safeString(fields, offset + 38);
        if (!ppStr.empty()) {
          std::istringstream ss(ppStr);
          std::string tok;
          while (std::getline(ss, tok, ',')) {
            try { cue.pausePoints.push_back(std::stod(tok)); } catch (...) {}
          }
          std::sort(cue.pausePoints.begin(), cue.pausePoints.end());
        }
      }
      cue.outputRotationDegrees = static_cast<float>(safeDouble(fields, offset + 39, 0.0));
      cue.cropLeft = static_cast<float>(safeDouble(fields, offset + 40, 0.0));
      cue.cropRight = static_cast<float>(safeDouble(fields, offset + 41, 0.0));
      cue.cropTop = static_cast<float>(safeDouble(fields, offset + 42, 0.0));
      cue.cropBottom = static_cast<float>(safeDouble(fields, offset + 43, 0.0));
      cue.chromaKeyEnabled = safeBool(fields, offset + 44, false);
      cue.chromaKeyColor = parseColor(safeString(fields, offset + 45));
      cue.chromaKeyTolerance = static_cast<float>(safeDouble(fields, offset + 46, 60.0));
      cue.chromaKeySoftness = static_cast<float>(safeDouble(fields, offset + 47, 20.0));
      cue.brightness = std::clamp(static_cast<float>(safeDouble(fields, offset + 48, 1.0)), 0.0f, 2.0f);
      cue.contrast = std::clamp(static_cast<float>(safeDouble(fields, offset + 49, 1.0)), 0.0f, 2.0f);
      cue.saturation = std::clamp(static_cast<float>(safeDouble(fields, offset + 50, 1.0)), 0.0f, 2.0f);
      cue.hueShift = std::clamp(static_cast<float>(safeDouble(fields, offset + 51, 0.0)), -180.0f, 180.0f);
      cue.cueId = normalizeCueIdShort(safeString(fields, offset + 52));
      cue.audioEnabled = safeBool(fields, offset + 53, true);
      cue.pauseAtBeginning = safeBool(fields, offset + 54, false);
      cue.transitionToNext = safeBool(fields, offset + 55, true);
      cue.gotoTarget = safeString(fields, offset + 56);
      cue.audioChannels = safeInt(fields, offset + 57, 0);
      cue.audioSampleRate = safeInt(fields, offset + 58, 0);
      cue.pipTargetCue = safeString(fields, offset + 59);
      cue.pipSourceType = safeString(fields, offset + 60);
      cue.attachedLowerThirdCue = safeString(fields, offset + 61);
      cue.attachedPipCue = safeString(fields, offset + 62);
      cue.compositeLayoutPreset = safeString(fields, offset + 63);
      cue.compositeAudioSlotId = safeString(fields, offset + 64);
      cue.compositeBackgroundColor = parseColor(safeString(fields, offset + 65));
      int compositeSlotCount = safeInt(fields, offset + 66, 0);
      size_t compositeBase = offset + 67;
      cue.compositeSlots.clear();
      cue.compositeSlots.reserve(std::max(0, compositeSlotCount));
      for (int slotIndex = 0; slotIndex < compositeSlotCount; ++slotIndex) {
        size_t slotOffset = compositeBase + static_cast<size_t>(slotIndex) * 11;
        if (slotOffset + 10 >= fields.size()) {
          break;
        }
        CompositeSlot slot;
        slot.id = safeString(fields, slotOffset + 0);
        slot.name = safeString(fields, slotOffset + 1);
        slot.sourceType = safeString(fields, slotOffset + 2);
        slot.source = safeString(fields, slotOffset + 3);
        slot.visible = safeBool(fields, slotOffset + 4, true);
        slot.audioEnabled = safeBool(fields, slotOffset + 5, false);
        slot.scaleMode = static_cast<ScaleMode>(safeInt(fields, slotOffset + 6, static_cast<int>(ScaleMode::Fit)));
        slot.normX = static_cast<float>(safeDouble(fields, slotOffset + 7, 0.0));
        slot.normY = static_cast<float>(safeDouble(fields, slotOffset + 8, 0.0));
        slot.normW = static_cast<float>(safeDouble(fields, slotOffset + 9, 0.5));
        slot.normH = static_cast<float>(safeDouble(fields, slotOffset + 10, 0.5));
        ensureCompositeSlotIdentity(slot, slotIndex);
        cue.compositeSlots.push_back(slot);
      }
      if (cue.kind == CueKind::Composite) {
        applyCompositePresetToCue(cue, cue.compositeLayoutPreset);
      }
      // Subtitle fields (after composite slots)
      size_t subtitleBase = compositeBase + static_cast<size_t>(compositeSlotCount) * 11;
      cue.subtitlePath = safeString(fields, subtitleBase + 0);
      cue.subtitleStreamId = safeString(fields, subtitleBase + 1);
      cue.subtitleEnabled = safeBool(fields, subtitleBase + 2, true);
      cue.refreshOnTake = safeBool(fields, subtitleBase + 3, false);
      cue.audioGainDb = std::clamp(static_cast<float>(safeDouble(fields, subtitleBase + 4, 0.0)),
                                   kCueAudioGainMinDb, kCueAudioGainMaxDb);
      cue.audioPan = std::clamp(static_cast<float>(safeDouble(fields, subtitleBase + 5, 0.0)), -1.0f, 1.0f);
      cue.audioMono = safeBool(fields, subtitleBase + 6, false);
      cue.audioFadeInSeconds = std::clamp(static_cast<float>(safeDouble(fields, subtitleBase + 7, -1.0)), -1.0f, 60.0f);
      cue.audioFadeOutSeconds = std::clamp(static_cast<float>(safeDouble(fields, subtitleBase + 8, -1.0)), -1.0f, 60.0f);
      cue.audioOutputPair = std::clamp(safeInt(fields, subtitleBase + 9, 0), 0, 7);
      // Appended after audioOutputPair; older saves simply lack them.
      cue.datamoshEnabled = safeBool(fields, subtitleBase + 10, false);
      cue.moshPath = safeString(fields, subtitleBase + 11);
      // Timer settings, appended after moshPath. Older saves lack them and
      // fall back to the struct defaults, which are a sane 5:00 / 60 / 15.
      {
        const std::size_t tb = subtitleBase + 12;
        cue.timer.durationSeconds = safeInt(fields, tb + 0, cue.timer.durationSeconds);
        cue.timer.amberSeconds    = safeInt(fields, tb + 1, cue.timer.amberSeconds);
        cue.timer.redSeconds      = safeInt(fields, tb + 2, cue.timer.redSeconds);
        cue.timer.countUpAfterZero = safeBool(fields, tb + 3, cue.timer.countUpAfterZero);
        cue.timer.blinkAtZero      = safeBool(fields, tb + 4, cue.timer.blinkAtZero);
        cue.timer.message          = safeString(fields, tb + 5);
        // Appended after message. These four were rendering but NOT persisting,
        // so a saved show lost its timer mode and face on reload.
        cue.timer.mode = static_cast<TimerMode>(std::clamp(safeInt(fields, tb + 6, 0), 0, 2));
        cue.timer.face = static_cast<TimerFace>(std::clamp(safeInt(fields, tb + 7, 0), 0, 1));
        cue.timer.showProgressBar = safeBool(fields, tb + 8, true);
        cue.timer.messageIsUrgent = safeBool(fields, tb + 9, false);
        cue.scheduledStartSeconds = safeDouble(fields, tb + 10, -1.0);
        parseMarkerTimes(cue, safeString(fields, tb + 11));
        parseMarkerNames(cue, safeString(fields, tb + 12));
        // Appended after the markers. Shows saved before the look existed were
        // all prepared with the CLASSIC recipe, which is this field's default,
        // so an old show keeps playing exactly as it did.
        cue.datamoshLook = std::clamp(safeInt(fields, tb + 13, kDatamoshLookClassic),
                                      0, kDatamoshLookCount - 1);
        // Timer colours and chimes, appended last. -1 keeps the built-in
        // colour, so a show saved before these existed looks unchanged.
        cue.timer.colorNormal     = safeInt(fields, tb + 14, -1);
        cue.timer.colorAmber      = safeInt(fields, tb + 15, -1);
        cue.timer.colorRed        = safeInt(fields, tb + 16, -1);
        cue.timer.colorBackground = safeInt(fields, tb + 17, -1);
        cue.timer.chimeAtAmber    = safeBool(fields, tb + 18, false);
        cue.timer.chimeAtRed      = safeBool(fields, tb + 19, false);
        cue.timer.chimeAtZero     = safeBool(fields, tb + 20, true);
        cue.timer.chimeSound      = std::clamp(safeInt(fields, tb + 21, 0), 0, 5);
        // Tone settings. Appended after the timer block; a show saved before
        // tone cues existed simply gets the defaults.
        cue.tone.waveform = static_cast<ToneWaveform>(
          std::clamp(safeInt(fields, tb + 22, 0), 0, 4));
        cue.tone.frequencyHz = std::clamp(safeDouble(fields, tb + 23, 1000.0), 20.0, 20000.0);
        cue.tone.levelDbfs = std::clamp(safeDouble(fields, tb + 24, -18.0), -60.0, -1.0);
        cue.tone.channel = std::clamp(safeInt(fields, tb + 25, -1), -1, 15);
        cue.tone.visual = static_cast<ToneVisual>(
          std::clamp(safeInt(fields, tb + 26, 1), 0, 3));
        cue.tone.visualEnabled = safeBool(fields, tb + 27, true);
        // Chip voice. Appended after the tone block; a show saved before the
        // synth existed takes the defaults. This is the THIRD time a cue field
        // shipped wired to state and effect but not to storage -- it works
        // perfectly until the show is reopened, which is the worst moment to
        // find out. Worth checking deliberately, not eventually.
        cue.tone.synth.chip = static_cast<SynthChip>(
          std::clamp(safeInt(fields, tb + 28, 0), 0, 1));
        cue.tone.synth.noteHz = std::clamp(safeDouble(fields, tb + 29, 220.0), 20.0, 8000.0);
        cue.tone.synth.attackSeconds = std::clamp(safeDouble(fields, tb + 30, 0.01), 0.0, 2.0);
        cue.tone.synth.releaseSeconds = std::clamp(safeDouble(fields, tb + 31, 0.30), 0.01, 4.0);
        cue.tone.synth.retriggerSeconds = std::clamp(safeDouble(fields, tb + 32, 0.0), 0.0, 4.0);
        cue.tone.synth.carrier = static_cast<FdsCarrier>(
          std::clamp(safeInt(fields, tb + 33, 0), 0, 4));
        cue.tone.synth.modulator = static_cast<FdsModulator>(
          std::clamp(safeInt(fields, tb + 34, 1), 0, 4));
        cue.tone.synth.modDepth = std::clamp(safeInt(fields, tb + 35, 16), 0, 63);
        cue.tone.synth.modRatio = std::clamp(safeDouble(fields, tb + 36, 0.5), 0.0, 8.0);
        cue.tone.synth.nesVoice = static_cast<NesVoice>(
          std::clamp(safeInt(fields, tb + 37, 0), 0, 2));
        cue.tone.synth.nesDuty = static_cast<NesDuty>(
          std::clamp(safeInt(fields, tb + 38, 2), 0, 3));
        cue.tone.synth.nesNoiseShort = safeBool(fields, tb + 39, false);
        cue.tone.synth.nesQuantise = safeBool(fields, tb + 40, true);
        // WHERE THE SAVER PUTS THEM. These two are written mid-record, right
        // after the NES flags, but were read from tb+64/65 as though they had
        // been appended after the video-synth block. The result was a
        // two-column skew across every video-synth field from shape to
        // spriteTileH: text mode came back holding the pixel-sort value, the
        // sprite path came back holding the glyph shuffle seed, and a saved
        // reference pitch came back as a sprite tile height.
        //
        // A show written before these fields existed has neither column, so
        // the layout is DETECTED rather than assumed: in the older record the
        // reference-pitch column holds videoSynth.palette, an int 0-4, and a
        // reference pitch is 380-480. Nothing overlaps, so one comparison
        // separates them, and every offset below hangs off the result.
        const bool hasSynthTuning = safeDouble(fields, tb + 42, 0.0) >= 100.0;   // layout-probe
        const std::size_t vs = hasSynthTuning ? tb + 43 : tb + 41;
        if (hasSynthTuning) {
          cue.tone.synth.tuning = static_cast<SynthTuning>(
            std::clamp(safeInt(fields, tb + 41, 0), 0, 6));
          cue.tone.synth.referenceHz =
            std::clamp(safeDouble(fields, tb + 42, 440.0), 380.0, 480.0);
        }
        // Video synth. Written at the same time as the feature rather than
        // discovered missing on reload, which is how the last three went.
        cue.videoSynth.shape = static_cast<VideoSynthShape>(
          std::clamp(safeInt(fields, vs + 0, 0), 0, 4));
        cue.videoSynth.mirror = static_cast<VideoSynthMirror>(
          std::clamp(safeInt(fields, vs + 1, 2), 0, 3));
        cue.videoSynth.palette = static_cast<VideoSynthPalette>(
          std::clamp(safeInt(fields, vs + 2, 0), 0, 4));
        cue.videoSynth.speed = std::clamp(safeDouble(fields, vs + 3, 1.0), 0.05, 8.0);
        cue.videoSynth.scale = std::clamp(safeDouble(fields, vs + 4, 1.0), 0.1, 8.0);
        cue.videoSynth.warp = std::clamp(safeDouble(fields, vs + 5, 0.35), 0.0, 2.0);
        cue.videoSynth.feedbackAmount = std::clamp(safeDouble(fields, vs + 6, 0.55), 0.0, 0.95);
        cue.videoSynth.feedbackZoom = std::clamp(safeDouble(fields, vs + 7, 1.02), 0.90, 1.15);
        cue.videoSynth.feedbackRotate = std::clamp(safeDouble(fields, vs + 8, 0.6), -10.0, 10.0);
        cue.videoSynth.audioReactivity = std::clamp(safeDouble(fields, vs + 9, 0.5), 0.0, 1.0);
        cue.videoSynth.resolution = std::clamp(safeInt(fields, vs + 10, 2), 1, 5);
        cue.videoSynth.pixelSort = std::clamp(safeDouble(fields, vs + 11, 0.0), 0.0, 1.0);
        cue.videoSynth.glitch = std::clamp(safeDouble(fields, vs + 12, 0.0), 0.0, 1.0);
        cue.videoSynth.ascii = safeBool(fields, vs + 13, false);
        cue.videoSynth.asciiCols = std::clamp(safeInt(fields, vs + 14, 80), 20, 200);
        cue.videoSynth.asciiGreen = safeBool(fields, vs + 15, true);
        cue.videoSynth.crt = std::clamp(safeDouble(fields, vs + 16, 0.0), 0.0, 1.0);
        cue.videoSynth.asciiCharSet = std::clamp(safeInt(fields, vs + 17, 0), 0, 7);
        cue.videoSynth.asciiShuffle = std::clamp(safeInt(fields, vs + 18, 0), 0, 8);
        // Older shows carry only the green boolean; map it onto the ink mode
        // so they reopen looking the way they were left.
        cue.videoSynth.asciiInk =
          std::clamp(safeInt(fields, vs + 19, cue.videoSynth.asciiGreen ? 1 : 0), 0, 5);
        cue.videoSynth.spriteSheetPath = safeString(fields, vs + 20);
        cue.videoSynth.spriteTileW = std::clamp(safeInt(fields, vs + 21, 16), 8, 128);
        cue.videoSynth.spriteTileH = std::clamp(safeInt(fields, vs + 22, 16), 8, 128);
        cue.videoSynth.spriteRotate = std::clamp(safeInt(fields, vs + 23, 0), 0, 5);
        cue.videoSynth.spriteFreeAngle =
          std::clamp(safeDouble(fields, vs + 24, 0.0), -720.0, 720.0);
        cue.videoSynth.spriteFlip = std::clamp(safeInt(fields, vs + 25, 0), 0, 3);
        cue.videoSynth.spriteJitter =
          std::clamp(safeDouble(fields, vs + 26, 0.0), 0.0, 1.0);
        cue.videoSynth.spriteChaos =
          std::clamp(safeDouble(fields, vs + 27, 0.0), 0.0, 1.0);
        // APPENDED, so they read from the END of the record — which is where
        // saveProject writes them. They were read from tb+22/tb+23 instead,
        // i.e. inserted into the MIDDLE, which silently shifted the loader's
        // view of every field after them by two: the whole tone block and all
        // ~47 video-synth fields. Symptom: a cue named "Test Tone 1kHz" came
        // back as 20Hz at -1.0dBFS, because frequencyHz was reading the
        // channel column (-1, clamped up to 20) and levelDbfs was reading the
        // visual column (1, clamped down to -1).
        //
        // Append new cue fields at the END and read them at the END. Inserting
        // mid-record corrupts everything downstream of the insertion.
        cue.timer.logoPath          = safeString(fields, vs + 28);
        cue.timer.logoHeightPercent = std::clamp(safeInt(fields, vs + 29, 18), 2, 40);
        // At the END, per the warning above. Absent on every show saved before
        // effects existed, which safeString reports as empty and parses to an
        // empty stack -- so an old show simply has no effects, which is right.
        cue.effects = parseCueEffects(safeString(fields, vs + 30));
        cue.motionDriverPath = safeString(fields, vs + 31);
        cue.motionDriverSpeed = std::clamp(
          static_cast<float>(safeDouble(fields, vs + 32, 1.0)), 0.0f, 4.0f);
        cue.motionDriverPaused = safeBool(fields, vs + 33, false);
        cue.motionDriverRestartOnTake = safeBool(fields, vs + 34, true);
        // Appended after the motion driver. Absent on every older save, which
        // safeString reports as empty -- and an empty expression leaves the
        // cue's default in place rather than rendering black.
        {
          const std::string expr = safeString(fields, vs + 35);
          if (!expr.empty()) {
            cue.codeExpression = expr;
          }
        }
        cue.videoSynth.asciiGlyphs = safeString(fields, vs + 36);
        cue.videoSynth.asciiPhrases = safeString(fields, vs + 37);
        // Absent on older shows, where safeDouble returns the default. 2.5s is
        // the pace a phrase can be read at before it moves.
        cue.videoSynth.asciiPhraseHold =
          std::clamp(safeDouble(fields, vs + 38, 2.5), 0.0, 60.0);
        // Absent on every show written before glitch text existed, where
        // safeDouble returns the default and the marks stay switched off.
        cue.videoSynth.asciiZalgoUp =
          std::clamp(safeDouble(fields, vs + 39, 0.0), 0.0, 1.0);
        cue.videoSynth.asciiZalgoDown =
          std::clamp(safeDouble(fields, vs + 40, 0.0), 0.0, 1.0);
        cue.videoSynth.asciiZalgoMid =
          std::clamp(safeDouble(fields, vs + 41, 0.0), 0.0, 1.0);
        cue.videoSynth.asciiZalgoReach =
          std::clamp(safeInt(fields, vs + 42, 2), 1, 6);
        cue.videoSynth.asciiZalgoDrift =
          std::clamp(safeDouble(fields, vs + 43, 0.0), 0.0, 1.0);
        // Defaults to 0 -- strictly by brightness -- so every show saved before
        // this existed looks exactly as it did.
        cue.videoSynth.asciiChaos =
          std::clamp(safeDouble(fields, vs + 44, 0.0), 0.0, 1.0);
      }
      if (!cue.path.empty()) {
        if (cue.name.empty()) {
          cue.name = fs::path(cue.path).stem().string();
        }
        ensureDeck(deckIndex).cues.push_back(cue);
      }
    } else {
      // A LINE WE DO NOT UNDERSTAND.
      //
      // Which is what the back half of a truncated show looks like, and what a
      // show written by a newer build looks like. Either way this project is
      // not a faithful reading of that file, and the auto-save must not write
      // it back over the original 300ms later -- which is exactly what happened
      // to a show truncated mid-cue during testing, destroying the bytes a
      // recovery tool could still have used.
      project.loadedCleanly = false;
    }
  }

  normalizeProject(project);
  return project;
}
