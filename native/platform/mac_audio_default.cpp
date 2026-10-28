// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// mac_audio_default.cpp -- see mac_audio_default.hpp.

#include "platform/mac_audio_default.hpp"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <vector>

namespace deckboy::platform {

std::string macDefaultPlaybackDeviceName() {
  // kAudioObjectPropertyElementMain is kAudioObjectPropertyElementMaster
  // before the macOS 12 SDK. Both are 0, and the literal builds against both
  // without an availability warning in the 10.13 build.
  constexpr AudioObjectPropertyElement kMainElement = 0;
  const AudioObjectPropertyAddress defaultAddress = {
    kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kMainElement};
  AudioDeviceID device = kAudioObjectUnknown;
  UInt32 size = sizeof(device);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddress, 0, nullptr, &size,
                                 &device) != noErr ||
      device == kAudioObjectUnknown) {
    return {};
  }

  // The property, scope and trimming SDL names a playback device with
  // (RefreshPhysicalDevices in SDL_coreaudio.m), so this matches its list.
  const AudioObjectPropertyAddress nameAddress = {
    kAudioObjectPropertyName, kAudioDevicePropertyScopeOutput, kMainElement};
  CFStringRef cfName = nullptr;
  size = sizeof(cfName);
  if (AudioObjectGetPropertyData(device, &nameAddress, 0, nullptr, &size, &cfName) != noErr ||
      !cfName) {
    return {};
  }
  const CFIndex capacity =
    CFStringGetMaximumSizeForEncoding(CFStringGetLength(cfName), kCFStringEncodingUTF8) + 1;
  std::vector<char> buffer(static_cast<std::size_t>(capacity), '\0');
  std::string name;
  if (CFStringGetCString(cfName, buffer.data(), capacity, kCFStringEncodingUTF8)) {
    name = buffer.data();
  }
  CFRelease(cfName);
  while (!name.empty() && name.back() == ' ') {
    name.pop_back();
  }
  return name;
}

}  // namespace deckboy::platform
