// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// media_probe.cpp — see media_probe.hpp. Moved here from main.cpp unchanged.

#include "core/media_probe.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include "core/subprocess.hpp"
#include "core/utils.hpp"
#include "platform/pdf_import.hpp"

namespace deckboy::core::media {

namespace fs = std::filesystem;
using namespace deckboy::core::utils;

bool isImagePath(const fs::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  static const std::array<std::string, 12> kImageExts {
    ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif", ".tif", ".tiff", ".avif",
    // Apple stills. An iPhone photo is HEIC (a single-frame "Main Still
    // Picture" HEVC in a HEIF container); without these it was classified as a
    // VIDEO cue and the transport waited forever for a stream that only ever
    // yields one frame — the "clip stuck loading" an operator hits the first
    // time they drop a photo straight off a phone. ffmpeg decodes HEIC/HEIF on
    // every platform, and the still path already uses ffmpeg, so this works
    // everywhere, not just macOS.
    ".heic", ".heif"
  };
  return std::find(kImageExts.begin(), kImageExts.end(), ext) != kImageExts.end();
}

// A MIDI FILE IS NOT AUDIO, whatever the folder it lives in says. It carries
// no sound at all -- it is a score, and what it needs is an instrument on the
// other end of a cable. Classed as audio it would be handed to the decoder,
// which would find no stream and rack a silent cue that looks broken.
//
// THE EXTENSION IS ONLY THE FIRST QUESTION. The bytes are checked for MThd
// before a cue is made, because a .mid that is really something else should be
// refused rather than played as nothing.
bool isMidiFilePath(const fs::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return ext == ".mid" || ext == ".midi" || ext == ".smf";
}

bool isAudioPath(const fs::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  static const std::array<std::string, 10> kAudioExts {
    ".mp3", ".wav", ".flac", ".aac", ".ogg", ".oga", ".m4a", ".opus", ".wma", ".aiff"
  };
  return std::find(kAudioExts.begin(), kAudioExts.end(), ext) != kAudioExts.end();
}

// True for files we accept when a folder is dropped/imported (video/image/audio).
bool isAcceptableMediaPath(const fs::path& path) {
  if (isImagePath(path) || isAudioPath(path)) {
    return true;
  }
  // A PDF is acceptable to IMPORT even though it never becomes a cue itself --
  // importPaths turns it into one still per page. Without this, dropping a
  // folder of show material silently skipped the slide decks in it.
  if (deckboy::platform::isPdfDocumentPath(path)) {
    return true;
  }
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  static const std::array<std::string, 14> kVideoExts {
    ".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v", ".mpg", ".mpeg",
    ".wmv", ".flv", ".ogv", ".ts", ".m2ts", ".mxf"
  };
  return std::find(kVideoExts.begin(), kVideoExts.end(), ext) != kVideoExts.end();
}

std::optional<double> parseFps(const std::string& rate) {
  auto slash = rate.find('/');
  if (slash == std::string::npos) {
    return std::nullopt;
  }
  try {
    double numerator = std::stod(rate.substr(0, slash));
    double denominator = std::stod(rate.substr(slash + 1));
    if (denominator == 0.0) {
      return std::nullopt;
    }
    return numerator / denominator;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<Cue> probeCue(const fs::path& mediaPath) {
  auto output = readAllText({
    "ffprobe",
    "-v",
    "error",
    "-show_entries",
    // nb_frames is here for one reason: telling an animated GIF from a still
    // one. Duration cannot do it -- a two-frame GIF runs for under a tenth of
    // a second, which is indistinguishable from the 0.04 a single frame
    // reports -- and the difference decides whether the cue moves or freezes.
    "format=duration,format_name,size:stream=codec_type,codec_name,width,height,r_frame_rate,channels,sample_rate,nb_frames",
    "-of",
    "default=noprint_wrappers=1",
    mediaPath.string()
  });

  if (!output) {
    return std::nullopt;
  }

  Cue cue;
  cue.path = mediaPath.string();
  cue.name = mediaPath.stem().string();
  cue.kind = isImagePath(mediaPath) ? CueKind::Image
           : isAudioPath(mediaPath) ? CueKind::Audio
           : CueKind::Video;
  cue.fps = cue.kind == CueKind::Image ? 0.0 : 30.0;

  // ffprobe may emit codec_name before or after codec_type depending on version/format.
  // Buffer the codec_name and apply it once we know the stream's codec_type.
  std::string lastCodecType;
  std::string pendingCodecName;
  bool pendingApplied = false;
  // Frames in the video stream, for the animated-GIF test below. A local
  // rather than a Cue field: nothing else wants it, and a field nothing reads
  // is the shape of problem the reachability audit exists to catch.
  long videoStreamFrames = 0;
  auto tryApplyCodec = [&]() {
    if (pendingCodecName.empty() || lastCodecType.empty() || pendingApplied) return;
    pendingApplied = true;
    if (lastCodecType == "video" && cue.videoCodec.empty()) {
      cue.videoCodec = pendingCodecName;
    } else if (lastCodecType == "audio" && cue.audioCodec.empty()) {
      cue.audioCodec = pendingCodecName;
      cue.hasAudio = true;
    } else if (lastCodecType == "subtitle" && cue.subtitleStreamId.empty()) {
      cue.subtitleStreamId = "0:s:0";
    }
  };
  for (const auto& line : splitLines(*output)) {
    auto sep = line.find('=');
    if (sep == std::string::npos) {
      continue;
    }
    std::string key = line.substr(0, sep);
    std::string value = line.substr(sep + 1);

    if (key == "codec_type") {
      // If the previous stream's pair was already applied, this codec_type
      // marks a new stream — clear any stale pendingCodecName left over from
      // the previous stream so we don't mis-pair (e.g. audio-first mp4 where
      // codec_name=h264 arrives while lastCodecType is still "audio").
      if (pendingApplied) {
        pendingCodecName.clear();
        pendingApplied = false;
      }
      lastCodecType = value;
      if (value == "audio") {
        ++cue.audioTrackCount;   // every sound track, so a player can offer the others
      } else if (value == "subtitle") {
        ++cue.subtitleTrackCount;
      }
      tryApplyCodec();
    } else if (key == "codec_name") {
      // Symmetric: if the previous stream was paired, this codec_name is a
      // new stream boundary — clear the stale lastCodecType.
      if (pendingApplied) {
        lastCodecType.clear();
      }
      pendingCodecName = value;
      pendingApplied = false;
      tryApplyCodec();
    } else if (key == "width") {
      if (lastCodecType == "video" && cue.width == 0) {
        cue.width = std::max(0, std::atoi(value.c_str()));
      }
    } else if (key == "height") {
      if (lastCodecType == "video" && cue.height == 0) {
        cue.height = std::max(0, std::atoi(value.c_str()));
      }
    } else if (key == "r_frame_rate" && cue.kind == CueKind::Video) {
      auto fps = parseFps(value);
      if (fps && *fps > 1.0) {
        cue.fps = *fps;
      }
    } else if (key == "channels" && lastCodecType == "audio") {
      cue.audioChannels = std::max(0, std::atoi(value.c_str()));
    } else if (key == "sample_rate" && lastCodecType == "audio") {
      cue.audioSampleRate = std::max(0, std::atoi(value.c_str()));
    } else if (key == "nb_frames" && lastCodecType == "video") {
      // "N/A" for anything the demuxer cannot count without decoding, which is
      // most formats. A GIF is one of the few that reports honestly, and it is
      // the only one this is asked about.
      videoStreamFrames = std::strtol(value.c_str(), nullptr, 10);
    } else if (key == "duration" && (cue.kind == CueKind::Video || cue.duration == 0.0)) {
      double d = std::atof(value.c_str());
      if (d > 0.0) cue.duration = d;
    } else if (key == "format_name") {
      cue.formatName = value;
    } else if (key == "size") {
      cue.sizeBytes = static_cast<std::uintmax_t>(std::strtoull(value.c_str(), nullptr, 10));
    }
  }

  // Detect rotation from side_data (phone videos) and swap width/height if needed
  if (cue.width > 0 && cue.height > 0 && !cue.videoCodec.empty()) {
    auto sideData = readAllText({
      "ffprobe", "-v", "error", "-select_streams", "v:0",
      "-show_entries", "stream_side_data=rotation",
      "-of", "default=noprint_wrappers=1",
      mediaPath.string()
    });
    if (sideData) {
      for (const auto& line : splitLines(*sideData)) {
        auto sep = line.find('=');
        if (sep != std::string::npos && line.substr(0, sep) == "rotation") {
          int rot = std::abs(std::atoi(line.substr(sep + 1).c_str()));
          if (rot == 90 || rot == 270) {
            std::swap(cue.width, cue.height);
          }
        }
      }
    }
  }

  // Auto-detect audio-only: no video stream but has audio
  if (cue.videoCodec.empty() && cue.hasAudio) {
    cue.kind = CueKind::Audio;
    // Audio cues don't have video dimensions — give them nominal size
    if (cue.width <= 0) cue.width = 1;
    if (cue.height <= 0) cue.height = 1;
  }
  // AN ANIMATED GIF IS A MOVING PICTURE.
  //
  // .gif sits in the image extensions, which is right for the still ones and
  // wrong for the rest: an animated GIF came in as an Image cue, which decodes
  // exactly one frame, so it went to air as a frozen first frame with nothing
  // said. Slide decks arrive full of them.
  //
  // Decided by what the FILE contains rather than by its extension, which is
  // the same correction the audio-only branch above makes: a GIF reporting
  // more than one frame is a video cue, and ffmpeg decodes it as one. A
  // single-frame GIF stays a still, so nothing that already worked changes.
  if (cue.kind == CueKind::Image && cue.videoCodec == "gif" && videoStreamFrames > 1) {
    cue.kind = CueKind::Video;
    if (cue.fps <= 0.0) {
      cue.fps = 30.0;
    }
  }
  if (cue.width <= 0 || cue.height <= 0) {
    return std::nullopt;
  }
  if ((cue.kind == CueKind::Video || cue.kind == CueKind::Audio) && cue.duration <= 0.0) {
    cue.duration = 0.0;
  }
  return cue;
}

std::string extractEmbeddedSubtitleSrt(const std::string& mediaPath, const std::string& streamId) {
  const std::string mapArg = streamId.empty() ? "0:s:0" : streamId;
  auto result = readAllText({"ffmpeg", "-v", "error", "-i", mediaPath, "-map", mapArg, "-f", "srt", "pipe:1"});
  return result.value_or("");
}

}  // namespace deckboy::core::media
