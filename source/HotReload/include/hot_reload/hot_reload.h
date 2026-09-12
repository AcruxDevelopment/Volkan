/* hot_reload.h
 *
 * Public C API for the hot-reload library -- see cpp-style-guide.md
 * Part II (C API Style) for the naming/formatting rules this header
 * follows, and section 11 ("Runtime Loading") for why this boundary is
 * plain C in the first place: anything crossing a dlopen()/LoadLibrary()
 * boundary must have C linkage, so this is exactly the kind of surface
 * that section calls out.
 *
 * WHAT THIS LIBRARY DOES
 * -----------------------
 * hot_reload_load() loads a shared library (.so/.dll) under a logical
 * name. hot_reload_reload() loads a NEW build of that same shared
 * library and, for every function the two builds have in common,
 * rewrites the FIRST FEW BYTES of the old function in memory into a
 * jump to the new function -- so every existing call site (anyone who
 * already resolved the old function's address, including via a plain
 * C function pointer) transparently starts running the new code,
 * without restarting the process.
 *
 * WHAT THIS LIBRARY DELIBERATELY DOES NOT DO
 * --------------------------------------------
 * It never patches anything that isn't executable code. A symbol is
 * only ever a patch candidate if the loader's own symbol table marks
 * it as a function (ELF STT_FUNC / a PE export whose address falls
 * inside an executable section) -- see the design note in
 * SymbolTable.hpp for exactly how that's determined. Global variables,
 * class/struct instances, vtables, and anything else classified as
 * data are always left alone, on both the old and the new module --
 * there is no code path in this library that ever writes to a data
 * symbol. This is a mechanical guarantee from how symbols are
 * classified, not a naming convention you opt into.
 *
 * It also cannot verify that a hot-reloadable function's SIGNATURE
 * hasn't changed. Patching only ever redirects control flow at a fixed
 * entry point; it has no way to know, from a symbol table alone, that
 * `void tick(int)` became `void tick(int, int)` between builds. If a
 * function's signature changes, give it a new exported name (so the
 * old symbol is treated as removed and the new one as added -- see
 * HOT_RELOAD_SYMBOL_REMOVED below) rather than reusing the old name.
 * See the top-level README for the full list of hazards this library
 * cannot detect for you.
 *
 * REQUIREMENTS ON A HOT-RELOADABLE MODULE
 * ------------------------------------------
 * Matching an old build's function up with the new build's requires a
 * name that is stable across recompiles. extern "C" linkage -- see
 * cpp-style-guide.md section 11 -- is the simplest way to get one: no
 * mangling at all, so the name never depends on the compiler, its
 * version, or the function's signature.
 *
 * A real, unmodified C++ mangled name works too, and needs no special
 * handling by this library -- classification only ever looks at a
 * symbol's type and section, never at what its name looks like, so an
 * ordinary out-of-line method, a virtual method (redirected wherever
 * it's reached from -- directly or through a vtable slot, since both
 * hold the same address), and an operator overload all patch exactly
 * like any extern "C" function. What extern "C" actually buys you is
 * a name you can type by hand into hot_reload_get_symbol() and a
 * guarantee that it won't change if you rebuild with a different
 * compiler; a mangled name gives up both of those, only stays stable
 * within one ABI family (GCC and Clang share the Itanium C++ ABI;
 * MSVC does not), and -- the one hazard that has nothing to do with
 * this library and everything to do with what "hot-reloadable" can
 * even mean -- only exists to patch at all if the compiler actually
 * emitted an out-of-line body for it, which an implicitly-inline
 * method defined in a class body, or a template instantiated at the
 * call site, generally will not. See the top-level README's "Hot
 * reloading C++ symbols" section for the full picture, including how
 * to get a readable name for logging with hot_reload_demangle().
 */
#ifndef HOT_RELOAD_H
#define HOT_RELOAD_H

#include <stddef.h>

#ifdef _WIN32
	#ifdef HOT_RELOAD_BUILD_SHARED
		#define HOT_RELOAD_API __declspec(dllexport)
	#elif defined(HOT_RELOAD_USE_SHARED)
		#define HOT_RELOAD_API __declspec(dllimport)
	#else
		#define HOT_RELOAD_API
	#endif
#else
	#if defined(HOT_RELOAD_BUILD_SHARED) || defined(HOT_RELOAD_USE_SHARED)
		#define HOT_RELOAD_API __attribute__((visibility("default")))
	#else
		#define HOT_RELOAD_API
	#endif
#endif

/* Marks a single declaration for export under
 * hot_reload_restrict_exports() (cmake/HotReload.cmake) without
 * otherwise changing its linkage: pair with extern "C" yourself for a
 * free function you want a stable, human-typable name for (see this
 * file's own comment on when you'd want that) -- extern "C" first,
 * then this macro, then the return type, e.g.
 * `extern "C" HOT_RELOAD_EXPORT int my_function(...)`, which is the
 * one ordering GCC/Clang accept without a warning here. Leave the
 * extern "C" off to keep a C++ class or method's real mangled name,
 * e.g. a virtual method a host only ever reaches through a vtable
 * slot (see the top-level README's "Hot reloading C++ symbols"
 * section).
 *
 * hot_reload_restrict_exports() is a narrower export surface, not a
 * quieter one: see the README's "Why do std::string/std::vector
 * symbols show up as PATCHED?" section for why it does not, and
 * structurally cannot, suppress those specifically. */
#ifdef _WIN32
	#define HOT_RELOAD_EXPORT __declspec(dllexport)
#else
	#define HOT_RELOAD_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/* Opaque -- see cpp-style-guide.md section 16 ("Do: use opaque
 * pointers ... to hide implementation details"). The real definition
 * is the C++ class hot_reload::HotReloadContext (HotReloadContext.hpp),
 * never visible from this header. */
typedef struct HotReloadContext HotReloadContext;

/* Every fallible function returns one of these -- see section 16
 * ("Do: return an integer error code / status enum from every
 * fallible function"). */
typedef enum HotReloadStatus
{
	HOT_RELOAD_OK = 0,

	/* ctx, module_name, or path/new_path was NULL, or module_name
	 * was not found for hot_reload_reload / hot_reload_get_symbol. */
	HOT_RELOAD_ERROR_INVALID_ARGUMENT,

	/* dlopen()/LoadLibrary() (or the equivalent parse of the file on
	 * disk) failed for the given path. On hot_reload_reload(),
	 * this is non-destructive: the previously loaded version is left
	 * running untouched, exactly as if reload had not been called --
	 * see the file comment above HotReloadContext::reload(). Call
	 * hot_reload_last_error() for the platform's own description of
	 * why -- this one status code covers everything from a missing
	 * file to a genuinely corrupt image to (a real case, found while
	 * building this feature) a shared library with an unresolved
	 * symbol reference, and those need very different fixes. */
	HOT_RELOAD_ERROR_LOAD_FAILED,

	/* hot_reload_reload() was called for a module_name that was never
	 * hot_reload_load()'ed first, so there is no "old" version to
	 * diff against. Call hot_reload_load() once, up front. */
	HOT_RELOAD_ERROR_NO_PREVIOUS_VERSION,

	/* This process's CPU architecture has no patch backend compiled
	 * in (see Architecture.hpp -- currently x86, x86-64, and ARM64).
	 * Nothing was patched; hot_reload_load() still succeeded, you
	 * just cannot hot_reload_reload() on this target. */
	HOT_RELOAD_ERROR_UNSUPPORTED_ARCHITECTURE
} HotReloadStatus;

/* Reported once per matched/unmatched symbol during hot_reload_reload()
 * via HotReloadOptions::on_symbol_event, so the host application can
 * log (or assert on, in a test) exactly what happened -- nothing here
 * is silent. */
typedef enum HotReloadSymbolEvent
{
	/* Existed in both builds and was classified as code in both --
	 * the old function's entry now jumps to the new one. */
	HOT_RELOAD_SYMBOL_PATCHED,

	/* Exists in the new build only. Nothing to patch -- it becomes
	 * reachable the normal way, e.g. via hot_reload_get_symbol(). */
	HOT_RELOAD_SYMBOL_ADDED,

	/* Existed in the old build, missing from the new one. Per
	 * cpp-style-guide.md's spirit of never leaving a caller with an
	 * ambiguous, half-updated state: the old implementation is left
	 * running as-is unless HotReloadOptions::on_deleted_symbol
	 * supplies a stub to redirect to instead -- see that field. */
	HOT_RELOAD_SYMBOL_REMOVED,

	/* Present in both builds by name, but the symbol table classifies
	 * it as data (or something else non-function) in either build --
	 * e.g. an accidentally-exported global. Never touched, in either
	 * direction. This is the mechanical half of the "only executable
	 * code changes" guarantee described at the top of this file. */
	HOT_RELOAD_SYMBOL_SKIPPED_NOT_CODE,

	/* Classified as a function in both builds, but the OLD function's
	 * compiled size is smaller than the number of bytes this
	 * platform's jump sequence needs to write -- patching it would
	 * overwrite whatever comes after it in memory. Skipped rather
	 * than guessed; see hot_reload_enable_patchable_functions() in
	 * cmake/HotReload.cmake for how to stop this from happening. */
	HOT_RELOAD_SYMBOL_SKIPPED_TOO_SMALL,

	/* Classified as a function in both builds, correctly sized, but
	 * the OS refused to make its page temporarily writable (mprotect /
	 * VirtualProtect failed -- e.g. a hardened environment that denies
	 * W^X transitions). Rare in a normal developer setup; reported
	 * rather than silently ignored either way. */
	HOT_RELOAD_SYMBOL_SKIPPED_UNWRITABLE
} HotReloadSymbolEvent;

/* One event report, passed to HotReloadOptions::on_symbol_event.
 * old_address/new_address are NULL when not applicable (e.g.
 * old_address is NULL for HOT_RELOAD_SYMBOL_ADDED). */
typedef struct HotReloadSymbolEventInfo
{
	const char* name;
	HotReloadSymbolEvent kind;
	void* old_address;
	void* new_address;
} HotReloadSymbolEventInfo;

typedef void (*HotReloadSymbolEventFn)(const HotReloadSymbolEventInfo* event, void* user_data);

/* Called instead of patching, once per HOT_RELOAD_SYMBOL_REMOVED
 * event, so the host can catch stale references during development
 * instead of them silently continuing to run old code forever. Return
 * the address of a replacement to redirect the old function to (its
 * only safe use is a fixed-signature stub -- e.g. `void stub(void)`--
 * that logs/asserts; this library has no way to know the deleted
 * function's real signature, so redirecting to anything that reads
 * arguments the caller didn't pass is undefined behavior, exactly
 * like calling any function through the wrong function-pointer type).
 * Return NULL (or pass a NULL callback in HotReloadOptions to begin
 * with) to leave the old implementation running untouched -- the
 * default, and the safe choice unless you specifically want deleted
 * functions to be loud. */
typedef void* (*HotReloadDeletedSymbolFn)(const char* symbol_name, void* user_data);

/* Passed to hot_reload_context_create(); NULL is a valid options
 * pointer meaning "no callbacks, default policy for everything". */
typedef struct HotReloadOptions
{
	HotReloadSymbolEventFn on_symbol_event;     /* nullable */
	HotReloadDeletedSymbolFn on_deleted_symbol; /* nullable */
	void* user_data;                            /* passed back to both callbacks, untouched */
} HotReloadOptions;

/* Caller owns the returned pointer and must call
 * hot_reload_context_destroy() -- see cpp-style-guide.md section 16
 * ("Document ownership transfer explicitly"). options may be NULL. */
HOT_RELOAD_API HotReloadContext* hot_reload_context_create(const HotReloadOptions* options);

HOT_RELOAD_API void hot_reload_context_destroy(HotReloadContext* ctx);

/* Loads path for the first time under module_name -- an arbitrary
 * caller-chosen key used to look the module back up in later
 * hot_reload_reload()/hot_reload_get_symbol() calls; it does not need
 * to match anything on disk. Returns HOT_RELOAD_ERROR_INVALID_ARGUMENT
 * if module_name was already loaded -- call hot_reload_reload()
 * instead of hot_reload_load() again for the same name. */
HOT_RELOAD_API HotReloadStatus hot_reload_load(HotReloadContext* ctx, const char* module_name, const char* path);

/* Loads a NEW build of the module previously registered under
 * module_name, diffs its exported functions against the previous
 * version, and patches every one that is safe to patch in place --
 * see HotReloadSymbolEvent above for exactly what happens to each
 * kind of change. On any HOT_RELOAD_ERROR_* return, nothing was
 * changed: the previous version keeps running exactly as it was. */
HOT_RELOAD_API HotReloadStatus hot_reload_reload(HotReloadContext* ctx, const char* module_name, const char* new_path);

/* Resolves symbol_name in module_name's CURRENT build (i.e. always
 * the freshest one loaded, patched or not) -- for calling a function
 * that did not exist yet when the host first started, such as one
 * reported via HOT_RELOAD_SYMBOL_ADDED. Returns NULL if module_name
 * is unknown or symbol_name is not an exported function in it. */
HOT_RELOAD_API void* hot_reload_get_symbol(HotReloadContext* ctx, const char* module_name, const char* symbol_name);

/* Only meaningful immediately after hot_reload_load() or
 * hot_reload_reload() on this SAME ctx returned something other than
 * HOT_RELOAD_OK: the underlying platform's own description of why --
 * dlerror() on POSIX, the formatted message for GetLastError() on
 * Windows -- e.g. "undefined symbol: _ZN6Widget4tickEi" for a shared
 * library with an unresolved reference (a real, confusing-until-you-
 * see-this-message case: a non-inline static class member that is
 * declared but never given an out-of-class definition compiles fine
 * and links fine into a shared library -- the linker allows a shared
 * library to keep undefined symbols by default -- and only fails,
 * right here, once something ODR-uses it and the loader tries to
 * resolve it for real). Overwritten by the next
 * hot_reload_load()/hot_reload_reload() call on this ctx; copy it out
 * immediately if you need to keep it past that point (e.g. for a log
 * line). Empty if the platform gave no further detail, or if the
 * failure never reached the platform loader at all (e.g. a NULL
 * argument, which is HOT_RELOAD_ERROR_INVALID_ARGUMENT before
 * anything platform-specific happens). A static string internally --
 * never free() this. */
HOT_RELOAD_API const char* hot_reload_last_error(HotReloadContext* ctx);

/* A static string literal -- never free() this. */
HOT_RELOAD_API const char* hot_reload_status_string(HotReloadStatus status);

/* Renders name for a human to read: a real C++ mangled name (Itanium
 * on GCC/Clang/MinGW, MSVC's own scheme under MSVC -- whichever this
 * library itself was built with) becomes something like
 * "Widget::tick(int)"; anything else, including an ordinary extern "C"
 * name, comes back unchanged. Safe to call unconditionally on any
 * HotReloadSymbolEventInfo::name or hot_reload_get_symbol() key --
 * this never changes what name means for lookup purposes, it is only
 * ever applied when you ask for it, purely for display. Returns NULL
 * only if name itself is NULL or allocation fails; caller must free
 * a non-NULL result with hot_reload_free_string(). */
HOT_RELOAD_API char* hot_reload_demangle(const char* name);

HOT_RELOAD_API void hot_reload_free_string(char* str);

#ifdef __cplusplus
}
#endif

#endif /* HOT_RELOAD_H */
