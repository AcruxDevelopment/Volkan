#!/usr/bin/env bash
# One-time (or re-run any time) environment setup for this template:
#
#   1. Fetches the pinned ninja (cmake/FetchNinja.cmake) -- used by
#      scripts/build.sh / build.bat if found (see there); a system ninja
#      still works fine too, this just avoids needing one installed at all.
#   2. Fetches the pinned Doxygen (cmake/FetchDoxygen.cmake) -- NOT
#      required to build the project itself (see cmake/Documentation.cmake),
#      only to run scripts/docs.sh / docs.bat afterward.
#   3. Checks for clang-format/clang-tidy (used by scripts/refactor.sh and
#      .githooks/pre-commit) and prints install guidance if either is
#      missing -- these aren't auto-installed, since doing so would need
#      assumptions about your package manager and elevated privileges
#      this script shouldn't assume it has.
#   4. Activates the pre-commit hook (.githooks/pre-commit) for this
#      clone, if it's a git repository.
#
# Usage (from the project root):
#   scripts/setup.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

command -v cmake >/dev/null 2>&1 || { echo "error: cmake not found on PATH." >&2; exit 1; }

echo "==> Fetching pinned ninja (cmake/FetchNinja.cmake)"
cmake -P cmake/FetchNinja.cmake

echo
echo "==> Fetching pinned Doxygen (cmake/FetchDoxygen.cmake)"
cmake -P cmake/FetchDoxygen.cmake

echo
echo "==> Checking for clang-format / clang-tidy"
_missing=0
if ! command -v clang-format >/dev/null 2>&1; then
    echo "    clang-format: not found"
    _missing=1
else
    echo "    clang-format: $(command -v clang-format) ($(clang-format --version))"
fi
if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "    clang-tidy:   not found"
    _missing=1
else
    echo "    clang-tidy:   $(command -v clang-tidy) ($(clang-tidy --version | head -n1))"
fi

if [ "$_missing" -eq 1 ]; then
    echo
    echo "    Install the missing tool(s) with:"
    case "$(uname -s)" in
        Darwin)
            echo "      brew install clang-format llvm"
            ;;
        *)
            echo "      sudo apt-get install clang-format clang-tidy   # Debian/Ubuntu"
            echo "      (or your distro's equivalent package)"
            ;;
    esac
    echo "    Not required to build this project -- only for scripts/refactor.sh"
    echo "    and the pre-commit hook."
fi

echo
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "==> Activating pre-commit hook"
    chmod +x .githooks/pre-commit
    git config core.hooksPath .githooks
    echo "    core.hooksPath = .githooks"
else
    echo "==> Skipping pre-commit hook activation (not a git repository yet)"
    echo "    Once you've run 'git init', re-run this script or:"
    echo "      chmod +x .githooks/pre-commit && git config core.hooksPath .githooks"
fi

echo
echo "==> Setup complete. scripts/build.sh and scripts/docs.sh are ready to use."
