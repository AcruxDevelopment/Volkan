/* Demangle.cpp -- hot_reload_demangle() / hot_reload_free_string().
 *
 * Pure string utility, no HotReloadContext involved, so it gets its
 * own file alongside HotReloadApi.cpp rather than crowding that one's
 * single responsibility (the opaque-pointer shim). Part II (C API)
 * style, like everything else in this directory -- see .clang-tidy
 * here and cpp-style-guide.md section 13.
 *
 * Every C++ symbol name in this library's events/lookups is always
 * the REAL symbol-table name (whatever hot_reload_get_symbol() needs
 * to find it again) -- this function is an opt-in convenience for
 * DISPLAYING that name, never applied automatically, so there is
 * never a mismatch between what an event reports and what you'd pass
 * back in to look the symbol up.
 */
#include "hot_reload/hot_reload.h"

#include <cstring>
#include <string>

#if defined(__GNUC__) || defined(__clang__)
	// Covers GCC and Clang on every OS this library targets, MinGW
	// included: what selects the demangling scheme is the C++ ABI the
	// COMPILER implements (Itanium here), not the operating system --
	// MinGW is still GCC/Clang generating Itanium-mangled names even
	// though the OS is Windows. MSVC, on the same OS, uses its own,
	// unrelated scheme (the #elif below). See README.md's "Hot
	// reloading C++ symbols" section for why this distinction matters
	// beyond just this function.
	#include <cxxabi.h>
#elif defined(_MSC_VER)
	#include <dbghelp.h>
	#include <windows.h>
#endif

extern "C"
{

	char* hot_reload_demangle(const char* name)
	{
		if (name == nullptr)
		{
			return nullptr;
		}

		std::string text = name; // fallback: unchanged, for anything that isn't a mangled C++ name at all

		try
		{
#if defined(__GNUC__) || defined(__clang__)
			int status = 0;
			char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
			if (status == 0 && demangled != nullptr)
			{
				text = demangled;
			}
			std::free(demangled); // __cxa_demangle always malloc()s or leaves this null either way
#elif defined(_MSC_VER)
			// UnDecorateSymbolName operates on the string alone (no
			// SymInitialize/session needed, unlike most of
			// DbgHelp) -- but DbgHelp as a whole is documented as not
			// thread-safe, so serialize calls to this function
			// yourself if you call it from more than one thread.
			// Written against Microsoft's documented behavior; unlike
			// most of this library, this specific function was not
			// exercised on real Windows -- see README.md's toolchain
			// verification notes.
			char buffer[2048];
			if (UnDecorateSymbolName(name, buffer, sizeof(buffer), UNDNAME_COMPLETE) != 0)
			{
				text = buffer;
			}
#endif

			char* result = new char[text.size() + 1];
			std::memcpy(result, text.c_str(), text.size() + 1);
			return result;
		}
		catch (...)
		{
			return nullptr;
		}
	}

	void hot_reload_free_string(char* str)
	{
		delete[] str;
	}

}
