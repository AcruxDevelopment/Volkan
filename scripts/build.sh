#!/usr/bin/env bash
# Configure (if needed) and build the project, preferably with Ninja.
#
# Usage (from the project root):
#   scripts/build.sh [BUILD_TYPE]
#
#   BUILD_TYPE  Debug|Release|RelWithDebInfo|MinSizeRel (default: Release)
#
# Set BUILD_DIR to build somewhere other than out/build (see Configuration.cmake).
set -euo pipefail
shopt -s nullglob

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

BUILD_DIR="${BUILD_DIR:-out/build}"
BUILD_TYPE="${1:-Release}"

command -v cmake >/dev/null 2>&1 || { echo "error: cmake not found on PATH." >&2; exit 1; }

# Prefer a pinned ninja (fetched by scripts/setup.sh -- see
# cmake/FetchNinja.cmake) over a system one, same "always the pinned copy"
# preference as Doxygen. TOOLCACHE_DIR default here matches
# Configuration.cmake's; if you've overridden TOOLCACHE_DIR at configure
# time, also set it here as an env var so this still finds it.
TOOLCACHE_DIR="${TOOLCACHE_DIR:-.cache/tools}"
NINJA_BIN=""
for _candidate in "${TOOLCACHE_DIR}"/ninja-*/ninja; do
    if [ -x "$_candidate" ]; then
        # Must be absolute: CMake invokes CMAKE_MAKE_PROGRAM from several
        # different working directories during its own internal
        # try-compile steps, where a relative path doesn't resolve.
        NINJA_BIN="$(cd "$(dirname "$_candidate")" && pwd)/$(basename "$_candidate")"
        break
    fi
done

GENERATOR_ARGS=()
if [ -n "$NINJA_BIN" ]; then
    echo "==> Using pinned ninja: ${NINJA_BIN}"
    GENERATOR_ARGS=(-G Ninja "-DCMAKE_MAKE_PROGRAM=${NINJA_BIN}")
elif command -v ninja >/dev/null 2>&1; then
    echo "==> Using system ninja: $(command -v ninja)"
    GENERATOR_ARGS=(-G Ninja)
else
    echo "warning: ninja not found (no pinned copy, none on PATH) -- falling back to" >&2
    echo "         CMake's default generator for this platform. Run scripts/setup.sh" >&2
    echo "         to fetch a pinned ninja, or install ninja yourself, for a faster" >&2
    echo "         and more consistent build. Continuing without it." >&2
    GENERATOR_ARGS=()
fi

echo "==> Configuring (${BUILD_TYPE}) into ${BUILD_DIR}/"
cmake "${GENERATOR_ARGS[@]}" -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

echo "==> Building"
cmake --build "${BUILD_DIR}"

echo "==> Done. Run a module with:  scripts/run.sh <target-name>"
