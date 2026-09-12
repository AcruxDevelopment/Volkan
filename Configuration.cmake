# =============================================================================
# Configuration.cmake
#
# All output-path configuration lives here. Change a value here, or
# override with -D<NAME>=... at configure time, instead of hunting through
# CMakeLists.txt/cmake/*.cmake for a hardcoded path.
#
# Everything this project generates lives under one OUT_DIR:
#
#   out/
#     build/                           CMAKE_BINARY_DIR -- scripts/build.sh
#                                       defaults to configuring here (-B
#                                       out/build); compiled artifacts land
#                                       in out/build/<RUNTIME_OUTPUT_SUBDIR>
#                                       and out/build/<ARCHIVE_OUTPUT_SUBDIR>
#                                       during normal day-to-day builds.
#     install/<DIST_TARGET>/{bin,lib}  what `cmake --install` / scripts/
#                                       scripts/install.sh populates.
#     dist/                            reserved for packaged/archived
#                                       output (e.g. a future CPack
#                                       integration) -- not populated by
#                                       this project yet, but the path is
#                                       configured and ready.
#     docs/<module>/html, docs/index.html   generated Doxygen output
#                                       (cmake/Documentation.cmake).
#
# One thing lives outside OUT_DIR on purpose: .cache/tools/ (TOOLCACHE_DIR)
# caches downloaded, pinned build tools (currently Ninja and Doxygen), kept
# separate so a clean `rm -rf out/` doesn't force a re-download.
#
# .gitignore is kept in sync with the paths below automatically, every
# configure -- see the bottom of this file. This file is the source of
# truth; .gitignore (and anything else that needs these paths) should
# never hardcode a copy of them.
#
# Included by CMakeLists.txt before any output-directory variable is used,
# so every value here is available project-wide.
# =============================================================================

include_guard(GLOBAL)

set(OUT_DIR "${CMAKE_SOURCE_DIR}/out" CACHE PATH
    "Root directory for everything this project generates: build artifacts, install trees, docs, and reserved dist packaging output")

set(DIST_OUTPUT_DIR "${OUT_DIR}/dist" CACHE PATH
    "Reserved for packaged/archived distribution output (e.g. a future CPack integration). Not populated by this project yet.")

set(INSTALL_OUTPUT_DIR "${OUT_DIR}/install" CACHE PATH
    "Root of the cmake --install destination tree, one subdirectory per DIST_TARGET")

set(DOCS_OUTPUT_DIR "${OUT_DIR}/docs" CACHE PATH
    "Root of generated Doxygen documentation, one subdirectory per module")

set(COMPILE_COMMANDS_DESTINATION "${CMAKE_SOURCE_DIR}/compile_commands.json" CACHE FILEPATH
    "Where compile_commands.json is copied to after every build, for clangd/clang-tidy tooling (see CMakeLists.txt)")

# Deliberately NOT under OUT_DIR: this caches downloaded, pinned build
# tools (currently just Doxygen -- see cmake/Documentation.cmake), which
# are machine-local and expensive to re-fetch, not build output. Keeping
# it separate means a clean `rm -rf out/` (a normal "start fresh" gesture)
# doesn't force a re-download.
set(TOOLCACHE_DIR "${CMAKE_SOURCE_DIR}/.cache/tools" CACHE PATH
    "Where pinned build tools (currently Ninja and Doxygen) are cached, kept separate from OUT_DIR")

# out/dist/ is reserved (see the header comment above) and nothing writes
# to it yet, so it's created eagerly here -- otherwise it simply wouldn't
# exist until something populates it, which could easily read as "this
# was never wired up" rather than "intentionally reserved for later".
file(MAKE_DIRECTORY "${DIST_OUTPUT_DIR}")

# Subdirectory NAMES only (not full paths) for where compiled artifacts
# land inside whatever the current build directory is (CMAKE_BINARY_DIR) --
# these apply correctly no matter where -B points, including if you build
# outside out/build entirely. LIBRARY defaults to the same subdirectory as
# RUNTIME because shared libraries need to sit next to the executables
# that load them via the project's $ORIGIN-relative RPATH strategy (see
# CMakeLists.txt).
set(RUNTIME_OUTPUT_SUBDIR "bin" CACHE STRING "Build-dir subdirectory for runtime artifacts (executables, .dll, .so)")
set(LIBRARY_OUTPUT_SUBDIR "bin" CACHE STRING "Build-dir subdirectory for shared-library artifacts")
set(ARCHIVE_OUTPUT_SUBDIR "lib" CACHE STRING "Build-dir subdirectory for static/import-library artifacts (.a, .lib)")

# =============================================================================
# .gitignore sync
#
# Keeps a marked block in .gitignore in sync with the actual, current
# values of the path variables above -- every configure, automatically.
# Nothing outside that block (e.g. the Visual Studio template this
# project's .gitignore starts from) is ever touched. This is what makes
# this file the real source of truth: change OUT_DIR (or override it with
# -DOUT_DIR=...) and .gitignore updates on the next configure instead of
# silently drifting out of sync with what actually gets generated.
#
# If .gitignore doesn't exist yet, it's created with just this block. If
# it exists but has no markers yet, the block is appended. If the markers
# are already there, only the content between them is replaced. The file
# is only rewritten when the computed content actually differs, so a
# normal reconfigure doesn't spuriously touch it (or its mtime).
#
# A path is only added if it resolves inside the repo (CMAKE_SOURCE_DIR)
# -- e.g. an overridden TOOLCACHE_DIR pointing somewhere else entirely is
# silently skipped, since .gitignore has nothing meaningful to say about
# a location outside the repository it lives in.
# =============================================================================

set(_pmn_gitignore_begin "# >>> Configuration.cmake generated paths -- BEGIN (do not edit by hand; edit Configuration.cmake and reconfigure) >>>")
set(_pmn_gitignore_end   "# <<< Configuration.cmake generated paths -- END <<<")

function(_pmn_gitignore_relpath ABSOLUTE_VALUE OUT_VAR)
    file(RELATIVE_PATH _pmn_rel "${CMAKE_SOURCE_DIR}" "${ABSOLUTE_VALUE}")
    if(_pmn_rel MATCHES "^\\.\\.")
        set(${OUT_VAR} "" PARENT_SCOPE)
    else()
        set(${OUT_VAR} "${_pmn_rel}" PARENT_SCOPE)
    endif()
endfunction()

set(_pmn_gitignore_entries "")
foreach(_pmn_dir_var OUT_DIR DIST_OUTPUT_DIR INSTALL_OUTPUT_DIR DOCS_OUTPUT_DIR TOOLCACHE_DIR)
    _pmn_gitignore_relpath("${${_pmn_dir_var}}" _pmn_rel)
    if(_pmn_rel)
        list(APPEND _pmn_gitignore_entries "${_pmn_rel}/")
    endif()
endforeach()

_pmn_gitignore_relpath("${COMPILE_COMMANDS_DESTINATION}" _pmn_cc_rel)
if(_pmn_cc_rel)
    list(APPEND _pmn_gitignore_entries "${_pmn_cc_rel}")
endif()

list(REMOVE_DUPLICATES _pmn_gitignore_entries)
list(SORT _pmn_gitignore_entries)

set(_pmn_gitignore_block_content "")
foreach(_pmn_entry ${_pmn_gitignore_entries})
    string(APPEND _pmn_gitignore_block_content "${_pmn_entry}\n")
endforeach()

set(_pmn_gitignore_path "${CMAKE_SOURCE_DIR}/.gitignore")
set(_pmn_gitignore_new_block "${_pmn_gitignore_begin}\n${_pmn_gitignore_block_content}${_pmn_gitignore_end}")

if(EXISTS "${_pmn_gitignore_path}")
    file(READ "${_pmn_gitignore_path}" _pmn_gitignore_content)
else()
    set(_pmn_gitignore_content "")
endif()

string(FIND "${_pmn_gitignore_content}" "${_pmn_gitignore_begin}" _pmn_gitignore_begin_pos)
string(FIND "${_pmn_gitignore_content}" "${_pmn_gitignore_end}" _pmn_gitignore_end_pos)

if(_pmn_gitignore_begin_pos GREATER -1 AND _pmn_gitignore_end_pos GREATER -1
   AND _pmn_gitignore_end_pos GREATER _pmn_gitignore_begin_pos)
    # Replace only the content between the existing markers.
    string(SUBSTRING "${_pmn_gitignore_content}" 0 ${_pmn_gitignore_begin_pos} _pmn_gitignore_before)
    string(LENGTH "${_pmn_gitignore_end}" _pmn_gitignore_end_marker_len)
    math(EXPR _pmn_gitignore_after_start "${_pmn_gitignore_end_pos} + ${_pmn_gitignore_end_marker_len}")
    string(SUBSTRING "${_pmn_gitignore_content}" ${_pmn_gitignore_after_start} -1 _pmn_gitignore_after)
    set(_pmn_gitignore_final "${_pmn_gitignore_before}${_pmn_gitignore_new_block}${_pmn_gitignore_after}")
else()
    # No markers yet -- append (trimming trailing blank lines first so we
    # don't accumulate extra blank lines across repeated appends).
    string(REGEX REPLACE "\n+$" "" _pmn_gitignore_trimmed "${_pmn_gitignore_content}")
    if(_pmn_gitignore_trimmed STREQUAL "")
        set(_pmn_gitignore_final "${_pmn_gitignore_new_block}\n")
    else()
        set(_pmn_gitignore_final "${_pmn_gitignore_trimmed}\n\n${_pmn_gitignore_new_block}\n")
    endif()
endif()

if(NOT _pmn_gitignore_final STREQUAL _pmn_gitignore_content)
    file(WRITE "${_pmn_gitignore_path}" "${_pmn_gitignore_final}")
endif()
