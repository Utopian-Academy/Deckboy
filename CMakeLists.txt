cmake_minimum_required(VERSION 3.20)

project(playboy_native VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

find_path(SDL2_TTF_INCLUDE_DIR
  NAMES SDL_ttf.h
  PATH_SUFFIXES SDL2
)
find_library(SDL2_TTF_LIBRARY
  NAMES SDL2_ttf
)

if(NOT SDL2_TTF_INCLUDE_DIR OR NOT SDL2_TTF_LIBRARY)
  message(FATAL_ERROR "SDL2_ttf development files are required")
endif()

option(PLAYBOY_ENABLE_NDI "Enable optional NDI SDK integration" ON)
if(PLAYBOY_ENABLE_NDI)
  find_path(PLAYBOY_NDI_INCLUDE_DIR
    NAMES Processing.NDI.Lib.h
    PATHS
      "$ENV{PLAYBOY_NDI_SDK}/include"
      "/opt/NDI SDK for Linux/include"
      "/usr/local/include"
      "/usr/include"
  )
endif()

option(PLAYBOY_ENABLE_ALSA_MTC "Enable optional ALSA MTC ingest integration" ON)
if(UNIX AND NOT APPLE AND PLAYBOY_ENABLE_ALSA_MTC)
  find_path(PLAYBOY_ALSA_INCLUDE_DIR
    NAMES asoundlib.h
    PATH_SUFFIXES alsa
    PATHS
      /usr/include
      /usr/local/include
  )
  find_library(PLAYBOY_ALSA_LIBRARY
    NAMES asound
  )
endif()

# Pro Features Feature Gates
option(ENABLE_DECKLINK "Build with Blackmagic DeckLink 10-bit SDI/HDMI output" OFF)
option(ENABLE_MIDI "Build with RtMidi cross-platform MIDI input" OFF)
option(ENABLE_SIPHON "Build with macOS Siphon interprocess texture sharing" OFF)
option(ENABLE_SPOUT "Build with Windows Spout interprocess texture sharing" OFF)
option(ENABLE_CEF "Build with Chromium Embedded Framework browser cues" OFF)
option(ENABLE_WEBVIEW "Build with native WebView APIs (macOS WKWebView, Windows WebView2)" OFF)

# DeckLink SDK
if(ENABLE_DECKLINK)
  find_path(DECKLINK_INCLUDE_DIR
    NAMES DeckLinkAPI.h
    PATHS
      "$ENV{DECKLINK_SDK}/include"
      "/opt/Blackmagic DeskLink SDK"
      "/usr/local/include"
      "/usr/include"
  )
  if(NOT DECKLINK_INCLUDE_DIR)
    message(FATAL_ERROR "DeckLink SDK not found. Set DECKLINK_SDK env var or disable ENABLE_DECKLINK")
  endif()
  message(STATUS "DeckLink support enabled: ${DECKLINK_INCLUDE_DIR}")
endif()

# RtMidi
if(ENABLE_MIDI)
  pkg_check_modules(RTMIDI rtmidi QUIET)
  
  if(NOT RTMIDI_FOUND)
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
  
  if(RTMIDI_FOUND OR (RTMIDI_INCLUDE_DIR AND RTMIDI_LIBRARY))
    if(RTMIDI_FOUND)
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

# Siphon (macOS)
if(ENABLE_SIPHON AND APPLE)
  find_library(SIPHON_FRAMEWORK Siphon)
  if(NOT SIPHON_FRAMEWORK)
    message(FATAL_ERROR "Siphon framework not found on macOS. Disable ENABLE_SIPHON or install Siphon")
  endif()
  message(STATUS "Siphon support enabled")
endif()

# Spout (Windows)
if(ENABLE_SPOUT AND WIN32)
  find_path(SPOUT_INCLUDE_DIR
    NAMES SpoutLibrary.h
    PATHS
      "$ENV{SPOUT_SDK}/include"
      "C:/Spout/include"
  )
  if(NOT SPOUT_INCLUDE_DIR)
    message(FATAL_ERROR "Spout SDK not found on Windows. Disable ENABLE_SPOUT or set SPOUT_SDK env var")
  endif()
  message(STATUS "Spout support enabled")
endif()

# CEF (Chromium Embedded Framework)
if(ENABLE_CEF)
  find_path(CEF_INCLUDE_DIR
    NAMES cef_api_hash.h
    PATHS
      "$ENV{CEF_SDK}/include"
      "/opt/cef"
  )
  if(NOT CEF_INCLUDE_DIR)
    message(FATAL_ERROR "CEF SDK not found. Disable ENABLE_CEF or set CEF_SDK env var")
  endif()
  message(STATUS "CEF support enabled")
endif()

# WebView (macOS/Windows native)
if(ENABLE_WEBVIEW AND NOT APPLE AND NOT WIN32)
  message(FATAL_ERROR "ENABLE_WEBVIEW is only supported on macOS and Windows")
endif()

add_executable(playboy-native
  native/main.cpp
  native/core/paths.cpp
  native/core/utils.cpp
  native/core/subprocess.cpp
  native/render/primitives.cpp
  native/render/text_renderer.cpp
  native/render/waveform_renderer.cpp
  native/render/output_renderer.cpp
)

# Platform modules (conditional)
if(ENABLE_MIDI)
  target_sources(playboy-native PRIVATE native/platform/midi.cpp)
endif()

if(ENABLE_DECKLINK)
  target_sources(playboy-native PRIVATE native/platform/decklink.cpp)
endif()

if(ENABLE_SIPHON OR ENABLE_SPOUT)
  target_sources(playboy-native PRIVATE native/platform/siphon_spout.cpp)
endif()

if(ENABLE_CEF OR ENABLE_WEBVIEW)
  target_sources(playboy-native PRIVATE native/platform/browser.cpp)
endif()

target_include_directories(playboy-native PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/native
  ${SDL2_INCLUDE_DIRS}
  ${SDL2_TTF_INCLUDE_DIR}
)

# Pro Features includes and defines
if(ENABLE_DECKLINK)
  target_include_directories(playboy-native PRIVATE ${DECKLINK_INCLUDE_DIR})
  target_compile_definitions(playboy-native PRIVATE PLAYBOY_HAS_DECKLINK=1)
endif()

if(ENABLE_MIDI)
  if(RTMIDI_FOUND)
    target_include_directories(playboy-native PRIVATE ${RTMIDI_INCLUDE_DIRS})
    target_link_libraries(playboy-native PRIVATE ${RTMIDI_LIBRARIES})
  elseif(RTMIDI_INCLUDE_DIR AND RTMIDI_LIBRARY)
    target_include_directories(playboy-native PRIVATE ${RTMIDI_INCLUDE_DIR})
    target_link_libraries(playboy-native PRIVATE ${RTMIDI_LIBRARY})
  endif()
  target_compile_definitions(playboy-native PRIVATE PLAYBOY_HAS_MIDI=1)
endif()

if(ENABLE_SIPHON AND APPLE)
  target_link_frameworks(playboy-native PRIVATE Siphon)
  target_compile_definitions(playboy-native PRIVATE PLAYBOY_HAS_SIPHON=1)
endif()

if(ENABLE_SPOUT AND WIN32)
  target_include_directories(playboy-native PRIVATE ${SPOUT_INCLUDE_DIR})
  target_compile_definitions(playboy-native PRIVATE PLAYBOY_HAS_SPOUT=1)
endif()

if(ENABLE_CEF)
  target_include_directories(playboy-native PRIVATE ${CEF_INCLUDE_DIR})
  target_compile_definitions(playboy-native PRIVATE PLAYBOY_HAS_CEF=1)
endif()

if(ENABLE_WEBVIEW)
  if(APPLE)
    target_link_frameworks(playboy-native PRIVATE WebKit)
    target_compile_definitions(playboy-native PRIVATE PLAYBOY_HAS_WEBVIEW=1)
  elseif(WIN32)
    target_compile_definitions(playboy-native PRIVATE PLAYBOY_HAS_WEBVIEW=1)
  endif()
endif()

if(PLAYBOY_ENABLE_NDI AND PLAYBOY_NDI_INCLUDE_DIR)
  target_include_directories(playboy-native PRIVATE
    ${PLAYBOY_NDI_INCLUDE_DIR}
  )
  target_compile_definitions(playboy-native PRIVATE
    PLAYBOY_HAS_NDI_SDK=1
  )
endif()

if(UNIX AND NOT APPLE AND PLAYBOY_ENABLE_ALSA_MTC AND PLAYBOY_ALSA_INCLUDE_DIR AND PLAYBOY_ALSA_LIBRARY)
  target_include_directories(playboy-native PRIVATE
    ${PLAYBOY_ALSA_INCLUDE_DIR}
  )
  target_link_libraries(playboy-native PRIVATE
    ${PLAYBOY_ALSA_LIBRARY}
  )
  target_compile_definitions(playboy-native PRIVATE
    PLAYBOY_HAS_ALSA=1
  )
endif()

target_compile_options(playboy-native PRIVATE
  ${SDL2_CFLAGS_OTHER}
)

target_link_directories(playboy-native PRIVATE
  ${SDL2_LIBRARY_DIRS}
)

target_link_libraries(playboy-native PRIVATE
  ${SDL2_LIBRARIES}
  ${SDL2_TTF_LIBRARY}
)

if(UNIX AND NOT APPLE)
  target_link_libraries(playboy-native PRIVATE dl)
endif()

target_compile_definitions(playboy-native PRIVATE
  $<$<CONFIG:Debug>:PLAYBOY_DEBUG=1>
)
