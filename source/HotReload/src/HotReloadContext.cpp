#include "HotReloadContext.hpp"

#include "Architecture.hpp"
#include "MemoryProtection.hpp"

#include <cstdio>
#include <fstream>
#include <unordered_set>

namespace hot_reload
{
	namespace
	{
		bool copyFile(const std::string& from, const std::string& to)
		{
			std::ifstream in(from, std::ios::binary);
			if (!in)
			{
				return false;
			}
			std::ofstream out(to, std::ios::binary | std::ios::trunc);
			if (!out)
			{
				return false;
			}
			out << in.rdbuf();
			return static_cast<bool>(out);
		}
	}

	HotReloadContext::HotReloadContext(const HotReloadOptions* options)
		: m_onSymbolEvent(nullptr)
		, m_onDeletedSymbol(nullptr)
		, m_userData(nullptr)
	{
		if (options != nullptr)
		{
			m_onSymbolEvent = options->on_symbol_event;
			m_onDeletedSymbol = options->on_deleted_symbol;
			m_userData = options->user_data;
		}
	}

	void HotReloadContext::emitEvent(HotReloadSymbolEvent kind, const std::string& name, void* oldAddress, void* newAddress)
	{
		if (m_onSymbolEvent == nullptr)
		{
			return;
		}
		HotReloadSymbolEventInfo info;
		info.name = name.c_str();
		info.kind = kind;
		info.old_address = oldAddress;
		info.new_address = newAddress;
		m_onSymbolEvent(&info, m_userData);
	}

	std::string HotReloadContext::stagePathForLoad(
		LoadedModule& module, const std::string& moduleName, const std::string& sourcePath)
	{
		// Every load (first and subsequent) goes through a freshly,
		// uniquely-named copy rather than dlopen()/LoadLibrary()'ing
		// sourcePath directly. Without this, a build that overwrites
		// the SAME path in place for every rebuild (rather than the
		// more common write-temp-then-rename pattern) can hit
		// dlopen's device+inode-based caching and hand back the
		// already-mapped OLD content instead of genuinely reloading --
		// a well-known gotcha for this style of tool. Copying to a
		// name guaranteed never reused sidesteps it regardless of how
		// the build produced sourcePath.
		++module.reloadCounter;
		std::string staged = sourcePath + ".hotreload." + moduleName + "." + std::to_string(module.reloadCounter);
		if (!copyFile(sourcePath, staged))
		{
			return {};
		}
		return staged;
	}

	HotReloadStatus HotReloadContext::load(const std::string& moduleName, const std::string& path)
	{
		m_lastError.clear();

		if (m_modules.find(moduleName) != m_modules.end())
		{
			// Already loaded -- call reload() to load a new build of
			// an existing name, rather than silently discarding
			// whatever was already patched into the first one.
			return HOT_RELOAD_ERROR_INVALID_ARGUMENT;
		}

		LoadedModule module;
		std::string staged = stagePathForLoad(module, moduleName, path);
		if (staged.empty())
		{
			m_lastError = "could not stage '" + path + "' for loading (source unreadable, or destination path unwritable)";
			return HOT_RELOAD_ERROR_LOAD_FAILED;
		}

		detail::ModuleHandle handle = detail::ModuleHandle::load(staged);
		if (!handle.isValid())
		{
			m_lastError = handle.errorMessage();
			std::remove(staged.c_str());
			return HOT_RELOAD_ERROR_LOAD_FAILED;
		}

		module.currentSymbols = detail::readExportedSymbols(staged, handle.loadedBase());
		std::remove(staged.c_str()); // best-effort tidy-up; the mapping survives regardless (see stagePathForLoad)
		module.versions.push_back(std::move(handle));

		m_modules.emplace(moduleName, std::move(module));
		return HOT_RELOAD_OK;
	}

	HotReloadStatus HotReloadContext::reload(const std::string& moduleName, const std::string& newPath)
	{
		m_lastError.clear();

		auto it = m_modules.find(moduleName);
		if (it == m_modules.end())
		{
			return HOT_RELOAD_ERROR_NO_PREVIOUS_VERSION;
		}
		if (!detail::isArchitectureSupported())
		{
			return HOT_RELOAD_ERROR_UNSUPPORTED_ARCHITECTURE;
		}

		LoadedModule& module = it->second;

		std::string staged = stagePathForLoad(module, moduleName, newPath);
		if (staged.empty())
		{
			m_lastError =
				"could not stage '" + newPath + "' for loading (source unreadable, or destination path unwritable)";
			return HOT_RELOAD_ERROR_LOAD_FAILED;
		}

		detail::ModuleHandle newHandle = detail::ModuleHandle::load(staged);
		if (!newHandle.isValid())
		{
			m_lastError = newHandle.errorMessage();
			std::remove(staged.c_str());
			return HOT_RELOAD_ERROR_LOAD_FAILED; // nothing touched -- the previous version keeps running
		}

		std::vector<detail::ExportedSymbol> newSymbols = detail::readExportedSymbols(staged, newHandle.loadedBase());
		std::remove(staged.c_str());

		std::unordered_map<std::string, const detail::ExportedSymbol*> newByName;
		newByName.reserve(newSymbols.size());
		for (const detail::ExportedSymbol& symbol : newSymbols)
		{
			newByName[symbol.name] = &symbol;
		}

		std::unordered_set<std::string> matchedNewNames;
		for (const detail::ExportedSymbol& oldSymbol : module.currentSymbols)
		{
			auto found = newByName.find(oldSymbol.name);
			if (found == newByName.end())
			{
				handleRemoved(oldSymbol);
				continue;
			}

			const detail::ExportedSymbol& newSymbol = *found->second;
			matchedNewNames.insert(oldSymbol.name);

			if (!oldSymbol.isFunction || !newSymbol.isFunction)
			{
				// Present on both sides, but classified as data (or
				// something this library does not recognize as code)
				// on at least one side -- the mechanical half of the
				// "only executable code changes" guarantee. Never
				// touched, in either direction.
				emitEvent(HOT_RELOAD_SYMBOL_SKIPPED_NOT_CODE, oldSymbol.name, oldSymbol.address, newSymbol.address);
				continue;
			}

			patchOne(oldSymbol, newSymbol);
		}

		for (const detail::ExportedSymbol& newSymbol : newSymbols)
		{
			if (matchedNewNames.find(newSymbol.name) == matchedNewNames.end())
			{
				emitEvent(HOT_RELOAD_SYMBOL_ADDED, newSymbol.name, nullptr, newSymbol.address);
			}
		}

		module.currentSymbols = std::move(newSymbols);
		module.versions.push_back(std::move(newHandle));
		return HOT_RELOAD_OK;
	}

	void* HotReloadContext::getSymbol(const std::string& moduleName, const std::string& symbolName) const
	{
		auto it = m_modules.find(moduleName);
		if (it == m_modules.end())
		{
			return nullptr;
		}
		for (const detail::ExportedSymbol& symbol : it->second.currentSymbols)
		{
			if (symbol.name == symbolName)
			{
				return symbol.address;
			}
		}
		return nullptr;
	}

	void HotReloadContext::patchOne(const detail::ExportedSymbol& oldSymbol, const detail::ExportedSymbol& newSymbol)
	{
		detail::PatchLayout layout = detail::planPatch(oldSymbol.address);
		if (oldSymbol.size != 0 && oldSymbol.size < layout.totalBytes)
		{
			emitEvent(HOT_RELOAD_SYMBOL_SKIPPED_TOO_SMALL, oldSymbol.name, oldSymbol.address, newSymbol.address);
			return;
		}

		detail::ScopedCodeWriteAccess writable(oldSymbol.address, layout.totalBytes);
		if (!writable.isValid())
		{
			emitEvent(HOT_RELOAD_SYMBOL_SKIPPED_UNWRITABLE, oldSymbol.name, oldSymbol.address, newSymbol.address);
			return;
		}

		detail::writeJump(oldSymbol.address, newSymbol.address);
		writable.finishWrite();
		emitEvent(HOT_RELOAD_SYMBOL_PATCHED, oldSymbol.name, oldSymbol.address, newSymbol.address);
	}

	void HotReloadContext::handleRemoved(const detail::ExportedSymbol& oldSymbol)
	{
		// Ask for a stub at most once, and only for an old symbol that
		// was itself real code -- there is no sensible "redirect" for
		// a removed data symbol. A returned NULL (including because no
		// callback was supplied at all) means the default: leave the
		// old implementation running untouched.
		void* stub = nullptr;
		if (oldSymbol.isFunction && m_onDeletedSymbol != nullptr)
		{
			stub = m_onDeletedSymbol(oldSymbol.name.c_str(), m_userData);
		}

		if (stub != nullptr)
		{
			detail::PatchLayout layout = detail::planPatch(oldSymbol.address);
			bool sizeOk = oldSymbol.size == 0 || oldSymbol.size >= layout.totalBytes;
			if (sizeOk)
			{
				detail::ScopedCodeWriteAccess writable(oldSymbol.address, layout.totalBytes);
				if (writable.isValid())
				{
					detail::writeJump(oldSymbol.address, stub);
					writable.finishWrite();
				}
				// If the OS refused write access, or the function was
				// too small to safely hold the stub redirect, this
				// silently falls back to leaving the old implementation
				// running -- the same safe default as not supplying a
				// stub at all, rather than adding a second kind of
				// event on top of HOT_RELOAD_SYMBOL_REMOVED below for
				// what is already a rare, best-effort extra.
			}
		}

		emitEvent(HOT_RELOAD_SYMBOL_REMOVED, oldSymbol.name, oldSymbol.address, nullptr);
	}

}
