# HotReload.cmake
#
# One function: hot_reload_enable_patchable_functions(<target>).
# Call it on any SHARED library target whose exported functions you
# intend to hot-reload with source/HotReload. It applies whichever
# compiler flag guarantees enough safely-overwritable space at each
# function's entry point for this library's jump-patch backends
# (source/HotReload/src/Architecture.cpp) to write into -- without it,
# HotReloadContext::reload() falls back to the compiled function's
# symbol size (ELF only -- see SymbolTable.cpp) and silently skips
# (HOT_RELOAD_SYMBOL_SKIPPED_TOO_SMALL) anything too small, rather than
# corrupting whatever comes after it in memory, but a target built
# with this flag applied should never actually hit that fallback.
#
# 32 bytes covers every backend with margin: ARM64's worst case is a
# 4-byte BTI landing pad (only present if the target enables ARM
# branch protection) plus a 16-byte jump sequence = 20 bytes; x86-64's
# is a 4-byte ENDBR64 landing pad (on by default on some toolchains --
# see Architecture.cpp's file comment) plus its 14-byte jump = 18;
# plain x86 needs at most 4 + 5 = 9. 32 leaves headroom over the
# largest of those without meaningfully affecting code size -- this is
# NOP padding, skipped in a few cycles by the branch predictor on
# every call until a function is actually reloaded.
function(hot_reload_enable_patchable_functions TARGET_NAME)
	if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		# M (the "before entry" count) is deliberately 0: all 32 NOPs
		# land AT and after the function's address, which is exactly
		# where source/HotReload writes into. Confirmed against
		# `objdump` output while building this library -- see the
		# design note this function's file comment links back to.
		target_compile_options(${TARGET_NAME} PRIVATE -fpatchable-function-entry=32,0)
	elseif(MSVC)
		# MSVC's own equivalent: reserves a 2-byte pad at every
		# function's entry plus additional padding immediately before
		# it, specifically so an external tool can overwrite the entry
		# with a short jump. See
		# https://learn.microsoft.com/en-us/cpp/build/reference/hotpatch
		# This does not take a byte count the way -fpatchable-function-
		# entry does; the PE export table also gives this library no
		# per-symbol size the way ELF's symbol table does (see
		# SymbolTable.cpp's file comment), so on MSVC this flag is not
		# just recommended but the primary safety mechanism -- there is
		# no runtime fallback check backing it up here the way there is
		# on GCC/Clang/ELF.
		target_compile_options(${TARGET_NAME} PRIVATE /hotpatch)
	else()
		message(WARNING
			"hot_reload_enable_patchable_functions(${TARGET_NAME}): "
			"unrecognized compiler '${CMAKE_CXX_COMPILER_ID}', no patchable-entry "
			"flag applied. Hot-reloading ${TARGET_NAME} will still work for any "
			"function whose compiled body happens to be large enough on its own; "
			"see HOT_RELOAD_SYMBOL_SKIPPED_TOO_SMALL in hot_reload.h.")
	endif()
endfunction()

# Restricts TARGET's export surface to only what's explicitly marked
# with HOT_RELOAD_EXPORT (hot_reload.h) -- the opposite of both
# GCC/Clang's default (export every symbol with external linkage) and
# WINDOWS_EXPORT_ALL_SYMBOLS. Narrower, not quieter: see the top-level
# README's "Why do std::string/std::vector symbols show up as
# PATCHED?" section for the one specific kind of noise this
# deliberately does NOT remove, and why removing it would need
# excluding all weak-linkage symbols by default -- which would just as
# happily hide a hot-reloadable template of your OWN.
#
# What this DOES remove: your own internal helpers you never meant to
# expose, and (on GCC/Clang, if the target statically links its C++
# runtime) incidental runtime-library internals -- both real,
# observed cases from building this library and its examples.
function(hot_reload_restrict_exports TARGET_NAME)
	if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		target_compile_options(${TARGET_NAME} PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
	elseif(MSVC)
		# MSVC already exports nothing by default -- WINDOWS_EXPORT_ALL_SYMBOLS
		# is what opts OUT of that; simply not setting it (and not adding
		# individual __declspec(dllexport)/HOT_RELOAD_EXPORT other than where
		# you want them) is this function's Windows equivalent. Nothing to
		# apply here, but calling this on every platform your target builds
		# for, unconditionally, is still the point -- see hot_reload_enable_
		# patchable_functions() immediately above for the same pattern.
	endif()
endfunction()
