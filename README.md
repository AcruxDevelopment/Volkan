<div align="center">
  <img src="https://github.com/AcruxDevelopment/Magma/blob/main/magma-icon.png" alt="Logo" width="auto" height="80">
  
  # Volkan

  **C++ Hot-Reload library**
</div>

<br>

A cross-platform, cross-compiler C/C++ library that patches a running
process's own code in place, so a new build of a function takes effect
immediately, without restarting -- see
[`source/HotReload/README.md`](source/HotReload/README.md) for what it
does, what it guarantees, and its honest limitations. This file covers
the project around it: the CMake module system, its tooling, and how
the examples and tests fit together.

```sh
scripts/setup.sh    # optional, one-time -- see "Setup" below
scripts/build.sh Release -DBUILD_EXAMPLES=ON
./out/build/bin/examples/hotreload_demo_host
```

## What's here

| | |
|---|---|
| [`source/HotReload/`](source/HotReload/README.md) | The library itself: `hotreload_lib`, a `STATIC` library with a plain C API (`hot_reload.h`) over a C++ implementation |
| [`source/examples/HotReloadDemo/`](source/examples/HotReloadDemo) | A scripted, deterministic demo: a host + two builds of one plugin, exercising every guarantee the library makes |
| [`source/examples/HotReloadLiveDemo/`](source/examples/HotReloadLiveDemo/README.md) | An interactive demo: a host that stays running, and one plugin file you edit and rebuild yourself |
| [`source/tests/HotReloadTests/`](source/tests/HotReloadTests) | An automated `ctest` suite covering the library end to end, including a regression test for a real bug found while building this |

The rest of this file is about the project *around* that: a per-module
CMake setup adapted from a generic template (each module owns a tiny,
self-contained `CMakeLists.txt`; the root `CMakeLists.txt` discovers
them automatically), `scripts/` for the everyday commands, and the
code-style tooling. None of it is specific to hot-reloading -- if
you're only here for the library, `source/HotReload/README.md` is the
one to read.

## Layout

```
CMakeLists.txt                 project setup, options, install(), discovery -- edit by hand
Configuration.cmake             all output-path configuration -- edit to relocate anything
cmake/BuildHelpers.cmake        module macros + discovery mechanism -- edit rarely
cmake/Dependencies.cmake        vendoring / installed-library / FetchContent helpers -- edit rarely
cmake/Documentation.cmake       per-module Doxygen integration -- only ever LOOKS for a pinned Doxygen
cmake/DoxygenPin.cmake          shared version/URL/hash parameters for the pinned Doxygen
cmake/FetchDoxygen.cmake        standalone downloader (run via `cmake -P`, not during normal configure)
cmake/NinjaPin.cmake            shared version/URL/hash parameters for the pinned Ninja
cmake/FetchNinja.cmake          standalone downloader (run via `cmake -P`, not during normal configure)
cmake/HotReload.cmake           hot_reload_enable_patchable_functions() / hot_reload_restrict_exports()
source/HotReload/               the library -- library module "hotreload_lib" (STATIC)
source/examples/HotReloadDemo/  worked example: a host + two builds of one hot-reloadable plugin
source/examples/HotReloadLiveDemo/  interactive: a persistent host + one plugin file you edit yourself
source/tests/HotReloadTests/    the automated test suite
source/tests/UndefinedSymbolFixture/  a fixture the test suite loads, deliberately broken -- see its own comment
scripts/setup.sh, setup.bat     one-time: fetch pinned Ninja + Doxygen, check tooling, activate git hook
scripts/build.sh, build.bat     configure + build -- prefers a pinned Ninja, falls back gracefully
scripts/run.sh, run.bat         run a module by its CMake target name
scripts/install.sh, install.bat install this project's own artifacts only
scripts/docs.sh, docs.bat       build documentation for every module (or one) -- needs setup.sh first
scripts/refactor.sh, refactor.bat  auto-fix formatting/naming across this project's own code
.clang-format, .clang-tidy      C++ style guide tooling (see "Code style & tooling" below)
.editorconfig, .gitattributes   -- same
.gitignore                      Visual Studio template + a block auto-synced from Configuration.cmake
c-api/.clang-tidy               naming override template for a C API subtree -- applied for real at
                                 source/HotReload/src/c-api/ and source/HotReload/include/hot_reload/
.githooks/pre-commit            formatting/lint pre-commit hook
cpp-style-guide.md, TOOLING.md  the style guide itself + tooling docs
```

Everything generated -- the build tree, install output, docs, and reserved
packaging output -- lives under one `out/` directory (see "Output paths"
below); nothing under `out/` is source-controlled.

Each module's `CMakeLists.txt` is (conventionally) one line:

```cmake
# source/HotReload/CMakeLists.txt
add_lib_module(hotreload_lib TYPE STATIC OUTPUT_NAME "hotreload")
```

A module declares its own dependencies in its own `CMakeLists.txt` --
same "minimal tool intervention" principle as module registration:
nothing to add in the root file. This project's own modules don't
currently need any third-party dependency, so the table in "Three ways
to bring in a dependency" below is showing you machinery this project
isn't using itself right now -- it's still there, exercised, and ready
if you add a module that needs it.

## Output paths (`Configuration.cmake`)

Every generated path is a cache variable, overridable with `-D<NAME>=...`
at configure time instead of editing any file:

| Variable | Default | What it controls |
|---|---|---|
| `OUT_DIR` | `out/` | Root of everything generated; override this to relocate all of the below at once |
| `INSTALL_OUTPUT_DIR` | `out/install` | Root of the `cmake --install` tree, one subdirectory per `DIST_TARGET` |
| `DIST_OUTPUT_DIR` | `out/dist` | Reserved for packaged/archived output (e.g. a future CPack integration) -- not populated by this project yet, but created and ready |
| `DOCS_OUTPUT_DIR` | `out/docs` | Root of generated Doxygen documentation, one subdirectory per module |
| `TOOLCACHE_DIR` | `.cache/tools` | Where downloaded, pinned build tools (currently Ninja and Doxygen) are cached -- kept outside `OUT_DIR` so `rm -rf out/` doesn't force a re-download |
| `COMPILE_COMMANDS_DESTINATION` | `compile_commands.json` | Where `compile_commands.json` is copied to after every build (see "compile_commands.json / clang tooling" below) |
| `RUNTIME_OUTPUT_SUBDIR` | `bin` | Subdirectory (inside whatever the build dir is) for executables/`.dll`/`.so` |
| `LIBRARY_OUTPUT_SUBDIR` | `bin` | Subdirectory for shared-library artifacts (kept alongside executables so `$ORIGIN` RPATH resolves them) |
| `ARCHIVE_OUTPUT_SUBDIR` | `lib` | Subdirectory for static/import-library artifacts |

The build directory itself (`CMAKE_BINARY_DIR`, where `CMakeCache.txt`
lives) isn't a `Configuration.cmake` variable -- it's fixed by whatever you
pass to `-B` when configuring. `scripts/build.sh`/`build.bat` default to
`out/build`, so the resulting layout is:

```
out/
  build/                          CMAKE_BINARY_DIR -- out/build/bin, out/build/lib during normal builds
  install/<DIST_TARGET>/{bin,lib} what `cmake --install` / scripts/install.sh populates
  dist/                           reserved for future packaging output
  docs/<module>/html/, docs/index.html   generated documentation
```

Example -- build into a different location entirely:

```bash
cmake -G Ninja -S . -B /tmp/my-build -DOUT_DIR=/tmp/my-out
cmake --build /tmp/my-build
```

### `.gitignore` stays in sync automatically

`Configuration.cmake` is the single source of truth for every path above
-- `.gitignore` doesn't hardcode its own copy of them. Every configure,
a marked block near the bottom of `.gitignore` is regenerated from the
*current* value of each path variable:

```
# >>> Configuration.cmake generated paths -- BEGIN (do not edit by hand; edit Configuration.cmake and reconfigure) >>>
.cache/tools/
compile_commands.json
out/
out/dist/
out/docs/
out/install/
# <<< Configuration.cmake generated paths -- END <<<
```

Change `OUT_DIR` (or any of the others, permanently in `Configuration.cmake`
or per-configure with `-D...=...`) and this block updates to match on the
next configure -- nothing to keep in sync by hand. Everything outside the
block is untouched: `.gitignore` ships based on GitHub's
[`VisualStudio.gitignore`](https://github.com/github/gitignore/blob/main/VisualStudio.gitignore)
template, and that content is never rewritten. A path is only added if it
resolves *inside* the repo -- pointing `TOOLCACHE_DIR` somewhere else
entirely just omits it, since `.gitignore` has nothing useful to say about
a location outside the repository it lives in. The file is only rewritten
when the computed block actually differs from what's already there, so a
normal reconfigure doesn't spuriously touch its contents or mtime.

## Three ways to bring in a dependency (`cmake/Dependencies.cmake`)

Not currently used by any module in this project (`hotreload_lib` and
everything under `source/examples/`/`source/tests/` are entirely
self-contained -- no network access is needed to build any of it), but
fully exercised machinery, ready for a module that does need one:

| Helper | For | Usage |
|---|---|---|
| `add_vendor_header_only(NAME [INCLUDE_DIR <dir>])` | A buildless header-only drop under `vendor/<NAME>/include` | `add_vendor_header_only(some_header_lib)` |
| `add_vendor_subdirectory(SUBDIR)` | A vendored dependency with its own `CMakeLists.txt`, under `vendor/<SUBDIR>` | `add_vendor_subdirectory(some_vendored_lib)` |
| `add_dependency(NAME <FetchContent_Declare args>)` | An installed package (`FIND_PACKAGE_ARGS ...`) with a fetch-from-source fallback | `add_dependency(fmt GIT_REPOSITORY ... FIND_PACKAGE_ARGS NAMES fmt)` |

All three are safe to call more than once for the same name (e.g. two
modules that need the same dependency) and mark the dependency's headers
`SYSTEM`, so `-Wall -Wextra -Wpedantic`/`/W4` -- which we only ever apply
per-target inside our own module macros, never globally -- doesn't fire on
code you don't control. Their targets are also dropped from
`compile_commands.json` (see below).

`add_dependency` is CMake's native find-then-fetch integration: it tries
`find_package()` first (an already-installed apt/vcpkg/conan copy) via
`FIND_PACKAGE_ARGS`, and only downloads + builds from source if nothing is
found. Omit `FIND_PACKAGE_ARGS` to always fetch. Requires network access
the first time a dependency isn't already installed.

A module `DEPENDS` on whatever target the chosen path defines exactly
like depending on another module -- `add_lib_module`/`add_exe_module`
don't need to know or care which of the three supplied it.

## Documentation (automatic per module, pinned Doxygen, entirely opt-in)

Every module registered via `add_lib_module`/`add_exe_module`/
`add_test_module`/`add_example_module` gets its own
[Doxygen](https://www.doxygen.nl/) documentation target for free -- nothing
to add to a module's own `CMakeLists.txt`. This project uses a pinned
Doxygen (currently **1.16.1**, see `DOXYGEN_PINNED_VERSION` in
`cmake/DoxygenPin.cmake`) -- it deliberately never looks for or uses a
system-installed `doxygen`, so every machine building this project
generates identical output regardless of what (if anything) is on `PATH`.

**Building the project never needs Doxygen.** `cmake/Documentation.cmake`
only ever *looks* for an already-fetched pinned Doxygen under
`.cache/tools/` (`TOOLCACHE_DIR`) -- it never downloads anything itself
during a normal configure, so `scripts/build.sh` has no Doxygen/network
dependency at all. Fetching it is a separate, explicit, one-time step:

```bash
scripts/setup.sh    # see "Setup" below -- fetches the pinned Doxygen once
```

Until you've run that, `scripts/docs.sh` prints a clear message instead of
building anything -- everything else (the actual C++ project) builds
completely normally either way:

```
Documentation isn't set up yet (or ENABLE_DOXYGEN=OFF). Run scripts/setup.sh (or setup.bat) once to fetch the pinned Doxygen, then reconfigure.
```

Once set up:

```bash
scripts/docs.sh                # build docs for every module, prints where to open it
scripts/docs.sh hotreload_lib  # build docs for just one module (docs_<target>)
```
```bat
scripts\docs.bat
scripts\docs.bat hotreload_lib
```

Output goes to `out/docs/<module>/html/index.html` (a sibling of
`out/build/`, not nested inside it -- see "Output paths" above), plus a
landing page at `out/docs/index.html` linking to every module.
Documentation is **not** built as part of a normal `scripts/build.sh` --
it's opt-in, the same way tests and examples are.

**Writing docs for a module** is just normal Doxygen comments in that
module's headers -- e.g. `source/HotReload/include/hot_reload/hot_reload.h`,
which is already written this way throughout.

Even with zero comments, a module's docs are still generated and useful --
file lists, function/class signatures, and (if [Graphviz](https://graphviz.org/)
is installed on your system -- this one enhancement, unlike Doxygen
itself, is still detected rather than pinned, since it's optional) call
graphs are produced regardless, since `DOXYGEN_EXTRACT_ALL` is on by
default. Comments just make it richer. Drop a `README.md` in a module's
own directory and it's automatically used as that module's Doxygen main
page -- `source/HotReload/README.md` already does this.

**Configuring it** -- nothing is required, but everything is overridable:

```bash
# Disable entirely (docs targets print a message instead of building):
cmake -S . -B out/build -DENABLE_DOXYGEN=OFF

# Pin a different Doxygen version (also update the two SHA256 hashes in
# cmake/DoxygenPin.cmake to match that release's assets -- a stale hash
# fails scripts/setup.sh loudly, on purpose, rather than silently
# accepting an unexpected file -- then re-run scripts/setup.sh):
cmake -S . -B out/build -DDOXYGEN_PINNED_VERSION=1.17.0

# Any Doxyfile tag can be set as a CMake variable, e.g. stricter output:
cmake -S . -B out/build -DDOXYGEN_WARN_IF_UNDOCUMENTED=YES -DDOXYGEN_QUIET=NO
```

A single module can opt out with `NO_DOCS`:

```cmake
# some/internal/module/CMakeLists.txt
add_lib_module(internal_only TYPE STATIC NO_DOCS)
```

Vendored and fetched dependencies (`cmake/Dependencies.cmake`) are never
auto-documented -- same reasoning as their `compile_commands.json`
exclusion, it isn't your code.

## compile_commands.json / clang tooling

`CMAKE_EXPORT_COMPILE_COMMANDS` is on by default, and every build copies
`compile_commands.json` from the build directory to the project root so
clangd/clang-tidy/IWYU find it without extra configuration (CMake only
ever writes it into the build directory; most editors look for it at the
repo root). Vendored and fetched dependencies are excluded from it, so it
only reflects code this project actually owns. The project also builds
cleanly with clang directly:

```bash
cmake -G Ninja -S . -B out/build -DCMAKE_CXX_COMPILER=clang++ -DDIST_TARGET=linux-x64-clang
cmake --build out/build
```

Verified, not assumed, throughout this project's own development: every
module here (the library, both example demos, and the test suite) was
built and its tests/demos run to completion under both GCC and Clang.

## Code style & tooling

This project follows [`cpp-style-guide.md`](cpp-style-guide.md) (tabs,
Allman braces, `PascalCase` file names, `camelCase` functions/variables,
`m_`/`s_`/`g_` member/static/global prefixes, plus a distinct Part II
style -- `snake_case`, library-prefixed names -- for any C API boundary;
see the file itself for the full rules and rationale), enforced by the
tooling described in [`TOOLING.md`](TOOLING.md):

| File | Enforces |
|---|---|
| `.clang-format` | All formatting: tabs, Allman braces, pointer alignment, include order, line length |
| `.clang-tidy` | Naming conventions and a subset of language-feature rules, for this project's own C++ code |
| `c-api/.clang-tidy` | A naming override (`snake_case`) for a C API subtree -- see below for where this is actually applied |
| `.editorconfig` | Baseline tabs/whitespace for editors that don't run clang-format live |
| `.gitattributes` | Forces LF line endings for source files on every OS |
| `.githooks/pre-commit` | Runs clang-format + clang-tidy on staged files before each commit; warns and skips (never blocks) if a tool isn't installed |

**Fixing violations automatically**, instead of hunting them down by hand:

```bash
scripts/refactor.sh                   # every file under source/ and c-api/ by default
scripts/refactor.sh source/HotReload  # just one file or directory
```
```bat
scripts\refactor.bat
scripts\refactor.bat source\HotReload
```

Runs `clang-format -i` (always) and `clang-tidy --fix` (if
`compile_commands.json` exists -- build once first if it doesn't) across
the given scope. The pre-commit hook only *checks*; this is the "just fix
it" counterpart -- review the result with `git diff` afterward, same as
any auto-formatter.

**Be especially careful reviewing `git diff` on a file with more than one
architecture/OS branch** (`#if defined(__x86_64__) ... #elif ... #endif`,
several of which exist under `source/HotReload/src/` -- see
`Architecture.cpp`, `ModuleHandle.cpp`, `MemoryProtection.cpp`,
`SymbolTable.cpp`). `clang-tidy` only ever analyzes the ONE branch active
for whichever platform you're running it on -- it cannot see, and will
not touch, the source text inside the other, inactive `#elif` branches at
all. Verified directly while building this project: running `--fix` on
`Architecture.cpp` from this x86-64 Linux machine renamed a local
constant inside the x86-64 branch only, then updated a later line shared
by *all three* architecture branches to match the new name -- silently
breaking the build for the other two architectures, which still declared
the old name. Caught only by reviewing the diff and separately
cross-compiling every branch, which is exactly the review step this
paragraph is asking you to actually do on a file shaped like this, not
skip.

With no argument, only `source/` and `c-api/` are ever scanned -- `out/`,
`.cache/`, and `.git/` are never walked, so this can't accidentally spend
minutes reformatting/analyzing a fetched dependency's entire source tree
(this project doesn't currently have one under `out/build/_deps/`, but the
exclusion is unconditional, not conditional on one existing). Passing an
explicit scope takes it as given: `refactor.sh` still excludes
`out`/`.cache`/`.git` defensively if your scope happens to overlap with
them; `refactor.bat` does not (its Windows batch equivalent of that filter
turned out to be unreliable to verify, so it was removed rather than
shipped uncertain -- pick a scope that doesn't overlap with those
directories, which is the normal case anyway, e.g. `source\HotReload`).

**One-time setup per clone** to activate the hook -- `scripts/setup.sh` /
`setup.bat` does this for you (see "Setup" below) along with fetching the
pinned Doxygen and checking for clang-format/clang-tidy; the two commands
it runs for the hook specifically are:

```bash
chmod +x .githooks/pre-commit
git config core.hooksPath .githooks
```

**clang-tidy needs `compile_commands.json`** to resolve `#include`s --
this project already generates and copies it to the repo root on every
build (see above), so as long as you've built at least once, the hook's
clang-tidy check works with no extra setup.

**This project genuinely has a C API subtree**, unlike the generic
template this was adapted from: `source/HotReload/include/hot_reload/`
(the public header) and `source/HotReload/src/c-api/` (its `extern "C"`
implementation) each carry a copy of `c-api/.clang-tidy`, positioned at
the root of the subtree they apply to -- `clang-tidy` always uses the
closest `.clang-tidy` file up the directory tree, so files under either
directory pick up `snake_case` naming instead of the rest of this
project's `PascalCase`/`camelCase`, while `source/HotReload/src/`'s own
Part I (C++) files, one level up, keep inheriting this repo-root
`.clang-tidy`'s rules exactly as normal. See either copy's own header
comment for the full explanation of how clang-tidy's directory-scoped
config resolution makes that work.

## How module discovery works

The root `CMakeLists.txt` calls `_discover_module_subdirectories()` on
three roots, in this order:

| Root | What it holds |
|---|---|
| `source/*` | library AND app/executable modules, side by side |
| `source/tests/*` | test modules |
| `source/examples/*` | example modules |

(`source/tests` and `source/examples` aren't picked up by the first call
-- they have no `CMakeLists.txt` directly inside them, only their own
children do.)

Any immediate child directory that contains its own `CMakeLists.txt` gets
`add_subdirectory()`'d automatically (`vendor/` is not one of these roots
-- vendored dependencies are opted into explicitly by the modules that
need them, not auto-built by default). This uses `CONFIGURE_DEPENDS`, so
the *next build* reconfigures on its own after you add or remove a module
directory -- no manual `cmake` re-run needed. A module may `DEPENDS` on a
module discovered later in this list, or reference one via
`add_dependencies()`/a generator expression before that other module's
own `CMakeLists.txt` has even run -- order doesn't affect linking or
target existence (CMake resolves target names, and dependency edges to
not-yet-declared targets, across the whole project at generate time --
verified directly with an isolated, minimal reproduction while building
this project, not assumed).

A module directory can itself contain more than one CMake target when its
shape doesn't fit the "one target per module" default -- see
`source/examples/HotReloadDemo/PluginV1/CMakeLists.txt`'s own comment for
a real example (a hot-reloadable plugin needs to exist as two separate
builds of the same exported names, which `add_lib_module` doesn't assume)
and `source/examples/HotReloadDemo/CMakeLists.txt` for the small grouping
file that pattern needs, one level up, purely so discovery (which only
looks one level deep into `source/examples/`) finds it at all.

## Requirements

CMake >= 3.25, a C++20 compiler (GCC/Clang/MSVC). **No network access is
needed to build this project** -- every module here is self-contained,
with no `add_dependency()`-fetched third-party library anywhere (see
"Three ways to bring in a dependency" above). Ninja and Doxygen are both
pinned and fetched automatically (see "Setup" below) if you want them,
not system dependencies, and `scripts/build.sh` warns rather than fails
if Ninja isn't available at all (see "Build" below).
[Graphviz](https://graphviz.org/) is optional for Doxygen call graphs, and
[clang-format](https://clang.llvm.org/docs/ClangFormat.html)/
[clang-tidy](https://clang.llvm.org/extra/clang-tidy/) are optional for
the pre-commit hook and `scripts/refactor.sh` (see "Code style & tooling"
above) -- both are detected, not pinned, since they're editor/workflow
tools rather than part of the build itself. `scripts/setup.sh` (below)
checks for both and prints install guidance if either is missing.

To actually try hot-reloading something (rather than just build the
library), you'll also want a way to rebuild one file while another
process keeps running -- covered in
[`source/examples/HotReloadLiveDemo/README.md`](source/examples/HotReloadLiveDemo/README.md).

## Setup

Optional, and separate from building (see "Requirements", "Build", and
"Documentation" above) -- run once per clone, or any time you want to
pick up a newer pinned version after bumping `NINJA_PINNED_VERSION` or
`DOXYGEN_PINNED_VERSION`:

```bash
scripts/setup.sh
```
```bat
scripts\setup.bat
```

Does four things, each independent of the others:

1. Fetches the pinned Ninja (`cmake/FetchNinja.cmake`) -- used by
   `scripts/build.sh`/`build.bat` if present (see "Build" below); not
   required, a system Ninja works fine too.
2. Fetches the pinned Doxygen (`cmake/FetchDoxygen.cmake`) -- needed for
   `scripts/docs.sh`, not for building the project.
3. Checks for `clang-format`/`clang-tidy` on `PATH` and prints
   OS-appropriate install guidance if either is missing (never
   auto-installs -- that would need assumptions about your package
   manager and elevated privileges this script shouldn't assume it has).
4. Activates the pre-commit hook for this clone, if it's a git repository
   (`git config core.hooksPath .githooks`).

Safe to re-run any time -- each step is a no-op if there's nothing to do
(e.g. the pinned Ninja/Doxygen are already cached).

## Build

```bash
scripts/build.sh                              # Release (default), library + tests only if BUILD_TESTS was set before
scripts/build.sh Release -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON   # + the automated tests + both demos
```
```bat
scripts\build.bat
scripts\build.bat Release -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
```

`BUILD_TESTS` and `BUILD_EXAMPLES` both default to `OFF` (see
`CMakeLists.txt`) -- plain `scripts/build.sh` with no extra arguments
builds only `hotreload_lib` itself. Anything after the build type is
passed straight through to `cmake`'s configure step (tested directly:
`scripts/build.sh Debug -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON` builds the
library, both demos, and the test suite together), and CMake caches `-D`
options across reconfigures, so passing them once and calling
`scripts/build.sh` plain afterward for incremental builds keeps them set
-- no need to repeat them on every call.

Picks a generator in this order, each falling back to the next:

1. **A pinned Ninja**, if `scripts/setup.sh`/`setup.bat` has fetched one --
   passed to CMake explicitly via `-DCMAKE_MAKE_PROGRAM=...` so it's used
   even if a different Ninja also happens to be on `PATH` (same "always
   the pinned copy" preference as Doxygen -- see "Documentation" above).
2. **A system Ninja**, if one is on `PATH` and no pinned copy was found.
3. **CMake's own default generator** for the platform, if neither Ninja is
   available anywhere -- `scripts/build.sh`/`build.bat` print a warning
   and continue rather than fail. (`CMakeLists.txt` separately warns too,
   once CMake reports which generator it actually picked -- this is the
   pre-existing "This project targets Ninja" message, unrelated to this
   script.)

Verified concretely, not assumed: with neither a pinned nor a system Ninja
present anywhere, `scripts/build.sh` still successfully configures, builds,
and produces working binaries -- it just uses whatever CMake's default
generator is for the platform (Unix Makefiles on Linux, typically NMake or
a Visual Studio generator on Windows) instead.

## Run

```bash
scripts/run.sh hotreload_demo_host
scripts/run.sh hotreload_live_demo_host
scripts/run.sh              # no args: lists available targets
```
```bat
scripts\run.bat hotreload_demo_host
scripts\run.bat hotreload_live_demo_host
```

`run.sh`/`run.bat` resolve the CMake **target id** (not the binary's
`OUTPUT_NAME`) to the actual binary via a manifest CMake generates at
configure time, so these scripts never need editing when a module is
added, removed, or renamed. Requires `-DBUILD_EXAMPLES=ON` to have been
set at configure time for either of the two hot-reload targets above,
since they live under `source/examples/`.

## Install

```bash
scripts/install.sh
```
```bat
scripts\install.bat
```

Always use the install script (or pass `--component hotreload` yourself if
calling `cmake --install` directly). A vendored/fetched dependency's own
`CMakeLists.txt` usually has `install()` rules of its own with no
component tag; installing without `--component` would also run those,
typically dumping files under `CMAKE_INSTALL_PREFIX` (e.g. `/usr/local`).
Tagging this project's own `install(TARGETS ...)` calls with `COMPONENT
hotreload` and always filtering on it is what keeps `cmake --install`
scoped to just this project's own artifacts -- currently just
`hotreload_lib` itself, since examples and tests are opt-in, dev-time
targets rather than something you'd install.

## Adding a module

1. Create a directory under the right root (see the discovery table above).
2. Add a `CMakeLists.txt` inside it with one `add_lib_module` /
   `add_exe_module` / `add_test_module` / `add_example_module` call, plus
   any `add_vendor_*`/`add_dependency` calls it needs.
3. Add your `.cpp`/`.h` files inside that same directory.
4. Rebuild (`scripts/build.sh`) -- no other file needs touching.

```cmake
# source/MyTool/CMakeLists.txt
add_exe_module(my_tool_exe OUTPUT_NAME "my-tool" DEPENDS hotreload_lib)
```

`scripts/run.sh my_tool_exe` picks it up automatically once it's built,
and `scripts/docs.sh my_tool_exe` documents it automatically too -- both
with zero edits anywhere outside `source/MyTool/`.
