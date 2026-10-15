# deckboy-listen: the live-captions speech engine (see deckboy_listen.cpp).
# Built beside whisper-cli from the same whisper.cpp checkout:
#   cmake -S tools/listen -B <dir> -DWHISPER_SOURCE=<whisper.cpp> -DCMAKE_BUILD_TYPE=Release
#   cmake --build <dir> --target deckboy-listen
cmake_minimum_required(VERSION 3.20)
project(deckboy_listen CXX C)

set(WHISPER_SOURCE "" CACHE PATH "a whisper.cpp source checkout")
if(NOT EXISTS "${WHISPER_SOURCE}/CMakeLists.txt")
  message(FATAL_ERROR "set WHISPER_SOURCE to a whisper.cpp checkout")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
add_subdirectory("${WHISPER_SOURCE}" whisper EXCLUDE_FROM_ALL)

add_executable(deckboy-listen deckboy_listen.cpp)
target_compile_features(deckboy-listen PRIVATE cxx_std_17)
target_link_libraries(deckboy-listen PRIVATE whisper)
