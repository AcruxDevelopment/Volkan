#!/usr/bin/env bash
# Rebuilds hotreload_live_plugin every time LivePlugin.cpp's mtime
# changes, so you can just edit-and-save instead of switching to a
# terminal to rebuild by hand. Run this in one terminal; run
# hotreload_live_demo_host (built once) in another; edit
# Plugin/src/LivePlugin.cpp in your editor. Matches this project's
# other scripts/*.sh conventions (see scripts/run.sh) even though it
# lives here rather than in scripts/, since it's specific to this one
# example rather than general project tooling.
#
# Usage (from anywhere):
#   source/examples/HotReloadLiveDemo/watch.sh [build-dir]
#
# build-dir defaults to out/build (relative to the project root, same
# default as scripts/build.sh); pass your own if you configured
# somewhere else, e.g.:
#   source/examples/HotReloadLiveDemo/watch.sh out/build-clang
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/../../.."

BUILD_DIR="${1:-out/build}"
SOURCE_FILE="$SCRIPT_DIR/Plugin/src/LivePlugin.cpp"
TARGET="hotreload_live_plugin"

if [ ! -d "$BUILD_DIR" ]; then
	echo "error: build directory '$BUILD_DIR' not found." >&2
	echo "Configure it first, e.g.:" >&2
	echo "  cmake -S . -B $BUILD_DIR -DBUILD_EXAMPLES=ON" >&2
	exit 1
fi

file_mtime() {
	# GNU stat (Linux) and BSD/macOS stat use incompatible flags --
	# try one, fall back to the other.
	stat -c %Y "$SOURCE_FILE" 2>/dev/null || stat -f %m "$SOURCE_FILE"
}

echo "Watching: $SOURCE_FILE"
echo "Rebuilds into: $BUILD_DIR (target: $TARGET)"
echo "Edit and save that file -- Ctrl+C here to stop watching (the running Host keeps going either way)."
echo ""

last="$(file_mtime)"
while true; do
	current="$(file_mtime)"
	if [ "$current" != "$last" ]; then
		last="$current"
		echo "---- rebuilding ($(date '+%H:%M:%S')) ----"
		if cmake --build "$BUILD_DIR" --target "$TARGET"; then
			echo "---- done -- the running Host should pick this up within about a second ----"
		else
			echo "---- build failed -- fix the error above and save again ----"
		fi
		echo ""
	fi
	sleep 0.5
done
