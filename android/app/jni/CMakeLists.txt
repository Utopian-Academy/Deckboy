cmake_minimum_required(VERSION 3.22)
project(deckboy_pocket LANGUAGES C CXX)

# Sources that are not in the repository (SDL, the FFmpeg build) live under
# POCKET_DEPS (default X:/android); see android/README.md.
set(POCKET_DEPS "X:/android" CACHE PATH "Where SDL's source and the Android FFmpeg build are")
add_subdirectory("${POCKET_DEPS}/src/SDL" SDL)

set(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
add_library(main SHARED "${REPO}/native/pocket/pocket_main.cpp")
target_compile_features(main PRIVATE cxx_std_20)
target_link_libraries(main PRIVATE SDL3::SDL3)
target_link_options(main PRIVATE "-Wl,-z,max-page-size=16384")
