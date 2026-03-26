cmake_minimum_required(VERSION 3.20)

project(deckboy_native VERSION 0.3.0 LANGUAGES CXX)

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

set(DECKBOY_SDL2_TARGET "")
set(DECKBOY_SDL2_TTF_TARGET "")
set(DECKBOY_SDL2_INCLUDE_DIRS "")
set(DECKBOY_SDL2_TTF_INCLUDE_DIRS "")
set(DECKBOY_SDL2_LINK_LIBS "")
set(DECKBOY_SDL2_TTF_LINK_LIBS "")
set(DECKBOY_SDL_COMPILE_OPTIONS "")
set(DECKBOY_SDL_LINK_DIRS "")

find_package(SDL2 QUIET CONFIG)
find_package(SDL2_ttf QUIET CONFIG)

if(TARGET SDL2::SDL2)
  set(DECKBOY_SDL2_TARGET SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
  set(DECKBOY_SDL2_TARGET SDL2::SDL2-static)
endif()

if(TARGET SDL2_ttf::SDL2_ttf)
  set(DECKBOY_SDL2_TTF_TARGET SDL2_ttf::SDL2_ttf)
elseif(TARGET SDL2_ttf::SDL2_ttf-static)
  set(DECKBOY_SDL2_TTF_TARGET SDL2_ttf::SDL2_ttf-static)
endif()

if(NOT DECKBOY_SDL2_TARGET OR NOT DECKBOY_SDL2_TTF_TARGET)
  find_package(PkgConfig QUIET)
endif()

if(PkgConfig_FOUND AND NOT DECKBOY_SDL2_TARGET)
  pkg_check_modules(SDL2 QUIET sdl2)
endif()

if(PkgConfig_FOUND AND NOT DECKBOY_SDL2_TTF_TARGET)
  pkg_check_modules(SDL2_TTF QUIET SDL2_ttf)
endif()

if(NOT DECKBOY_SDL2_TARGET)
  if(NOT SDL2_FOUND)
    find_path(SDL2_INCLUDE_DIR
      NAMES SDL.h
      PATH_SUFFIXES SDL2
    )
    find_library(SDL2_LIBRARY
      NAMES SDL2 SDL2-static
    )
  endif()

  set(DECKBOY_SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIRS})
  if(NOT DECKBOY_SDL2_INCLUDE_DIRS AND SDL2_INCLUDE_DIR)
    set(DECKBOY_SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
  endif()

  set(DECKBOY_SDL2_LINK_LIBS ${SDL2_LIBRARIES})
  if(NOT DECKBOY_SDL2_LINK_LIBS AND SDL2_LIBRARY)
    set(DECKBOY_SDL2_LINK_LIBS ${SDL2_LIBRARY})
  endif()

  list(APPEND DECKBOY_SDL_COMPILE_OPTIONS ${SDL2_CFLAGS_OTHER})
  list(APPEND DECKBOY_SDL_LINK_DIRS ${SDL2_LIBRARY_DIRS})

  if(NOT DECKBOY_SDL2_INCLUDE_DIRS OR NOT DECKBOY_SDL2_LINK_LIBS)
    message(FATAL_ERROR "SDL2 development files are required")
  endif()
endif()

if(NOT DECKBOY_SDL2_TTF_TARGET)
  find_path(SDL2_TTF_INCLUDE_DIR
    NAMES SDL_ttf.h
    PATH_SUFFIXES SDL2
  )
  find_library(SDL2_TTF_LIBRARY
    NAMES SDL2_ttf SDL2_ttf-static
  )

  set(DECKBOY_SDL2_TTF_INCLUDE_DIRS ${SDL2_TTF_INCLUDE_DIRS})
  if(NOT DECKBOY_SDL2_TTF_INCLUDE_DIRS AND SDL2_TTF_INCLUDE_DIR)
    set(DECKBOY_SDL2_TTF_INCLUDE_DIRS ${SDL2_TTF_INCLUDE_DIR})
  endif()

  set(DECKBOY_SDL2_TTF_LINK_LIBS ${SDL2_TTF_LIBRARIES})
  if(NOT DECKBOY_SDL2_TTF_LINK_LIBS AND SDL2_TTF_LIBRARY)
    set(DECKBOY_SDL2_TTF_LINK_LIBS ${SDL2_TTF_LIBRARY})
  endif()

  list(APPEND DECKBOY_SDL_COMPILE_OPTIONS ${SDL2_TTF_CFLAGS_OTHER})
  list(APPEND DECKBOY_SDL_LINK_DIRS ${SDL2_TTF_LIBRARY_DIRS})

  if(NOT DECKBOY_SDL2_TTF_INCLUDE_DIRS OR NOT DECKBOY_SDL2_TTF_LINK_LIBS)
    message(FATAL_ERROR "SDL2_ttf development files are required")
  endif()
endif()

option(DECKBOY_ENABLE_NDI "Enable optional NDI SDK integration" ON)
if(DECKBOY_ENABLE_NDI)
  find_path(DECKBOY_NDI_INCLUDE_DIR
    NAMES Processing.NDI.Lib.h
    PATHS
      "$ENV{DECKBOY_NDI_SDK}/include"
      "/opt/NDI SDK for Linux/include"
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

add_executable(deckboy-native
  native/main.cpp
  native/core/paths.cpp
  native/core/utils.cpp
  native/core/subprocess.cpp
  native/engine/media_engine.cpp
  native/platform/capture_backend.cpp
  native/platform/integration_backend.cpp
  native/platform/output_backend.cpp
  native/render/primitives.cpp
  native/render/text_renderer.cpp
  native/render/waveform_renderer.cpp
  native/render/output_renderer.cpp
)

# Platform modules (conditional)
if(ENABLE_MIDI)
  target_sources(deckboy-native PRIVATE native/platform/midi.cpp)
endif()

if(ENABLE_DECKLINK)
  target_sources(deckboy-native PRIVATE native/platform/decklink.cpp)
endif()

if(ENABLE_SIPHON OR ENABLE_SPOUT)
  target_sources(deckboy-native PRIVATE native/platform/siphon_spout.cpp)
endif()

if(ENABLE_CEF OR ENABLE_WEBVIEW)
  target_sources(deckboy-native PRIVATE native/platform/browser.cpp)
endif()

target_include_directories(deckboy-native PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/native
)

if(NOT DECKBOY_SDL2_TARGET)
  target_include_directories(deckboy-native PRIVATE ${DECKBOY_SDL2_INCLUDE_DIRS})
endif()

if(NOT DECKBOY_SDL2_TTF_TARGET)
  target_include_directories(deckboy-native PRIVATE ${DECKBOY_SDL2_TTF_INCLUDE_DIRS})
endif()

# Pro Features includes and defines
if(ENABLE_DECKLINK)
  target_include_directories(deckboy-native PRIVATE ${DECKLINK_INCLUDE_DIR})
  target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_DECKLINK=1)
endif()

if(ENABLE_MIDI)
  if(RTMIDI_FOUND)
    target_include_directories(deckboy-native PRIVATE ${RTMIDI_INCLUDE_DIRS})
    target_link_libraries(deckboy-native PRIVATE ${RTMIDI_LIBRARIES})
  elseif(RTMIDI_INCLUDE_DIR AND RTMIDI_LIBRARY)
    target_include_directories(deckboy-native PRIVATE ${RTMIDI_INCLUDE_DIR})
    target_link_libraries(deckboy-native PRIVATE ${RTMIDI_LIBRARY})
  endif()
  target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_MIDI=1)
endif()

if(ENABLE_SIPHON AND APPLE)
  deckboy_target_link_frameworks(deckboy-native Siphon)
  target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_SIPHON=1)
endif()

if(ENABLE_SPOUT AND WIN32)
  target_include_directories(deckboy-native PRIVATE ${SPOUT_INCLUDE_DIR})
  target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_SPOUT=1)
endif()

if(ENABLE_CEF)
  target_include_directories(deckboy-native PRIVATE ${CEF_INCLUDE_DIR})
  target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_CEF=1)
endif()

if(ENABLE_WEBVIEW)
  if(APPLE)
    deckboy_target_link_frameworks(deckboy-native WebKit)
    target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_WEBVIEW=1)
  elseif(WIN32)
    target_compile_definitions(deckboy-native PRIVATE DECKBOY_HAS_WEBVIEW=1)
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

if(DECKBOY_SDL2_TARGET)
  target_link_libraries(deckboy-native PRIVATE ${DECKBOY_SDL2_TARGET})
else()
  target_link_libraries(deckboy-native PRIVATE ${DECKBOY_SDL2_LINK_LIBS})
endif()

if(DECKBOY_SDL2_TTF_TARGET)
  target_link_libraries(deckboy-native PRIVATE ${DECKBOY_SDL2_TTF_TARGET})
else()
  target_link_libraries(deckboy-native PRIVATE ${DECKBOY_SDL2_TTF_LINK_LIBS})
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(deckboy-native PRIVATE dl)
endif()

target_compile_definitions(deckboy-native PRIVATE
  $<$<CONFIG:Debug>:DECKBOY_DEBUG=1>
)
