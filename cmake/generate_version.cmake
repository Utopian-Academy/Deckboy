# generate_version.cmake — run at build time via add_custom_command
# Reads SOURCE_DIR/VERSION, substitutes into version.hpp.in, writes OUTPUT_FILE.
# Variables passed in: SOURCE_DIR, OUTPUT_FILE

file(READ "${SOURCE_DIR}/VERSION" DECKBOY_VERSION_RAW)
string(STRIP "${DECKBOY_VERSION_RAW}" DECKBOY_VERSION_STRING)
if(NOT DECKBOY_VERSION_STRING MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.-]+)?$")
  message(FATAL_ERROR "VERSION must use SemVer: MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-prerelease. Got: ${DECKBOY_VERSION_STRING}")
endif()
set(DECKBOY_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(DECKBOY_VERSION_MINOR "${CMAKE_MATCH_2}")
set(DECKBOY_VERSION_PATCH "${CMAKE_MATCH_3}")
set(DECKBOY_VERSION_PRERELEASE "${CMAKE_MATCH_4}")
set(DECKBOY_VERSION_TAG "v${DECKBOY_VERSION_STRING}")

configure_file(
  "${SOURCE_DIR}/native/core/version.hpp.in"
  "${OUTPUT_FILE}"
  @ONLY
)
