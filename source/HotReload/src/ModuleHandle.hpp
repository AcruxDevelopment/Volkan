#pragma once

#include <string>

namespace hot_reload::detail
{

	/// RAII wrapper around dlopen()/LoadLibrary(). Move-only, closes
	/// the underlying module on destruction (or on being replaced by a
	/// move-assignment).
	class ModuleHandle
	{
	public:
		ModuleHandle() = default;
		~ModuleHandle();

		ModuleHandle(const ModuleHandle&) = delete;
		ModuleHandle& operator=(const ModuleHandle&) = delete;
		ModuleHandle(ModuleHandle&& other) noexcept;
		ModuleHandle& operator=(ModuleHandle&& other) noexcept;

		/// Loads path. On failure the returned handle's isValid() is
		/// false and loadedBase() is null.
		static ModuleHandle load(const std::string& path);

		bool isValid() const
		{
			return m_native != nullptr;
		}

		/// The address to pass as SymbolTable's loadedBase: on Windows
		/// this is simply the HMODULE; on POSIX it is the runtime load
		/// bias (struct link_map::l_addr) that turns an ELF symbol's
		/// file-relative st_value into a real address.
		void* loadedBase() const
		{
			return m_loadedBase;
		}

		/// Only meaningful when isValid() is false: the underlying
		/// platform's own description of why loading failed --
		/// dlerror() on POSIX, the formatted message for
		/// GetLastError() on Windows. Captured immediately inside
		/// load()'s failing branch, before any other dl*/Win32 call
		/// that could otherwise overwrite it first. Empty if load()
		/// was never attempted, or if the platform gave no message.
		const std::string& errorMessage() const
		{
			return m_errorMessage;
		}

	private:
		void* m_native = nullptr;
		void* m_loadedBase = nullptr;
		std::string m_errorMessage;
	};

}
