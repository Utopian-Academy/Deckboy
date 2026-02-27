cmake_minimum_required(VERSION 3.20)

project(playboy_native VERSION 0.0.1 LANGUAGES CXX)

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

add_executable(playboy-native
  native/main.cpp
)

target_include_directories(playboy-native PRIVATE
  ${SDL2_INCLUDE_DIRS}
  ${SDL2_TTF_INCLUDE_DIR}
)

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

target_compile_definitions(playboy-native PRIVATE
  $<$<CONFIG:Debug>:PLAYBOY_DEBUG=1>
)
