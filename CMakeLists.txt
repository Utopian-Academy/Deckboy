cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" DECKBOY_VERSION_RAW)
string(STRIP "${DECKBOY_VERSION_RAW}" DECKBOY_VERSION_STRING)
if(NOT DECKBOY_VERSION_STRING MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.-]+)?$")
  message(FATAL_ERROR
    "VERSION must use SemVer: MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-prerelease"
  )
endif()

set(DECKBOY_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(DECKBOY_VERSION_MINOR "${CMAKE_MATCH_2}")
set(DECKBOY_VERSION_PATCH "${CMAKE_MATCH_3}")
set(DECKBOY_VERSION_PRERELEASE "${CMAKE_MATCH_4}")
set(DECKBOY_VERSION_TAG "v${DECKBOY_VERSION_STRING}")

project(deckboy_native VERSION
  ${DECKBOY_VERSION_MAJOR}.${DECKBOY_VERSION_MINOR}.${DECKBOY_VERSION_PATCH}
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(deckboy_target_link_frameworks target)
  if(NOT APPLE)
    return()
  endif()

  foreach(framework IN LISTS ARGN)
    find_library(DECKBOY_${framework}_FRAMEWORK NAMES ${framework})
    if(NOT DECKBOY_${framework}_FRAMEWORK)
      message(FATAL_ERROR "Required macOS framework not found: ${framework}")
    endif()
    target_link_libraries(${target} PRIVATE ${DECKBOY_${framework}_FRAMEWORK})
  endforeach()
endfunction()

# SDL3 + SDL3_ttf (migrated from SDL2, v0.77.0). Prefer CMake config packages
# (vcpkg on Windows); fall back to pkg-config on Linux.
set(DECKBOY_SDL_TARGET "")
set(DECKBOY_SDL_TTF_TARGET "")
set(DECKBOY_SDL_INCLUDE_DIRS "")
set(DECKBOY_SDL_TTF_INCLUDE_DIRS "")
set(DECKBOY_SDL_LINK_LIBS "")
set(DECKBOY_SDL_TTF_LINK_LIBS "")
set(DECKBOY_SDL_COMPILE_OPTIONS "")
set(DECKBOY_SDL_LINK_DIRS "")

find_package(SDL3 QUIET CONFIG)
find_package(SDL3_ttf QUIET CONFIG)

if(TARGET SDL3::SDL3)
  set(DECKBOY_SDL_TARGET SDL3::SDL3)
elseif(TARGET SDL3::SDL3-static)
  set(DECKBOY_SDL_TARGET SDL3::SDL3-static)
endif()

if(TARGET SDL3_ttf::SDL3_ttf)
  set(DECKBOY_SDL_TTF_TARGET SDL3_ttf::SDL3_ttf)
elseif(TARGET SDL3_ttf::SDL3_ttf-static)
  set(DECKBOY_SDL_TTF_TARGET SDL3_ttf::SDL3_ttf-static)
endif()

# Unconditional: pkg_check_modules is also used further down (RtMidi), and
# looking PkgConfig up only inside the SDL fallback meant that when SDL3 came
# from a CONFIG package the command was never defined — so configuring with
# -DENABLE_MIDI=ON failed outright with "Unknown CMake command".
find_package(PkgConfig QUIET)

if(PkgConfig_FOUND AND NOT DECKBOY_SDL_TARGET)
  pkg_check_modules(SDL3 QUIET sdl3)
  set(DECKBOY_SDL_INCLUDE_DIRS ${SDL3_INCLUDE_DIRS})
  set(DECKBOY_SDL_LINK_LIBS ${SDL3_LIBRARIES})
  list(APPEND DECKBOY_SDL_COMPILE_OPTIONS ${SDL3_CFLAGS_OTHER})
  list(APPEND DECKBOY_SDL_LINK_DIRS ${SDL3_LIBRARY_DIRS})
endif()

if(PkgConfig_FOUND AND NOT DECKBOY_SDL_TTF_TARGET)
  pkg_check_modules(SDL3_TTF QUIET sdl3-ttf)
  set(DECKBOY_SDL_TTF_INCLUDE_DIRS ${SDL3_TTF_INCLUDE_DIRS})
  set(DECKBOY_SDL_TTF_LINK_LIBS ${SDL3_TTF_LIBRARIES})
  list(APPEND DECKBOY_SDL_COMPILE_OPTIONS ${SDL3_TTF_CFLAGS_OTHER})
  list(APPEND DECKBOY_SDL_LINK_DIRS ${SDL3_TTF_LIBRARY_DIRS})
endif()

if(NOT DECKBOY_SDL_TARGET AND NOT DECKBOY_SDL_LINK_LIBS)
  message(FATAL_ERROR "SDL3 development files are required")
endif()
if(NOT DECKBOY_SDL_TTF_TARGET AND NOT DECKBOY_SDL_TTF_LINK_LIBS)
  message(FATAL_ERROR "SDL3_ttf development files are required")
endif()

option(DECKBOY_ENABLE_NDI "Enable optional NDI SDK integration" ON)
if(DECKBOY_ENABLE_NDI)
  find_path(DECKBOY_NDI_INCLUDE_DIR
    NAMES Processing.NDI.Lib.h
    PATHS
      "$ENV{DECKBOY_NDI_SDK}/include"
      "$ENV{NDI_SDK_DIR}/Include"
      # Windows — NDI 6/5 SDK standard install locations
      "C:/Program Files/NDI/NDI 6 SDK/Include"
      "C:/Program Files/NDI/NDI 5 SDK/Include"
      # Linux — standard install locations (installer extracts to CWD)
      "/opt/NDI SDK for Linux/include"
      "$ENV{HOME}/NDI SDK for Linux/include"
      "/usr/local/include"
      "/usr/include"
  )
endif()

option(DECKBOY_ENABLE_ALSA_MTC "Enable optional ALSA MTC ingest integration" ON)
if(UNIX AND NOT APPLE AND DECKBOY_ENABLE_ALSA_MTC)
  find_path(DECKBOY_ALSA_INCLUDE_DIR
    NAMES asoundlib.h
    PATH_SUFFIXES alsa
    PATHS
      /usr/include
      /usr/local/include
  )
  find_library(DECKBOY_ALSA_LIBRARY
    NAMES asound
  )
endif()

# In-process libav* decode (GPU decode rewrite, docs/GPU_DECODE_PLAN.md).
# When ON and the FFmpeg dev libraries are found, file-backed Video/Audio
# cues decode in-process (d3d11va zero-copy on Windows) instead of spawning
# ffmpeg subprocess pipes. OFF (or libs missing) keeps the CLI pipe path —
# the break-glass fallback build.
option(DECKBOY_INPROC_DECODE "Decode media in-process via libav (GPU zero-copy)" ON)
set(DECKBOY_INPROC_DECODE_ACTIVE OFF)
if(DECKBOY_INPROC_DECODE)
  find_path(DECKBOY_AVCODEC_INCLUDE_DIR NAMES libavcodec/avcodec.h)
  find_library(DECKBOY_AVCODEC_LIBRARY NAMES avcodec)
  find_library(DECKBOY_AVFORMAT_LIBRARY NAMES avformat)
  find_library(DECKBOY_AVUTIL_LIBRARY NAMES avutil)
  find_library(DECKBOY_AVFILTER_LIBRARY NAMES avfilter)
  find_library(DECKBOY_SWSCALE_LIBRARY NAMES swscale)
  find_library(DECKBOY_SWRESAMPLE_LIBRARY NAMES swresample)
  if(DECKBOY_AVCODEC_INCLUDE_DIR AND DECKBOY_AVCODEC_LIBRARY AND
     DECKBOY_AVFORMAT_LIBRARY AND DECKBOY_AVUTIL_LIBRARY AND
     DECKBOY_AVFILTER_LIBRARY AND DECKBOY_SWSCALE_LIBRARY AND
     DECKBOY_SWRESAMPLE_LIBRARY)
    set(DECKBOY_INPROC_DECODE_ACTIVE ON)
    message(STATUS "In-process libav decode enabled: ${DECKBOY_AVCODEC_LIBRARY}")
  else()
    message(WARNING "DECKBOY_INPROC_DECODE=ON but FFmpeg dev libraries were not found; building with the ffmpeg CLI decode path only")
  endif()
endif()

# Pro Features Feature Gates
option(ENABLE_DECKLINK "Build with Blackmagic DeckLink 10-bit SDI/HDMI output" OFF)
option(ENABLE_MIDI "Build with RtMidi cross-platform MIDI input" OFF)
option(ENABLE_SIPHON "Build with macOS Siphon interprocess texture sharing" OFF)
option(ENABLE_SPOUT "Build with Windows Spout interprocess texture sharing" OFF)
option(ENABLE_CEF "Build with Chromium Embedded Framework browser cues" OFF)
option(ENABLE_WEBVIEW "Build with native WebView APIs (macOS WKWebView, Windows WebView2)" OFF)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
# Generate version header at build time so editing VERSION triggers a rebuild
# without needing to re-run cmake configure.
set(GENERATED_VERSION_HPP "${CMAKE_CURRENT_BINARY_DIR}/generated/deckboy_version.hpp")
add_custom_command(
  OUTPUT "${GENERATED_VERSION_HPP}"
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DOUTPUT_FILE=${GENERATED_VERSION_HPP}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_version.cmake"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/version.hpp.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_version.cmake"
  COMMENT "Generating deckboy_version.hpp from VERSION"
  VERBATIM
)
add_custom_target(deckboy_generate_version DEPENDS "${GENERATED_VERSION_HPP}")

# DeckLink SDK (Blackmagic DeckLink SDK 16.x)
#
# The SDK ships platform-specific headers:
#   Windows: .idl files → compiled by MIDL → DeckLinkAPI_h.h + DeckLinkAPI_i.c
#   Linux:   .h files + DeckLinkAPIDispatch.cpp (dynamic loader stub)
#   macOS:   .h files + DeckLinkAPIDispatch.cpp (dynamic loader stub)
#
# Set DECKLINK_SDK_DIR (cmake var or env) to the SDK root, e.g.:
#   -DDECKLINK_SDK_DIR="G:/Blackmagic_DeckLink_SDK_16.0/Blackmagic DeckLink SDK 16.0"
#
if(ENABLE_DECKLINK)
  # Resolve SDK root from cmake variable, then environment variable
  if(NOT DECKLINK_SDK_DIR)
    set(DECKLINK_SDK_DIR "$ENV{DECKLINK_SDK_DIR}")
  endif()
  # Also accept the older env var name
  if(NOT DECKLINK_SDK_DIR)
    set(DECKLINK_SDK_DIR "$ENV{DECKLINK_SDK}")
  endif()

  if(WIN32)
    # Windows SDK uses .idl files in Win/include/
    find_path(DECKLINK_IDL_DIR
      NAMES DeckLinkAPI.idl
      PATHS
        "${DECKLINK_SDK_DIR}/Win/include"
        "$ENV{DECKLINK_SDK}/Win/include"
      NO_DEFAULT_PATH
    )
    if(NOT DECKLINK_IDL_DIR)
      # Also try auto-discovery under common install paths
      find_path(DECKLINK_IDL_DIR
        NAMES DeckLinkAPI.idl
        PATHS
          "C:/Program Files/Blackmagic Design/DeckLink SDK/Win/include"
          "C:/Blackmagic DeckLink SDK/Win/include"
      )
    endif()
    if(NOT DECKLINK_IDL_DIR)
      message(FATAL_ERROR "DeckLink SDK not found (Windows). Set DECKLINK_SDK_DIR to the SDK root (containing Win/include/DeckLinkAPI.idl)")
    endif()
    message(STATUS "DeckLink support enabled (Windows IDL): ${DECKLINK_IDL_DIR}")
  else()
    # Linux / macOS SDK uses .h files directly
    if(APPLE)
      set(_DECKLINK_PLATFORM_SUBDIR "Mac")
    else()
      set(_DECKLINK_PLATFORM_SUBDIR "Linux")
    endif()
    find_path(DECKLINK_INCLUDE_DIR
      NAMES DeckLinkAPI.h
      PATHS
        "${DECKLINK_SDK_DIR}/${_DECKLINK_PLATFORM_SUBDIR}/include"
        "$ENV{DECKLINK_SDK}/${_DECKLINK_PLATFORM_SUBDIR}/include"
        "$ENV{DECKLINK_SDK}/include"
        "/opt/Blackmagic DeckLink SDK/${_DECKLINK_PLATFORM_SUBDIR}/include"
        "/usr/local/include"
        "/usr/include"
    )
    if(NOT DECKLINK_INCLUDE_DIR)
      message(FATAL_ERROR "DeckLink SDK not found. Set DECKLINK_SDK_DIR to the SDK root or disable ENABLE_DECKLINK")
    endif()
    # Verify dispatch source exists
    if(NOT EXISTS "${DECKLINK_INCLUDE_DIR}/DeckLinkAPIDispatch.cpp")
      message(FATAL_ERROR "DeckLinkAPIDispatch.cpp not found in ${DECKLINK_INCLUDE_DIR}")
    endif()
    message(STATUS "DeckLink support enabled: ${DECKLINK_INCLUDE_DIR}")
  endif()
endif()

# RtMidi — auto-detect from vcpkg, then fallback to pkg-config / manual paths
find_package(rtmidi CONFIG QUIET)
if(rtmidi_FOUND AND NOT ENABLE_MIDI)
  set(ENABLE_MIDI ON CACHE BOOL "Build with RtMidi cross-platform MIDI input" FORCE)
  message(STATUS "RtMidi auto-detected from vcpkg, enabling MIDI support")
endif()

if(ENABLE_MIDI)
  if(NOT rtmidi_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(RTMIDI rtmidi QUIET)
  endif()

  if(NOT rtmidi_FOUND AND NOT RTMIDI_FOUND)
    find_path(RTMIDI_INCLUDE_DIR
      NAMES RtMidi.h
      PATHS
        "$ENV{RTMIDI_SDK}/include"
        "/usr/local/include"
        "/usr/include"
    )
    find_library(RTMIDI_LIBRARY
      NAMES rtmidi
      PATHS
        "$ENV{RTMIDI_SDK}/lib"
        "/usr/local/lib"
        "/usr/lib"
    )
  endif()

  if(rtmidi_FOUND OR RTMIDI_FOUND OR (RTMIDI_INCLUDE_DIR AND RTMIDI_LIBRARY))
    if(rtmidi_FOUND)
      message(STATUS "RtMidi support enabled (vcpkg)")
    elseif(RTMIDI_FOUND)
      message(STATUS "RtMidi support enabled (pkg-config)")
    else()
      message(STATUS "RtMidi support enabled: ${RTMIDI_LIBRARY}")
    endif()
  else()
    message(STATUS "RtMidi SDK not found, using stub implementation (no actual MIDI input)")
  endif()
else()
  message(STATUS "RtMidi support disabled (ENABLE_MIDI=OFF)")
endif()

# Syphon (macOS). NOTE: the framework is "Syphon" (with a y); the earlier
# scaffolding searched for "Siphon" and so could never have found it. The build
# OPTION keeps the historical ENABLE_SIPHON spelling for compatibility.
if(ENABLE_SIPHON AND APPLE)
  find_library(SYPHON_FRAMEWORK Syphon)
  if(NOT SYPHON_FRAMEWORK)
    message(FATAL_ERROR "Syphon.framework not found. Build it from "
      "github.com/Syphon/Syphon-Framework and install it where find_library can "
      "see it, or disable ENABLE_SIPHON. See docs/SYPHON_PLAN.md.")
  endif()
  message(STATUS "Syphon support enabled: ${SYPHON_FRAMEWORK}")
endif()

# Spout (Windows) — auto-detect from vcpkg or manual SDK path
if(WIN32)
  find_package(Spout2 CONFIG QUIET)
  if(Spout2_FOUND)
    set(ENABLE_SPOUT ON)
    message(STATUS "Spout support auto-detected and enabled (vcpkg)")
  elseif(ENABLE_SPOUT)
    find_path(SPOUT_INCLUDE_DIR
      NAMES SpoutLibrary.h
      PATHS
        "$ENV{SPOUT_SDK}/include"
        "C:/Spout/include"
    )
    if(NOT SPOUT_INCLUDE_DIR)
      message(FATAL_ERROR "Spout SDK not found on Windows. Disable ENABLE_SPOUT or set SPOUT_SDK env var")
    endif()
    message(STATUS "Spout support enabled (manual SDK)")
  endif()
endif()

# CEF (Chromium Embedded Framework)
if(ENABLE_CEF)
  # Headers alone are NOT enough to claim CEF support. The previous version of
  # this block located cef_api_hash.h, defined DECKBOY_HAS_CEF, and stopped —
  # so a build with -DENABLE_CEF=ON advertised CEF in the About dialog while
  # browser cues still went through Xvfb. Require the library too, and the
  # wrapper static lib that every real CEF app links.
  find_path(CEF_INCLUDE_DIR
    NAMES cef_api_hash.h
    PATHS
      "$ENV{CEF_SDK}/include"
      "/opt/cef/include"
      "/opt/cef"
    PATH_SUFFIXES include
  )
  find_library(CEF_LIBRARY
    NAMES cef libcef
    PATHS
      "$ENV{CEF_SDK}/Release"
      "$ENV{CEF_SDK}/lib"
      "/opt/cef/Release"
  )
  find_library(CEF_DLL_WRAPPER
    NAMES cef_dll_wrapper libcef_dll_wrapper
    PATHS
      "$ENV{CEF_SDK}/libcef_dll_wrapper"
      "$ENV{CEF_SDK}/build/libcef_dll_wrapper/Release"
      "$ENV{CEF_SDK}/Release"
      "/opt/cef/libcef_dll_wrapper"
  )
  if(NOT CEF_INCLUDE_DIR OR NOT CEF_LIBRARY OR NOT CEF_DLL_WRAPPER)
    message(FATAL_ERROR
      "ENABLE_CEF=ON but a complete CEF SDK was not found.\n"
      "  headers     : ${CEF_INCLUDE_DIR}\n"
      "  libcef      : ${CEF_LIBRARY}\n"
      "  dll_wrapper : ${CEF_DLL_WRAPPER}\n"
      "Set CEF_SDK to a CEF binary distribution whose libcef_dll_wrapper has "
      "been built, or disable ENABLE_CEF. See docs/BROWSER_CEF_PLAN.md.")
  endif()
  message(STATUS "CEF support enabled: ${CEF_LIBRARY}")
endif()

# WebView (macOS/Windows native)
if(ENABLE_WEBVIEW AND NOT APPLE AND NOT WIN32)
  message(FATAL_ERROR "ENABLE_WEBVIEW is only supported on macOS and Windows")
endif()

add_executable(deckboy-native
  native/main.cpp
  native/core/paths.cpp
  native/core/utils.cpp
  native/core/subprocess.cpp
  native/engine/media_engine.cpp
  native/platform/browser.cpp
  native/platform/capture_backend.cpp
  native/platform/integration_backend.cpp
  native/platform/output_backend.cpp
  # ST 2110-20 sender: plain sockets only, no SDK, so it compiles unconditionally
  # on every platform (same reasoning as decklink.cpp above — a feature-guarded
  # translation unit whose symbols are referenced unguarded breaks the link).
  native/platform/st2110_output.cpp
  # HAP frame decoding: container parse + a vendored Snappy decompressor, no
  # SDK and no new dependency, so it compiles unconditionally like the two
  # above. Snappy is vendored rather than linked because decompression is a
  # small frozen format and a per-platform dep is exactly what kept this CI
  # red for months.
  native/engine/hap_decoder.cpp
  # PTPv2 slave for ST 2059 media-clock alignment. Plain sockets, no SDK.
  native/platform/ptp_client.cpp
  # NMOS IS-04/IS-05 node: HTTP server + registration client for the 2110
  # senders. Plain sockets and a hand-rolled JSON reader, no SDK, so this also
  # compiles unconditionally.
  native/platform/nmos_node.cpp
  # Vendored Terrarium sim. This shim compiles upstream/terrarium_core.cpp
  # INSIDE `namespace terra` — do not add that .cpp here directly, or its
  # definitions land at global scope and fail to link. See
  # native/extras/upstream/UPSTREAM.md.
  native/extras/terrarium_vendor.cpp
  native/render/primitives.cpp
  native/render/text_renderer.cpp
  native/render/waveform_renderer.cpp
  native/render/output_renderer.cpp
)

set_target_properties(deckboy-native PROPERTIES OUTPUT_NAME "Deckboy")

# macOS screen capture helper. avfoundation screen capture is gone on current
# macOS, so window/screen cues run this small ScreenCaptureKit binary, which
# writes raw RGBA frames to stdout exactly like ffmpeg does. Built for every
# macOS build (no feature flag) so the bundle always ships it. @available guards
# the 12.3+ symbols, so the deployment target stays at the app's minimum.
if(APPLE)
  add_executable(deckboy-sckcapture native/platform/sckcapture.mm)
  set_target_properties(deckboy-sckcapture PROPERTIES OUTPUT_NAME "deckboy-sckcapture")
  target_link_libraries(deckboy-sckcapture PRIVATE
    "-framework ScreenCaptureKit"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework CoreGraphics"
    "-framework Foundation")
  # Deckboy depends on the helper existing next to it, so build it alongside.
  add_dependencies(deckboy-native deckboy-sckcapture)

  # The main app's window picker (listCaptureWindows in capture_backend.cpp)
  # enumerates on-screen windows via CoreGraphics/CoreFoundation, so the app
  # itself must link them (the helper's own link line above is separate).
  target_link_libraries(deckboy-native PRIVATE
    "-framework CoreGraphics"
    "-framework CoreFoundation")
endif()

# Export dynamic symbols on POSIX so the crash handler's backtrace resolves to
# function names. Without this, backtrace_symbols_fd can only print
# "Deckboy(+0x4b83b)" for our own frames — the crash log tells you a crash
# happened and nothing about where, which is most of its value gone. Verified
# on Linux: the difference between an offset and `App::renderOutputWindow`.
# Costs a slightly larger dynamic symbol table and nothing else.
if(NOT MSVC)
  set_target_properties(deckboy-native PROPERTIES ENABLE_EXPORTS ON)
  target_link_options(deckboy-native PRIVATE -rdynamic)
endif()
add_dependencies(deckboy-native deckboy_generate_version)
if(WIN32)
  set_target_properties(deckboy-native PROPERTIES WIN32_EXECUTABLE TRUE)

  # Embed the application icon as resource IDI_DECKBOY_APP_ICON. This drives the
  # Explorer/Alt-Tab/taskbar-pin icon AND is loaded at runtime by
  # applyDeckboyWindowIcon() (WM_SETICON) for the live window/taskbar icon.
  set(DECKBOY_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/art/windows/icons/deckboy_app.ico")
  if(EXISTS "${DECKBOY_APP_ICON}")
    file(TO_NATIVE_PATH "${DECKBOY_APP_ICON}" _deckboy_icon_native)
    string(REPLACE "\\" "\\\\" DECKBOY_APP_ICON_RC_PATH "${_deckboy_icon_native}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/art/windows/deckboy.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/deckboy.rc" @ONLY)
    target_sources(deckboy-native PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/deckboy.rc")
  else()
    message(WARNING "Deckboy app icon not found at ${DECKBOY_APP_ICON}; exe will use the default icon.")
  endif()
endif()

# Platform modules (conditional)
if(ENABLE_MIDI)
  target_sources(deckboy-native PRIVATE native/platform/midi.cpp)
endif()

# decklink.cpp compiles unconditionally: it already carries a full stub
# implementation behind !DECKBOY_HAS_DECKLINK (listDevices returns {}, init
# reports unavailable). The UI calls DeckLinkOutput::listDevices() and the
# mode helpers without feature guards, so excluding the whole translation unit
# left those symbols undefined and broke every build with ENABLE_DECKLINK=OFF
# at link time. That never showed up locally because the Windows dev build
# has DeckLink enabled.
target_sources(deckboy-native PRIVATE native/platform/decklink.cpp)

if(ENABLE_DECKLINK)
  if(WIN32)
    # On Windows, compile the IDL with MIDL to generate DeckLinkAPI_h.h and
    # DeckLinkAPI_i.c in the build directory, then compile the _i.c file.
    set(DECKLINK_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/decklink_generated")
    file(MAKE_DIRECTORY "${DECKLINK_GENERATED_DIR}")
    add_custom_command(
      OUTPUT
        "${DECKLINK_GENERATED_DIR}/DeckLinkAPI_h.h"
        "${DECKLINK_GENERATED_DIR}/DeckLinkAPI_i.c"
      COMMAND midl
        /h "DeckLinkAPI_h.h"
        /iid "DeckLinkAPI_i.c"
        /proxy "DeckLinkAPI_p.c"
        /tlb "DeckLinkAPI.tlb"
        /out "${DECKLINK_GENERATED_DIR}"
        "${DECKLINK_IDL_DIR}/DeckLinkAPI.idl"
      DEPENDS "${DECKLINK_IDL_DIR}/DeckLinkAPI.idl"
      COMMENT "Compiling DeckLink IDL (MIDL)"
      VERBATIM
    )
    add_custom_target(deckboy_decklink_idl DEPENDS
      "${DECKLINK_GENERATED_DIR}/DeckLinkAPI_h.h"
      "${DECKLINK_GENERATED_DIR}/DeckLinkAPI_i.c"
    )
    add_dependencies(deckboy-native deckboy_decklink_idl)
    target_sources(deckboy-native PRIVATE "${DECKLINK_GENERATED_DIR}/DeckLinkAPI_i.c")
    set_source_files_properties("${DECKLINK_GENERATED_DIR}/DeckLinkAPI_i.c"
      PROPERTIES
        GENERATED TRUE
        LANGUAGE C
    )
  else()
    # On Linux/macOS, compile the SDK dispatch source (provides CreateDeckLinkIteratorInstance)
    target_sources(deckboy-native PRIVATE "${DECKLINK_INCLUDE_DIR}/DeckLinkAPIDispatch.cpp")
  endif()
endif()

if(ENABLE_SIPHON OR ENABLE_SPOUT)
  target_sources(deckboy-native PRIVATE native/platform/siphon_spout.cpp)
endif()

if(DECKBOY_INPROC_DECODE_ACTIVE)
  target_sources(deckboy-native PRIVATE native/engine/libav_decoder.cpp)
  target_include_directories(deckboy-native PRIVATE ${DECKBOY_AVCODEC_INCLUDE_DIR})
  target_link_libraries(deckboy-native PRIVATE
    ${DECKBOY_AVCODEC_LIBRARY}
    ${DECKBOY_AVFORMAT_LIBRARY}
    ${DECKBOY_AVUTIL_LIBRARY}
    ${DECKBOY_AVFILTER_LIBRARY}
    ${DECKBOY_SWSCALE_LIBRARY}
    ${DECKBOY_SWRESAMPLE_LIBRARY}
  )
  target_compile_definitions(deckboy-native PRIVATE DECKBOY_INPROC_DECODE=1)
endif()

target_include_directories(deckboy-native PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/native
  ${CMAKE_CURRENT_BINARY_DIR}/generated
)

if(NOT DECKBOY_SDL_TARGET)
  target_include_directories(deckboy-native PRIVATE ${DECKBOY_SDL_INCLUDE_DIRS})
endif()

if(NOT DECKBOY_SDL_TTF_TARGET)
  target_include_directories(deckboy-native PRIVATE ${DECKBOY_SDL_TTF_INCLUDE_DIRS})
endif()

# Pro Features includes and defines
if(ENABLE_DECKLINK)
  if(WIN32)
    # Include the MIDL-generated headers directory
    target_include_directories(deckboy-native PRIVATE "${DECKLINK_GENERATED_DIR}")
  else()
    # Include the SDK headers directory (contains DeckLinkAPI.h + LinuxCOM.h)
    target_include_directories(deckboy-native PRIVATE "${DECKLINK_INCLUDE_DIR}")
  endif()
  target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_DECKLINK=1)
endif()

if(ENABLE_MIDI)
  if(rtmidi_FOUND)
    target_link_libraries(deckboy-native PRIVATE RtMidi::rtmidi)
  elseif(RTMIDI_FOUND)
    target_include_directories(deckboy-native PRIVATE ${RTMIDI_INCLUDE_DIRS})
    target_link_libraries(deckboy-native PRIVATE ${RTMIDI_LIBRARIES})
  elseif(RTMIDI_INCLUDE_DIR AND RTMIDI_LIBRARY)
    target_include_directories(deckboy-native PRIVATE ${RTMIDI_INCLUDE_DIR})
    target_link_libraries(deckboy-native PRIVATE ${RTMIDI_LIBRARY})
  endif()
  target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_MIDI=1)
endif()

if(ENABLE_SIPHON AND APPLE)
  # The Objective-C++ publisher and the frameworks it needs. Only pulled in with
  # ENABLE_SIPHON so a default macOS build stays free of Metal/Syphon.
  target_sources(deckboy-native PRIVATE native/platform/syphon_metal.mm)
  deckboy_target_link_frameworks(deckboy-native Syphon Metal Foundation)
  target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_SIPHON=1)
endif()

if(ENABLE_SPOUT AND WIN32)
  if(Spout2_FOUND)
    target_link_libraries(deckboy-native PRIVATE Spout2::SpoutLibrary)
  else()
    target_include_directories(deckboy-native PRIVATE ${SPOUT_INCLUDE_DIR})
  endif()
  target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_SPOUT=1)
endif()

if(ENABLE_CEF)
  target_include_directories(deckboy-native PRIVATE ${CEF_INCLUDE_DIR})
  target_link_libraries(deckboy-native PRIVATE ${CEF_DLL_WRAPPER} ${CEF_LIBRARY})
  # DECKBOY_HAS_CEF means "libcef is linked into this binary". It does NOT yet
  # mean browser cues run on CEF — that is gated separately at runtime until the
  # off-screen backend lands (docs/BROWSER_CEF_PLAN.md, phase 3).
  target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_CEF=1)
endif()

if(ENABLE_WEBVIEW)
  if(APPLE)
    deckboy_target_link_frameworks(deckboy-native WebKit)
    target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_WEBVIEW=1)
  elseif(WIN32)
    find_package(unofficial-webview2 CONFIG REQUIRED)
    target_link_libraries(deckboy-native PRIVATE unofficial::webview2::webview2 dwmapi)
    target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_WEBVIEW=1)
    message(STATUS "WebView2 support enabled")
  endif()
elseif(WIN32)
  # Auto-detect WebView2 even when ENABLE_WEBVIEW is OFF
  find_package(unofficial-webview2 CONFIG QUIET)
  if(unofficial-webview2_FOUND)
    target_link_libraries(deckboy-native PRIVATE unofficial::webview2::webview2 dwmapi)
    target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_WEBVIEW=1)
    message(STATUS "WebView2 auto-detected and enabled")
  endif()
endif()

if(DECKBOY_ENABLE_NDI AND DECKBOY_NDI_INCLUDE_DIR)
  target_include_directories(deckboy-native PRIVATE
    ${DECKBOY_NDI_INCLUDE_DIR}
  )
  target_compile_definitions(deckboy-native PRIVATE
    DECKBOY_HAS_NDI_SDK=1
  )
endif()

if(UNIX AND NOT APPLE AND DECKBOY_ENABLE_ALSA_MTC AND DECKBOY_ALSA_INCLUDE_DIR AND DECKBOY_ALSA_LIBRARY)
  target_include_directories(deckboy-native PRIVATE
    ${DECKBOY_ALSA_INCLUDE_DIR}
  )
  target_link_libraries(deckboy-native PRIVATE
    ${DECKBOY_ALSA_LIBRARY}
  )
  target_compile_definitions(deckboy-native PRIVATE
    DECKBOY_HAS_ALSA=1
  )
endif()

target_compile_options(deckboy-native PRIVATE
  ${DECKBOY_SDL_COMPILE_OPTIONS}
)

if(DECKBOY_SDL_LINK_DIRS)
  target_link_directories(deckboy-native PRIVATE
    ${DECKBOY_SDL_LINK_DIRS}
  )
endif()

if(DECKBOY_SDL_TARGET)
  target_link_libraries(deckboy-native PRIVATE ${DECKBOY_SDL_TARGET})
else()
  target_link_libraries(deckboy-native PRIVATE ${DECKBOY_SDL_LINK_LIBS})
endif()

if(DECKBOY_SDL_TTF_TARGET)
  target_link_libraries(deckboy-native PRIVATE ${DECKBOY_SDL_TTF_TARGET})
else()
  target_link_libraries(deckboy-native PRIVATE ${DECKBOY_SDL_TTF_LINK_LIBS})
endif()

# The bundled `terrarium` companion exe was RETIRED in v0.81.5.
#
# It existed so the Konami egg could launch a windowed sim and capture it via
# gdigrab. That has been unnecessary since v0.78.4, when the sim moved
# in-process as pattern://terrarium — nothing has needed the exe since. It also
# built against the old forked copy of the sim, which is exactly the file this
# release replaced with a clean vendored upstream, so keeping it would have
# meant maintaining two divergent copies of Terrarium inside Deckboy.
#
# The real standalone app lives in its own repo (Utopian-Academy/terrarium).

if(UNIX AND NOT APPLE)
  target_link_libraries(deckboy-native PRIVATE dl)
endif()

target_compile_definitions(deckboy-native PRIVATE
  $<$<CONFIG:Debug>:DECKBOY_DEBUG=1>
  $<$<BOOL:${WIN32}>:NOMINMAX>
  $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>
  $<$<BOOL:${WIN32}>:SDL_MAIN_HANDLED>
)

if(MSVC)
  target_compile_options(deckboy-native PRIVATE /bigobj)
endif()

if(WIN32)
  target_link_libraries(deckboy-native PRIVATE ws2_32 shell32)
endif()
