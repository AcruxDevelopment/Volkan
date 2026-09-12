# =============================================================================
# NinjaPin.cmake
#
# Pure computation of "where does the pinned ninja live, and where would it
# be downloaded from" -- no network calls, no download, no extraction.
# Mirrors DoxygenPin.cmake exactly; see that file for the full rationale.
# Shared by two very different callers:
#
#   scripts/build.sh / build.bat  include() this (via `cmake -P`, same
#                                 mechanism as FetchNinja.cmake below) to
#                                 find out where to LOOK for an
#                                 already-downloaded pinned ninja, before
#                                 CMake configure even runs -- ninja is
#                                 needed as the generator itself, unlike
#                                 Doxygen, which is only ever needed for a
#                                 downstream custom target. Never
#                                 downloads anything itself.
#
#   cmake/FetchNinja.cmake        include()s this when run standalone via
#                                 `cmake -P cmake/FetchNinja.cmake` (what
#                                 scripts/setup.sh / setup.bat call) to
#                                 know what to actually download.
#
# Needs TOOLCACHE_DIR to already be set. Both callers above set it manually
# (matching Configuration.cmake's default) since neither runs through
# CMakeLists.txt.
#
# Bump NINJA_PINNED_VERSION to change the pinned version project-wide --
# doing so also requires updating the two SHA256 hashes below to match the
# new release's assets (a stale hash fails loud and hard, on purpose, in
# FetchNinja.cmake rather than silently accepting an unexpected file).
# =============================================================================

include_guard(GLOBAL)

if(NOT DEFINED NINJA_PINNED_VERSION)
    set(NINJA_PINNED_VERSION "1.13.2")
endif()

set(NINJA_PIN_TAG "v${NINJA_PINNED_VERSION}")
set(NINJA_PIN_ROOT "${TOOLCACHE_DIR}/ninja-${NINJA_PINNED_VERSION}")

if(CMAKE_HOST_WIN32)
    set(NINJA_PIN_URL "https://github.com/ninja-build/ninja/releases/download/${NINJA_PIN_TAG}/ninja-win.zip")
    set(NINJA_PIN_HASH "07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65")
    set(NINJA_PIN_EXECUTABLE "${NINJA_PIN_ROOT}/ninja.exe")
    set(NINJA_PIN_PLATFORM_SUPPORTED TRUE)
elseif(CMAKE_HOST_UNIX AND NOT CMAKE_HOST_APPLE)
    set(NINJA_PIN_URL "https://github.com/ninja-build/ninja/releases/download/${NINJA_PIN_TAG}/ninja-linux.zip")
    set(NINJA_PIN_HASH "5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6")
    set(NINJA_PIN_EXECUTABLE "${NINJA_PIN_ROOT}/ninja")
    set(NINJA_PIN_PLATFORM_SUPPORTED TRUE)
else()
    set(NINJA_PIN_URL "")
    set(NINJA_PIN_HASH "")
    set(NINJA_PIN_EXECUTABLE "")
    set(NINJA_PIN_PLATFORM_SUPPORTED FALSE)
endif()
