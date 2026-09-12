# =============================================================================
# DoxygenPin.cmake
#
# Pure computation of "where does the pinned Doxygen live, and where would
# it be downloaded from" -- no network calls, no download, no extraction.
# Shared by two very different callers, which is why this is its own file:
#
#   cmake/Documentation.cmake  include()s this during the normal CMake
#                              configure to find out where to LOOK for an
#                              already-downloaded pinned Doxygen. Never
#                              downloads anything itself -- see that file.
#
#   cmake/FetchDoxygen.cmake   include()s this when run standalone via
#                              `cmake -P cmake/FetchDoxygen.cmake` (what
#                              scripts/setup.sh / setup.bat call) to know
#                              what to actually download.
#
# Needs TOOLCACHE_DIR to already be set -- Documentation.cmake gets it from
# Configuration.cmake (included earlier by CMakeLists.txt); FetchDoxygen.cmake
# sets it manually since script mode (`cmake -P`) never runs CMakeLists.txt.
#
# Bump DOXYGEN_PINNED_VERSION to change the pinned version project-wide --
# doing so also requires updating the two SHA256 hashes below to match the
# new release's assets (a stale hash fails loud and hard, on purpose, in
# FetchDoxygen.cmake rather than silently accepting an unexpected file).
# =============================================================================

include_guard(GLOBAL)

if(NOT DEFINED DOXYGEN_PINNED_VERSION)
    set(DOXYGEN_PINNED_VERSION "1.16.1")
endif()

string(REPLACE "." "_" _pmn_doxygen_tag_version "${DOXYGEN_PINNED_VERSION}")
set(DOXYGEN_PIN_TAG "Release_${_pmn_doxygen_tag_version}")
set(DOXYGEN_PIN_ROOT "${TOOLCACHE_DIR}/doxygen-${DOXYGEN_PINNED_VERSION}")

if(CMAKE_HOST_WIN32)
    set(DOXYGEN_PIN_URL "https://github.com/doxygen/doxygen/releases/download/${DOXYGEN_PIN_TAG}/doxygen-${DOXYGEN_PINNED_VERSION}.windows.x64.bin.zip")
    set(DOXYGEN_PIN_HASH "ddfa59a4ae9549651330471c2e387ec7a9891080543faed541c859e5ce448653")
    set(DOXYGEN_PIN_EXECUTABLE "${DOXYGEN_PIN_ROOT}/doxygen.exe")
    set(DOXYGEN_PIN_PLATFORM_SUPPORTED TRUE)
elseif(CMAKE_HOST_UNIX AND NOT CMAKE_HOST_APPLE)
    set(DOXYGEN_PIN_URL "https://github.com/doxygen/doxygen/releases/download/${DOXYGEN_PIN_TAG}/doxygen-${DOXYGEN_PINNED_VERSION}.linux.bin.tar.gz")
    set(DOXYGEN_PIN_HASH "a56f885d37e3aae08a99f638d17bbb381224c03a878d9e2dda4f9fa4baf1d8bd")
    set(DOXYGEN_PIN_EXECUTABLE "${DOXYGEN_PIN_ROOT}/doxygen-${DOXYGEN_PINNED_VERSION}/bin/doxygen")
    set(DOXYGEN_PIN_PLATFORM_SUPPORTED TRUE)
else()
    set(DOXYGEN_PIN_URL "")
    set(DOXYGEN_PIN_HASH "")
    set(DOXYGEN_PIN_EXECUTABLE "")
    set(DOXYGEN_PIN_PLATFORM_SUPPORTED FALSE)
endif()
