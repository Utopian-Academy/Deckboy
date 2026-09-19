// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// audio_plugin.hpp — Third-party audio plugins, as slots in a cue's chain.
//
// WHAT THIS IS FOR: a cue's audio chain is Deckboy's own fourteen effects and
// eight bends. This lets an operator put THEIR plugins in the same chain -- a
// reverb they already own, the compressor their mix is built around -- without
// leaving the cue deck.
//
// FORMATS. VST3 in everything Deckboy ships, and that is a licensing fact
// rather than a preference: the interface headers this host is built against
// are MIT (see native/extras/vst3-sdk), so a GPL-3 project may carry them
// outright. The FULL VST3 SDK -- which Deckboy does not need and does not
// include -- is the dual-licensed GPLv3-or-proprietary one. They are different
// packages and the distinction is worth keeping straight.
//
// #########################################################################
// ##                                                                     ##
// ##         V S T 2 :   T H E   S W I T C H   W E   L E F T             ##
// ##                                                                     ##
// ##  Steinberg took the VST2 SDK away in 2018 and hands out no new      ##
// ##  licences. Its terms and the GPL do not get on. We think that is a  ##
// ##  daft way to treat twenty years of perfectly good plugins, and we   ##
// ##  are going to respect it anyway, because the alternative is a       ##
// ##  lawyer reading our commit messages.                                ##
// ##                                                                     ##
// ##  So: no Deckboy release is built with VST2, and none will be. No    ##
// ##  part of that SDK is in this repository -- the backend declares the ##
// ##  handful of structs and opcodes it needs and includes none of       ##
// ##  Steinberg's headers.                                               ##
// ##                                                                     ##
// ##  But the switch is right there, unlocked, with the key in it:       ##
// ##                                                                     ##
// ##    -DENABLE_VST2=ON -DVST2_SDK_DIR=/your/own/copy                   ##
// ##                                                                     ##
// ##  Off by default, refuses to configure without a path to an SDK YOU  ##
// ##  hold a licence for. Flip it and the plugins you already own work.  ##
// ##  Flip it and you are the one distributing that build, under your    ##
// ##  own terms -- which, conveniently, is exactly what a licence is.    ##
// ##                                                                     ##
// ##  Flipping it for something you then publish as Deckboy: don't.      ##
// ##  That is the one way to make this everybody's problem.              ##
// ##                                                                     ##
// ##  See native/platform/audio_plugin_vst2.inc and the ENABLE_VST2      ##
// ##  block in CMakeLists.txt.                                           ##
// ##                                                                     ##
// #########################################################################
//
// CLAP is the obvious next format: MIT, one header, and the interface below is
// deliberately format-agnostic so it can be added behind it.
//
// THE AUDIO THREAD IS THE WHOLE DESIGN PROBLEM. Deckboy's own effects are
// written to a rule: no allocation, no locks, bounded work. A plugin is
// somebody else's code and honours no such rule -- it may allocate, take a
// mutex, or spend thirty milliseconds on a buffer, and doing any of that on the
// audio thread is a click in the middle of a show. So every instance here
// carries a time budget and is BYPASSED, loudly, when it overruns; see
// AudioPluginInstance::overran.
//
// Implementation: audio_plugin.cpp
// ============================================================================

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace deckboy::platform::audioplugin {

// What a plugin is, as far as a show file is concerned. `id` is the durable
// reference a cue stores: it survives the plugin moving folders, because it
// carries the format and the plugin's own class id rather than a path alone.
struct PluginDescriptor {
  std::string id;          // "vst3:<class-uid>" -- what a cue stores
  std::string name;        // "Pro-Q 4"
  std::string vendor;      // "FabFilter"
  std::string format;      // "vst3"
  std::string path;        // the bundle or module on disk, for diagnostics
  bool isInstrument = false;   // true = it makes sound, false = it treats it
  std::string category;    // the plugin's own category string, if it gives one
};

// One parameter, as the inspector draws it. Plugins expose hundreds; the rack
// shows them as ordinary Deckboy rows, which is why they are normalised 0-1
// here rather than passed through in whatever units the plugin thinks in.
struct PluginParameter {
  std::uint32_t id = 0;
  std::string name;
  double defaultValue = 0.0;   // normalised 0-1
  bool automatable = true;
};

// A loaded plugin, processing one cue's audio.
//
// Instances are created and destroyed on the MAIN thread and processed on the
// audio thread. Nothing here allocates once processing has started.
class AudioPluginInstance {
 public:
  virtual ~AudioPluginInstance() = default;

  // Interleaved stereo, in place, `frames` sample-frames. Returns false when
  // the plugin has been bypassed for overrunning its budget -- the caller then
  // leaves the audio as it found it.
  virtual bool process(float* interleavedStereo, int frames) = 0;

  virtual const std::vector<PluginParameter>& parameters() const = 0;
  virtual void setParameter(std::uint32_t id, double normalised) = 0;
  virtual double parameter(std::uint32_t id) const = 0;

  // A note for an instrument. Ignored by effects.
  virtual void noteOn(int channel, int key, double velocity) = 0;
  virtual void noteOff(int channel, int key) = 0;

  // The plugin's own state, for saving in the show. Opaque and plugin-defined.
  virtual std::string saveState() const = 0;
  virtual bool loadState(const std::string& state) = 0;

  // True once the plugin has overrun its time budget often enough to be taken
  // out of the chain. The app reports it rather than letting a show click.
  virtual bool overran() const = 0;
  virtual const PluginDescriptor& descriptor() const = 0;
};

// Every plugin this machine has, from the standard folders for the platform.
// Scanning opens each module, so it is slow enough to do once and cache; the
// app calls this off the main thread at startup.
std::vector<PluginDescriptor> scanAudioPlugins();

// The folders scanned, for the diagnostics screen and for "no plugins found"
// to be able to say WHERE it looked.
std::vector<std::string> audioPluginSearchPaths();

// Load one, by the id a cue stored. Null when the plugin is not installed on
// this machine, which is a thing a touring show has to survive: the cue keeps
// its id and its state, and says which plugin is missing.
std::unique_ptr<AudioPluginInstance> openAudioPlugin(const std::string& id,
                                                     double sampleRate,
                                                     int maxBlockFrames);

// False in a build without plugin support, so the UI can say so rather than
// showing an empty list that looks like "you own no plugins".
bool audioPluginsSupported();

}  // namespace deckboy::platform::audioplugin
