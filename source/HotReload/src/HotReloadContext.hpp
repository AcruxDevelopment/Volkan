#pragma once

#include "ModuleHandle.hpp"
#include "SymbolTable.hpp"
#include "hot_reload/hot_reload.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace hot_reload
{

	/// The real type behind the opaque ::HotReloadContext the public C
	/// API hands out -- see HotReloadApi.cpp for the extern "C" shim
	/// that casts between the two, following the same opaque-pointer
	/// PIMPL pattern as cpp-style-guide.md's own NetManager example.
	class HotReloadContext
	{
	public:
		explicit HotReloadContext(const HotReloadOptions* options);

		HotReloadContext(const HotReloadContext&) = delete;
		HotReloadContext& operator=(const HotReloadContext&) = delete;

		/// See hot_reload_load()'s doc comment in hot_reload.h.
		HotReloadStatus load(const std::string& moduleName, const std::string& path);

		/// See hot_reload_reload()'s doc comment in hot_reload.h.
		HotReloadStatus reload(const std::string& moduleName, const std::string& newPath);

		/// See hot_reload_get_symbol()'s doc comment in hot_reload.h.
		void* getSymbol(const std::string& moduleName, const std::string& symbolName) const;

		/// See hot_reload_last_error()'s doc comment in hot_reload.h.
		const std::string& lastError() const
		{
			return m_lastError;
		}

	private:
		/// Everything tracked for one logical module name. Has no
		/// meaning outside HotReloadContext, so it lives here rather
		/// than in its own header -- see cpp-style-guide.md's carve-out
		/// for small types with no use outside their owning type.
		struct LoadedModule
		{
			// Every version ever loaded for this name, kept alive for
			// the rest of the process's lifetime: once a function is
			// patched, anything that already holds its old address
			// must stay valid memory forever, even though it now holds
			// nothing but a jump. versions.back() is the active one.
			std::vector<detail::ModuleHandle> versions;
			std::vector<detail::ExportedSymbol> currentSymbols;
			int reloadCounter = 0;
		};

		std::string stagePathForLoad(LoadedModule& module, const std::string& moduleName, const std::string& sourcePath);
		void patchOne(const detail::ExportedSymbol& oldSymbol, const detail::ExportedSymbol& newSymbol);
		void handleRemoved(const detail::ExportedSymbol& oldSymbol);
		void emitEvent(HotReloadSymbolEvent kind, const std::string& name, void* oldAddress, void* newAddress);

		std::unordered_map<std::string, LoadedModule> m_modules;
		HotReloadSymbolEventFn m_onSymbolEvent;
		HotReloadDeletedSymbolFn m_onDeletedSymbol;
		void* m_userData;
		std::string m_lastError;
	};

}
