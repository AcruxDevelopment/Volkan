#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace hot_reload::detail
{

	/// One exported symbol found in a loaded module, already classified.
	struct ExportedSymbol
	{
		std::string name;
		void* address = nullptr;

		/// The single load-bearing safety gate behind the "only
		/// executable code changes" guarantee in hot_reload.h: false
		/// for anything not mechanically identified as code (a data
		/// object, a forwarder export, or a symbol kind this reader
		/// does not recognize at all). HotReloadContext never patches
		/// a symbol where this is false, on either side of a reload.
		bool isFunction = false;

		/// Compiled size in bytes if known, 0 if not. ELF gives this
		/// exactly (the symbol table's st_size field); PE gives no
		/// such field, so this holds a conservative estimate derived
		/// from the distance to the next exported function in the
		/// same section -- see the file comment in SymbolTable.cpp
		/// for exactly what that heuristic can and cannot catch.
		std::size_t size = 0;
	};

	/// Reads every exported, defined symbol belonging to the module
	/// mapped at loadedBase (the base address dlopen()/LoadLibrary()
	/// returned for it), classifying each one as code or data.
	///
	/// On POSIX this parses imagePath on disk (its ELF .dynsym /
	/// .dynstr / section-header table) and adds loadedBase as the
	/// runtime load bias. On Windows this ignores imagePath and reads
	/// directly from the already-mapped image at loadedBase (its PE
	/// export directory and section table) -- see the per-platform
	/// implementation blocks in SymbolTable.cpp for why each platform
	/// reads from a different place.
	///
	/// Returns an empty vector, never throws, if the image could not
	/// be parsed as this platform's native format (including the rare
	/// case of a .so with its section header table deliberately
	/// stripped -- see the file comment in SymbolTable.cpp).
	std::vector<ExportedSymbol> readExportedSymbols(const std::string& imagePath, void* loadedBase);

}
