# Hot Reload

Patches a running process's own code in place so that a new build of a
function takes effect immediately -- no restart, and every existing
caller (including a raw C function pointer someone cached ten minutes
ago) picks up the change automatically. `hot_reload.h` is the API;
`source/examples/HotReloadDemo` is a full worked example; this file is
the overview `hot_reload.h`'s own doc comments don't have room for,
especially the parts you should know are *not* guaranteed.

If you just want to see it run:

```sh
cmake -S . -B out -DBUILD_EXAMPLES=ON
cmake --build out
./out/bin/examples/hotreload_demo_host
```

Then open `source/examples/HotReloadDemo/PluginV1/src/Plugin.cpp` and
`PluginV2/src/Plugin.cpp` side by side against the output -- they're
written to stand in for "the file before your edit" and "after". The
same two directories' `CppWidget.cpp` do the same for a real C++
class with no `extern "C"` anywhere -- see "Hot reloading C++ symbols"
below.

Both of those exist to prove things deterministically, though -- for
actually playing with this yourself, see
`source/examples/HotReloadLiveDemo/README.md` instead: a host that
stays running, and one plugin file you edit and rebuild as many times
as you want while it's live.

## How it works, in one paragraph

`hot_reload_load()` loads a shared library and reads its exported
symbol table. `hot_reload_reload()` loads a *new* build of that same
library, reads its symbol table too, and for every name both builds
export as a function, overwrites the first few bytes of the *old*
function with an unconditional jump to the *new* one's address. Every
existing call site -- direct calls, calls through the PLT/GOT, raw
function pointers already sitting in some struct -- reaches the old
address and immediately redirects, because that's now all that's
there. Full byte-level detail, verified against a real assembler
rather than transcribed from memory, is in
`src/Architecture.cpp`'s file comment.

## The two guarantees this library asked to make

**"Only executable code changes, never data."** This isn't a naming
convention you have to follow -- it's mechanical. `src/SymbolTable.cpp`
classifies every exported symbol using the loader's own metadata (an
ELF symbol's type + its section's executable flag; a PE export's
containing section's characteristics), and `HotReloadContext` refuses
to patch anything not classified as a function, on *either* side of a
reload. `source/examples/HotReloadDemo` proves this concretely:
`plugin_build_marker` is an exported `int` sitting right next to real
functions, changes value between V1 and V2 exactly like a function's
behavior would, and the demo reads its original address again *after*
reloading to show it's untouched.

**"Handle deletion gracefully."** A function missing from the new
build is reported through `HotReloadOptions::on_symbol_event`
(`HOT_RELOAD_SYMBOL_REMOVED`) and, by default, its old implementation
is simply left running -- nothing is ever patched to a null or
garbage address. If you want stale calls to be loud instead,
`on_deleted_symbol` lets you redirect it to a stub of your own. Both
policies are exercised in this repo:
`source/examples/HotReloadDemo/Host` supplies a stub;
`source/tests/HotReloadTests` covers the default.

Neither guarantee depends on anything the hot-reloadable module's
author has to remember to do correctly -- see "What this cannot
detect" below for the one important thing that's true in reverse.

## Hot reloading C++ symbols (classes, virtual methods, operators)

Everything above works for a real, unmodified C++ mangled name with
**no changes to this library at all** -- classification only ever
looks at a symbol's type and section (see `src/SymbolTable.cpp`),
never at what its name looks like, so a class's ordinary method, a
virtual method, and an overloaded operator are, mechanically, no
different from an `extern "C"` function. This isn't a theoretical
claim: `source/examples/HotReloadDemo/Shared/CppWidgetApi.h` declares
a class with all three, exported with no `extern "C"` anywhere, living
in the *same* plugin build as the plain-C demo, and
`source/examples/HotReloadDemo/Host/Main.cpp` hot-reloads it -- run
the demo (see the top of this file) and watch the `CppWidget::` lines
in the output.

**Virtual methods work automatically, including through a real
vtable.** A virtual call is dispatched by loading a function pointer
out of the object's vtable and calling through it -- and that pointer
holds exactly the same address a direct call would, since the plugin's
own code populated the vtable with it. Patch that address, and *every*
path that reaches it redirects: a raw function pointer, a direct call,
and a vtable slot are all just different ways of holding the same
address. The demo proves this directly: it calls `widget->virtualTick(x)`
through ordinary polymorphic dispatch, once before `hot_reload_reload()`
and once after, on the *same* pre-existing object, and the second call
runs the new build's code.

That said, a vtable is data, and changing its *layout* -- adding,
removing, or reordering virtual functions, or changing base classes --
is exactly the data-layout hazard "What this cannot detect" describes
below, just in a C++-specific shape: a pre-existing object's vptr
still points at the *old* build's vtable, with the old slot count and
order, so new code that assumes the new layout while running against
an old object's vtable will read the wrong slot. A newly-constructed
object (created after the reload) gets the new vtable and is fine.
This library does not check for this -- it isn't something a symbol
table carries any information about -- so keep a hot-reloadable
class's virtual function set unchanged across a reload, the same
discipline as keeping a free function's signature unchanged.

**Operators are just functions with unusual mangled names** and need
nothing special either -- `CppWidgetApi.h`'s `operator+` patches
exactly like `tick()` below.

**The one real requirement, restated for C++:** a function only exists
to be patched at all if the compiler emitted an actual out-of-line
body for it somewhere addressable. This was already true for
`extern "C"` functions (see "Using it in your own module" below), but
C++ makes it much easier to violate by accident:

- A method defined *inside* a class body (`struct S { int f() { ...
  } };`) is implicitly `inline`, and a caller that includes the header
  will very often get the body inlined directly into its own compiled
  code, with no separate symbol left anywhere to patch. This is not
  hypothetical -- built and inspected while writing this feature:
  ```cpp
  struct InlineWidget { int tick(int x) { return x * 2; } };
  int caller(int x) { InlineWidget w; return w.tick(x); }
  ```
  compiled with optimization on, `nm -D` on the resulting library shows
  **no `InlineWidget::tick` symbol at all** -- the compiler deleted the
  separate function entirely, so there is nothing any binary-patching
  tool, this one included, could ever redirect. `caller`'s own compiled
  code has to be replaced by reloading `caller` itself.
- Templates are instantiated per translation unit and, unless
  explicitly instantiated once elsewhere, typically get inlined at
  each call site for the same reason.
- The fix, where it applies, is the same one C already needed: declare
  in a header, define exactly once out-of-line in a `.cpp` compiled
  into the reloadable module, and don't let a caller outside that
  module see the definition (so the compiler never has the option to
  inline it away). `CppWidgetApi.h`'s methods are declared in the
  header and defined in `PluginV1/src/CppWidget.cpp` /
  `PluginV2/src/CppWidget.cpp` for exactly this reason.

**Mangled names are only stable within one ABI family.** GCC and
Clang implement the same (Itanium) C++ ABI and mangle the demo's
`CppWidget` identically -- confirmed directly: the demo was built and
run with both compilers, and separately cross-compiled with MinGW and
run under Wine, all using the exact same hardcoded mangled-name string
in `Host/Main.cpp`. MSVC uses its own, unrelated scheme. This isn't a
new limitation this library introduces -- you were always going to
reload with the same toolchain you started a session with -- but it's
worth stating plainly for mangled names specifically, since unlike an
`extern "C"` name, a mangled one simply won't match at all across the
two families.

**Looking up a mangled name is genuinely more annoying than an
`extern "C"` one**, which is the practical reason to prefer the
latter for anything the *host* needs to call by a human-typed string.
There's no portable way to ask the compiler for another declaration's
mangled name at compile time; the demo gets it the same way you would
in practice, with `nm -D --defined-only <library> | c++filt`. Two ways
around this, both shown in the demo:
1. Keep one `extern "C"` factory/entry function (`make_cpp_widget()`
   in the demo) and reach everything else through it -- a returned
   object's *virtual* methods then need no lookup at all (see above).
2. For a method that isn't virtual, call `hot_reload_demangle()` on
   whatever name you *do* have (e.g. for logging -- see
   `HotReloadOptions::on_symbol_event`'s output in the demo, where
   `_ZN9CppWidget4tickEi` prints as `CppWidget::tick(int)`), or accept
   the manual `nm | c++filt` lookup as a one-time cost, same as the
   demo does for `CppWidget::tick`.

`hot_reload_demangle()` renders a real mangled name for display (via
`__cxa_demangle` on GCC/Clang/MinGW, `UnDecorateSymbolName` on MSVC)
and returns anything else -- including a plain `extern "C"` name --
completely unchanged, so it's safe to call on every
`HotReloadSymbolEventInfo::name` unconditionally, as the demo does.
It never changes what a name means for lookup purposes; it exists
purely so a log is readable.

**One general C++-plugin caveat, not specific to hot-reloading:**
exceptions and RTTI (`dynamic_cast`, `typeid`) generally should not
cross the boundary into or out of a dynamically-loaded module at
all -- this is already true of any `dlopen()`/`LoadLibrary()`-based
plugin architecture, for the same reasons cpp-style-guide.md section
11 gives for the plain C boundary, and hot-reloading a module doesn't
change it either way. The demo's `delete widget` relies on `new` and
`delete` resolving to the same allocator across the boundary, which
holds on Linux/macOS as tested here; it's worth double-checking on
Windows if you link the CRT in an unusual way (e.g. mismatched
static/dynamic CRT between the host and a plugin).

## Why do std::string/std::vector symbols show up as PATCHED?

Because they're real, addressable functions too, and classification
doesn't -- can't -- know that a symbol "belongs to the standard library"
as opposed to your own code. Confirmed directly: any plugin that uses
`std::string`/`std::vector<T>` internally will show template
instantiations like `std::vector<int>::_M_realloc_insert<...>` getting
`HOT_RELOAD_SYMBOL_PATCHED` on every single reload, even one that
changed nothing about how you use them -- because these are genuinely
present, genuinely exported (with default visibility, by default, on
every build tested here), and genuinely at a new address every time,
since that's just true of everything in a freshly-loaded `.so`/`.dll`.

**This is safe.** It's the exact same template instantiation in both
builds -- same mangled name, same object layout, same ABI, and (using
the same compiler and standard library on both sides, which you already
need for mangled names to match at all -- see above) functionally
equivalent code. Redirecting old calls to the new copy changes where the
logic runs, never what it does.

**It is not fixable by hiding symbols, and that's not just an
unimplemented feature.** The obvious fix -- compile with
`-fvisibility=hidden -fvisibility-inlines-hidden` -- does not work here,
confirmed directly: `libstdc++`'s headers mark these template
instantiations `default` visibility explicitly, in the header itself,
specifically so multiple `.so`s can share type identity for RTTI and
exception handling across shared-library boundaries. That annotation
wins over your translation unit's own `-fvisibility=hidden` default,
on purpose, and this library has no way to override it that wouldn't
also risk breaking that cross-DSO guarantee for anyone who needs it.

**A blanket "don't patch weak-linkage symbols" rule would be the wrong
fix even if it worked**, which is why this library doesn't do that
either: weak/COMDAT linkage is also exactly how a *hot-reloadable
template of your own* would be classified. A rule that reliably filters
out the standard library's internals would just as reliably filter out
your own reloadable generic code.

**What actually helps, if the log's noise level bothers you, is
narrowing your plugin's export surface** with
`hot_reload_restrict_exports()` (`cmake/HotReload.cmake`) and
`HOT_RELOAD_EXPORT` (`hot_reload.h`) -- see "Using it in your own
module" below. This doesn't remove the standard-library instantiations
specifically (see above), but it does stop your own internal helpers,
and (on GCC/Clang, if a target statically links its C++ runtime)
incidental runtime-library internals, from becoming part of the diff at
all -- both real cases observed while building this library's own
examples. Beyond that, filtering the event log for *display* is a
one-line addition in your own `on_symbol_event` callback -- e.g. skip
anything whose `hot_reload_demangle()`'d name starts with `"std::"`.

## Scope: what's actually implemented and checked

| | x86 | x86-64 | ARM64 | ARM32 |
|---|---|---|---|---|
| Jump-patch backend | yes | yes | yes | not implemented |
| Landing pad handled | ENDBR32 | ENDBR64 | BTI | -- |

*"Any architecture"* was the ask; ARM32 is the one mainstream target
deliberately left out, because it's the one case this project's
testing couldn't back up with confidence -- see below.

**Compilers.** GCC and Clang share one backend (including MinGW,
which is one or the other targeting Windows); MSVC has its own
`/hotpatch`-based path in `cmake/HotReload.cmake` and its own PE
export-table reader in `SymbolTable.cpp`.

**What was actually run, not just compiled**, while building this:
- Native GCC and Clang on Linux/x86-64: the full demo, both compilers,
  including a real ENDBR64 landing pad this toolchain turned out to
  emit by default -- see `src/Architecture.cpp`'s file comment.
- Cross-compiled to ARM64 and run under `qemu-aarch64`: the full demo,
  twice -- once plain, once with `-mbranch-protection=bti` -- to
  exercise the BTI-landing-pad path on real (emulated) ARM64
  semantics, not just have it compile.
- Cross-compiled with MinGW and run under Wine: the full demo,
  exercising the real Win32 `LoadLibrary`/`VirtualProtect`/
  `FlushInstructionCache` calls and the PE export-table parser. This
  run is also *why* the demo plugins now set
  `WINDOWS_EXPORT_ALL_SYMBOLS ON` -- the first attempt silently
  exported nothing under an MSVC-style build until that was added.
  Re-run after adding `CppWidgetApi.h`'s C++ class: confirmed the PE
  reader also correctly classifies a vtable and typeinfo as data (not
  code) on Windows, not just on ELF, and that GCC and Clang produce
  identical mangled names for the same class (the demo's hardcoded
  `_ZN9CppWidget4tickEi` lookup string was derived from a native GCC
  build but also worked unmodified against this MinGW/Wine build and
  the separate native Clang build).
- **MSVC itself was not run anywhere in this process** -- there is no
  Windows machine or MSVC install available in the environment this
  was built in. The MSVC-specific code (`/hotpatch`,
  `IMAGE_EXPORT_DIRECTORY` parsing, `VirtualProtect`,
  `FlushInstructionCache`) is written directly against Microsoft's
  documented structures and behavior, and the equivalent PE-reading
  code was compiled clean against real `<windows.h>` headers via
  MinGW, but treat the MSVC path as unverified until you've built it
  on an actual Windows box. If it doesn't work, `SymbolTable.cpp`'s
  PE branch and `cmake/HotReload.cmake`'s `/hotpatch` branch are the
  two places to look first.
- **32-bit ARM was not implemented at all**, deliberately: it's the
  one architecture in this list where getting it wrong is easy and
  getting it verified is hard. Its instructions are either 4 bytes
  (ARM mode) or a 2/4-byte mix (Thumb mode), and a function pointer's
  low bit is the only thing that says which -- get the interworking
  wrong and you don't get a clean crash, you get a CPU that's decoding
  the wrong instruction stream. Nothing in this environment could
  exercise that on real or emulated ARM32 hardware, so rather than
  ship an ARM32 backend on the strength of a manual reading of the
  ISA reference alone, it's left out. Adding it is a matter of
  implementing one more branch of `src/Architecture.cpp`'s interface;
  test it for real before trusting it.
- **macOS (Mach-O) was not implemented.** Nothing in the request named
  it, and it would need a third format reader alongside
  `SymbolTable.cpp`'s ELF and PE branches. `readExportedSymbols()` is
  the one function that would need it.

## What this cannot detect

**A changed function signature.** Patching redirects control flow at a
fixed address; it has no way to know from a symbol table alone that
`void tick(int)` became `void tick(int, int)`. If a function's
signature changes, give it a new exported name instead of reusing the
old one -- the old name is then correctly reported as
`HOT_RELOAD_SYMBOL_REMOVED` and the new one as `HOT_RELOAD_SYMBOL_ADDED`,
rather than being patched into a mismatched calling convention.

**A data layout change reached only through code that still gets
patched.** The library guarantees it never *touches* a data symbol
directly. It cannot guarantee that a patched function is safe to run
against a `struct`/class instance that already exists in memory with
the *old* layout -- e.g. a game object allocated before the reload,
if the new code adds a field to its type. This is a correctness
property of your own code across the reload boundary, not something a
symbol table (which carries no field-offset information) can check.
If you need this checked, the extension point is the same
place: `HotReloadContext::reload()` -- diffing the two builds' debug
info (DWARF or PDB) for layout changes in types touched by patched
functions is real work, genuinely useful, and out of scope for what's
here now.

**Being called concurrently with the code it's patching.** The
intended use is the same as every other tool in this space (Unreal's
Live Coding, Handmade Hero's hot reload, etc.): call
`hot_reload_reload()` from a quiescent point -- your main thread
between frames or ticks -- where nothing else is actively calling into
the module you're reloading. The write order in
`src/Architecture.cpp` (payload bytes before the opcode that arms
them, with a release fence between) protects the realistic case where
a call arrives right at that boundary. It does not protect a thread
that is genuinely, actively executing through the exact bytes being
patched at that moment -- doing that in general on a variable-length
ISA needs a signal-handler-based retry protocol, a substantially
bigger mechanism than a dev-loop tool like this one calls for. See
that file's comment for the full reasoning.

## Known, accepted gaps

- **A `.so` with its section header table deliberately stripped**
  reads back as exporting no functions at all (empty, not a crash) --
  `SymbolTable.cpp`'s ELF reader uses section headers to find
  `.dynsym`/`.dynstr`, which is simpler and easier to get right than
  walking `.dynamic`'s `DT_*` tags the way the dynamic linker itself
  does, at the cost of this one edge case. Stripping section headers
  from a `.so` you are actively hot-reloading is an unusual, separate
  step from an ordinary `strip` and essentially never happens by
  accident.
- **PE exports have no per-symbol size.** Unlike ELF's `st_size`,
  which backs a real safety check, the Windows path estimates size
  from the gap to the next exported function and can only ever prove
  "not enough room," never prove "enough" -- see `SymbolTable.cpp`'s
  file comment. `/hotpatch` (applied automatically by
  `hot_reload_enable_patchable_functions()`) is the real safety
  mechanism on Windows; treat the size estimate as a bonus, not a
  guarantee.
- **A forwarder PE export** (one that re-exports another DLL's
  function) is classified as non-function and left alone, not
  followed.

## Diagnosing a failed load

`HOT_RELOAD_ERROR_LOAD_FAILED` alone doesn't say *why* -- a missing
file, a permissions problem, a corrupt or wrong-architecture image,
and a genuinely undefined symbol all look the same from the status
code alone. Call `hot_reload_last_error(ctx)` right after a failing
`hot_reload_load()`/`hot_reload_reload()` for the platform's own
description (`dlerror()` on POSIX, a formatted `GetLastError()` on
Windows).

The specific case that motivated adding this, reproduced directly
while writing it: a **non-inline `static` class member that is
declared but never given an out-of-class definition**.

```cpp
class Randomizer
{
	static std::mt19937& gen() { return m_gen; } // ODR-uses m_gen
	static std::mt19937 m_gen; // declared here -- never defined anywhere
};
```

This compiles cleanly and links cleanly into a shared library --
unlike an executable, a shared library is allowed to keep undefined
symbols by default, on the assumption that whatever eventually loads
it will supply them. It only fails once `dlopen(..., RTLD_NOW)` (this
library always uses `RTLD_NOW`, deliberately -- see the comment in
`ModuleHandle.cpp` on why a clean, catchable failure right here beats
a deferred, uncatchable one under `RTLD_LAZY`) tries to eagerly
resolve every symbol and can't find this one anywhere in the process.
Confirmed directly: building the same class with the code path that
uses it left uncalled produces a `.so` with no trace of the missing
symbol at all (the compiler drops the unused, never-instantiated
inline method entirely); the moment something calls it,
`hot_reload_last_error()` reports exactly
`undefined symbol: _ZN10Randomizer4m_genE`. The fix is the one C++17
already offers: mark it `inline static` (with its initializer, if it
has one) instead of a bare `static` declaration, the same way you'd
fix this in any shared library, hot-reloaded or not.

## Using it in your own module

1. `extern "C"` isn't required -- a real C++ mangled name works too
   (see "Hot reloading C++ symbols" above) -- but it's still the
   simplest choice for anything you want to look up by a plain,
   human-typed name, and the only way to get a name stable across
   compilers.
2. Build it as its own `SHARED` library target and call
   `hot_reload_enable_patchable_functions(<target>)`
   (`cmake/HotReload.cmake`) on it, so every exported function has
   guaranteed room for a jump. Without this, reload still works for
   any function whose compiled body happens to be large enough on its
   own -- functions that aren't get skipped
   (`HOT_RELOAD_SYMBOL_SKIPPED_TOO_SMALL`), not corrupted.
3. Decide how much of the target's export surface you want to be
   deliberate about:
   - **Simplest**: do nothing else. On GCC/Clang/MinGW this exports
     every symbol with external linkage by default; on MSVC, also set
     `WINDOWS_EXPORT_ALL_SYMBOLS ON` to match (MSVC exports nothing by
     default otherwise). `source/examples/HotReloadDemo`'s plugins use
     this style.
   - **Recommended for a real project**: call
     `hot_reload_restrict_exports(<target>)` (`cmake/HotReload.cmake`)
     instead, and mark each intended entry point with `HOT_RELOAD_EXPORT`
     (`hot_reload.h`) -- `extern "C" HOT_RELOAD_EXPORT int my_function(...)`
     for a plain-name free function (that exact order -- GCC/Clang want
     the attribute after `extern "C"`), or just `HOT_RELOAD_EXPORT` alone
     on a C++ class/method you want exported with its real mangled name
     intact. This keeps anything you didn't explicitly mark -- your own
     internal helpers, or (on GCC/Clang, if the target statically links
     its C++ runtime) incidental runtime-library internals -- out of the
     diff entirely. It does *not* narrow out `std::string`/`std::vector`
     template instantiations specifically; see "Why do std::string/
     std::vector symbols show up as PATCHED?" above for why not, and why
     that's fine either way. `source/examples/HotReloadLiveDemo`'s
     plugin uses this style -- compare the two examples' exported symbol
     lists (`nm -D --defined-only <path> | c++filt`) to see the
     difference directly.
4. Have your host `hot_reload_load()` it once at startup and
   `hot_reload_reload()` it after every rebuild, from a point where
   nothing else is calling into it (see "What this cannot detect"
   above).
5. Rebuilding to the *same output path* every time is fine --
   `HotReloadContext` stages every load through a fresh, uniquely-named
   copy internally specifically so your build system's exact
   write pattern can't cause a stale reload (a known gotcha for this
   style of tool; see `HotReloadContext.cpp`'s
   `stagePathForLoad()`).
