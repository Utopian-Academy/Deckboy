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

add_executable(playboy-native
  native/main.cpp
  native/core/paths.cpp
)

target_include_directories(playboy-native PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/native
  ${SDL2_INCLUDE_DIRS}
  ${SDL2_TTF_INCLUDE_DIR}
)

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
