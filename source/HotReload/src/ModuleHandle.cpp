#include "ModuleHandle.hpp"

#include <string>

#if defined(_WIN32)
	// NOMINMAX: see MemoryProtection.cpp's identical comment -- same
	// windows.h, same reason, needed in every translation unit that
	// includes it, not just once for the library as a whole.
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <dlfcn.h>
	#include <link.h>
#endif

namespace hot_reload::detail
{

#if defined(_WIN32)
	namespace
	{
		/// GetLastError() alone is just a number; this renders it the
		/// way a person would want to read it, the same information
		/// dlerror() gives directly on POSIX. Written against
		/// documented Win32 behavior; not exercised on real Windows --
		/// see source/HotReload/README.md's verification notes.
		std::string formatLastWindowsError()
		{
			DWORD errorCode = GetLastError();
			LPSTR buffer = nullptr;
			DWORD length = FormatMessageA(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
				errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&buffer), 0, nullptr);

			std::string message;
			if (length > 0 && buffer != nullptr)
			{
				message.assign(buffer, length);
				while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
				{
					message.pop_back();
				}
			}
			if (buffer != nullptr)
			{
				LocalFree(buffer);
			}
			if (message.empty())
			{
				message = "LoadLibrary failed (error code " + std::to_string(errorCode) + ")";
			}
			return message;
		}
	}
#endif

	ModuleHandle::~ModuleHandle()
	{
		if (m_native != nullptr)
		{
#if defined(_WIN32)
			FreeLibrary(static_cast<HMODULE>(m_native));
#else
			dlclose(m_native);
#endif
		}
	}

	ModuleHandle::ModuleHandle(ModuleHandle&& other) noexcept
		: m_native(other.m_native)
		, m_loadedBase(other.m_loadedBase)
	{
		other.m_native = nullptr;
		other.m_loadedBase = nullptr;
	}

	ModuleHandle& ModuleHandle::operator=(ModuleHandle&& other) noexcept
	{
		if (this != &other)
		{
			if (m_native != nullptr)
			{
#if defined(_WIN32)
				FreeLibrary(static_cast<HMODULE>(m_native));
#else
				dlclose(m_native);
#endif
			}
			m_native = other.m_native;
			m_loadedBase = other.m_loadedBase;
			other.m_native = nullptr;
			other.m_loadedBase = nullptr;
		}
		return *this;
	}

	ModuleHandle ModuleHandle::load(const std::string& path)
	{
		ModuleHandle handle;

#if defined(_WIN32)
		HMODULE module = LoadLibraryA(path.c_str());
		if (module != nullptr)
		{
			handle.m_native = module;
			handle.m_loadedBase = module; // the HMODULE already IS the mapped base on Windows
		}
		else
		{
			handle.m_errorMessage = formatLastWindowsError();
		}
#else
		// RTLD_LOCAL: keep each version's symbols out of the global
		// symbol namespace. Hot reload deliberately resolves symbols
		// itself (see SymbolTable.cpp) rather than through the dynamic
		// linker's own global name lookup, and RTLD_GLOBAL would let
		// an old and a new build of the same plugin -- which export
		// the very same names on purpose -- collide there.
		//
		// RTLD_NOW (eager symbol resolution), not RTLD_LAZY: this
		// means a plugin with a genuinely undefined symbol (e.g. a
		// non-inline static class member declared but never defined,
		// only ODR-used once some previously-dead code path becomes
		// reachable -- a real case found while building this feature)
		// fails right here, cleanly and reportably, INSTEAD of
		// crashing the whole host process the moment that code path
		// first runs under RTLD_LAZY's deferred binding. For a
		// long-running host, a clean, catchable load failure is far
		// preferable to a delayed, uncatchable one.
		void* module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (module == nullptr)
		{
			const char* error = dlerror();
			handle.m_errorMessage = error != nullptr ? error : "dlopen failed with no further detail";
		}
		else
		{
			struct link_map* map = nullptr;
			if (dlinfo(module, RTLD_DI_LINKMAP, &map) == 0 && map != nullptr)
			{
				handle.m_native = module;
				handle.m_loadedBase = reinterpret_cast<void*>(map->l_addr);
			}
			else
			{
				// Captured before dlclose() below, which could
				// otherwise overwrite dlerror()'s state first.
				const char* error = dlerror();
				handle.m_errorMessage =
					error != nullptr ? error : "dlinfo(RTLD_DI_LINKMAP) failed with no further detail";
				dlclose(module);
			}
		}
#endif

		return handle;
	}

}
