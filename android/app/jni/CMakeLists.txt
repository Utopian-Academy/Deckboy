cmake_minimum_required(VERSION 3.22)
project(deckboy_pocket LANGUAGES C CXX)

# Sources that are not in the repository (SDL, SDL_ttf, the FFmpeg build) live
# under POCKET_DEPS (default X:/android); see android/README.md.
# Every library 16 KB-page ready (Android 15+ phones with 16 KB pages refuse
# the rest): SDL and SDL_ttf are built here too, not only Pocket's own.
string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,-z,max-page-size=16384")
set(POCKET_DEPS "X:/android" CACHE PATH "Where SDL's source and the Android FFmpeg build are")
add_subdirectory("${POCKET_DEPS}/src/SDL" SDL)
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${POCKET_DEPS}/src/SDL_ttf" SDL_ttf)

# FFmpeg built for this ABI (android/build_ffmpeg_android.sh). When it is not
# there yet, Pocket builds without video and plays Deckboy's generators.
set(_ffmpeg "${POCKET_DEPS}/prefix/${ANDROID_ABI}")
if(EXISTS "${_ffmpeg}/include/libavcodec/avcodec.h")
  list(APPEND CMAKE_FIND_ROOT_PATH "${_ffmpeg}")
  set(DECKBOY_INPROC_DECODE ON CACHE BOOL "" FORCE)
  set(DECKBOY_REQUIRE_INPROC_DECODE ON CACHE BOOL "" FORCE)
else()
  message(WARNING "Pocket: no FFmpeg at ${_ffmpeg}; building without video")
  set(DECKBOY_INPROC_DECODE OFF CACHE BOOL "" FORCE)
endif()

# Deckboy itself, with what a phone cannot have switched off. The desk target
# is configured but never built: Gradle asks for "main" only.
foreach(_off DECKBOY_BUILD_MINI BUILD_TESTING DECKBOY_ENABLE_NDI DECKBOY_ENABLE_ALSA_MTC
             ENABLE_DECKLINK ENABLE_ASIO ENABLE_VST3 ENABLE_LINK ENABLE_MIDI ENABLE_CEF
             ENABLE_WEBVIEW ENABLE_SPOUT ENABLE_SIPHON DECKBOY_ISF)
  set(${_off} OFF CACHE BOOL "" FORCE)
endforeach()
set(DECKBOY_BUILD_POCKET ON CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../.." deckboy)
