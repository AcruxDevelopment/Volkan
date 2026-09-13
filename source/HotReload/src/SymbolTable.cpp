#include "SymbolTable.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

/* WHY "IS THIS A FUNCTION" IS DECIDED HERE, MECHANICALLY
 * ---------------------------------------------------------
 * hot_reload.h promises that data -- globals, class instances, vtables,
 * anything that isn't executable code -- is never patched. That
 * guarantee is enforced in exactly one place: here. Every symbol this
 * file reports carries a real classification pulled from the loader's
 * own metadata (ELF's symbol type + section flags, or a PE export's
 * containing section's characteristics), never inferred from the
 * symbol's name or any convention the hot-reloadable module is
 * expected to follow. HotReloadContext (the only caller) simply
 * refuses to patch anything this file marks isFunction = false, on
 * either side of a reload -- so nothing downstream has to be trusted
 * to double-check that part.
 */

namespace hot_reload::detail
{

#if defined(_WIN32)

	// ---------------------------------------------------------------
	// PE / Windows: read straight out of the image already mapped at
	// loadedBase. Unlike ELF (below), the OS loader has already fully
	// relocated this image and handed us its exact base address, so
	// there is no separate "load bias" to compute -- every RVA in the
	// headers just gets added directly to loadedBase.
	// ---------------------------------------------------------------

	// NOMINMAX matters concretely in this exact file, not just as a
	// defensive habit: without it, windows.h's own min/max macros
	// substitute into the std::max/std::min calls a few lines below
	// (the preprocessor sees the bare token max after std::, not the
	// qualified name, and expands it regardless) and the file fails to
	// compile on MSVC. Found via an actual Windows build/run of this
	// library, not just documentation.
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>

	namespace
	{
		struct SectionInfo
		{
			std::uint32_t rvaBegin;
			std::uint32_t rvaEnd;
			bool executable;
		};

		const char* rvaToPointer(std::uint8_t* base, std::uint32_t rva)
		{
			return reinterpret_cast<const char*>(base + rva);
		}
	}

	std::vector<ExportedSymbol> readExportedSymbols(const std::string& /*imagePath*/, void* loadedBase)
	{
		std::vector<ExportedSymbol> result;
		if (loadedBase == nullptr)
		{
			return result;
		}

		std::uint8_t* base = static_cast<std::uint8_t*>(loadedBase);
		auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		{
			return result;
		}

		auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE)
		{
			return result;
		}

		// Section table for the executable-section check, following
		// the same "code lives in a section marked executable" rule
		// as the ELF path's SHF_EXECINSTR check below.
		std::vector<SectionInfo> sections;
		const auto* sectionHeaders = IMAGE_FIRST_SECTION(nt);
		for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
		{
			const IMAGE_SECTION_HEADER& section = sectionHeaders[i];
			SectionInfo info;
			info.rvaBegin = section.VirtualAddress;
			info.rvaEnd = section.VirtualAddress + std::max(section.Misc.VirtualSize, section.SizeOfRawData);
			info.executable = (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
			sections.push_back(info);
		}

		auto sectionFor = [&sections](std::uint32_t rva) -> const SectionInfo*
		{
			for (const SectionInfo& section : sections)
			{
				if (rva >= section.rvaBegin && rva < section.rvaEnd)
				{
					return &section;
				}
			}
			return nullptr;
		};

		if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT)
		{
			return result;
		}

		const IMAGE_DATA_DIRECTORY& exportDirEntry = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
		if (exportDirEntry.VirtualAddress == 0 || exportDirEntry.Size == 0)
		{
			return result; // no exports at all
		}

		const auto* exportDir = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + exportDirEntry.VirtualAddress);
		const auto* nameRvas = reinterpret_cast<const DWORD*>(base + exportDir->AddressOfNames);
		const auto* nameOrdinals = reinterpret_cast<const WORD*>(base + exportDir->AddressOfNameOrdinals);
		const auto* functionRvas = reinterpret_cast<const DWORD*>(base + exportDir->AddressOfFunctions);

		const std::uint32_t exportDirBegin = exportDirEntry.VirtualAddress;
		const std::uint32_t exportDirEnd = exportDirEntry.VirtualAddress + exportDirEntry.Size;

		struct Candidate
		{
			std::string name;
			std::uint32_t rva;
			bool isFunction;
		};
		std::vector<Candidate> candidates;

		for (DWORD i = 0; i < exportDir->NumberOfNames; ++i)
		{
			const char* name = rvaToPointer(base, nameRvas[i]);
			WORD ordinal = nameOrdinals[i];
			if (ordinal >= exportDir->NumberOfFunctions)
			{
				continue;
			}
			std::uint32_t functionRva = functionRvas[ordinal];
			if (functionRva == 0)
			{
				continue;
			}

			// A forwarder export's "RVA" actually points at a string
			// like "OtherDll.OtherFunction" living inside the export
			// directory itself, not at real code -- never classified
			// as a patchable function.
			bool isForwarder = functionRva >= exportDirBegin && functionRva < exportDirEnd;

			const SectionInfo* section = sectionFor(functionRva);
			bool isFunction = !isForwarder && section != nullptr && section->executable;

			candidates.push_back(Candidate{name, functionRva, isFunction});
		}

		// Best-effort size estimate: distance to the next function
		// export in the same section. This can OVER-estimate (there
		// may be non-exported code, or alignment padding, between two
		// exported functions, so a large gap does not guarantee a
		// large real function) -- it only ever gives a hard "not
		// enough room" answer when the gap itself is already small.
		// PE has no per-export size field the way ELF does (see
		// below), so /hotpatch (applied automatically by
		// cmake/HotReload.cmake) is the primary safety mechanism on
		// Windows; this is a secondary backstop, not a substitute.
		std::vector<std::uint32_t> functionRvasSorted;
		for (const Candidate& candidate : candidates)
		{
			if (candidate.isFunction)
			{
				functionRvasSorted.push_back(candidate.rva);
			}
		}
		std::sort(functionRvasSorted.begin(), functionRvasSorted.end());

		for (const Candidate& candidate : candidates)
		{
			ExportedSymbol symbol;
			symbol.name = candidate.name;
			symbol.address = base + candidate.rva;
			symbol.isFunction = candidate.isFunction;
			symbol.size = 0;

			if (candidate.isFunction)
			{
				const SectionInfo* section = sectionFor(candidate.rva);
				std::uint32_t upperBound = section != nullptr ? section->rvaEnd : candidate.rva;

				auto it = std::upper_bound(functionRvasSorted.begin(), functionRvasSorted.end(), candidate.rva);
				if (it != functionRvasSorted.end())
				{
					upperBound = std::min(upperBound, *it);
				}
				symbol.size = upperBound > candidate.rva ? (upperBound - candidate.rva) : 0;
			}

			result.push_back(std::move(symbol));
		}

		return result;
	}

#else

	// ---------------------------------------------------------------
	// ELF / POSIX: read imagePath on disk rather than the mapped
	// image. Reading from disk means walking the ordinary section
	// header table (.dynsym / .dynstr, plus every section's sh_flags
	// for the executable check) instead of re-deriving the same
	// information from the .dynamic section's DT_* tags the way the
	// dynamic linker itself does internally -- simpler to get right,
	// at the cost of one known gap: a shared object with its section
	// header table deliberately stripped (rare -- an explicit, extra
	// step beyond an ordinary `strip`, essentially never done to a
	// library you are actively iterating on) will read back as having
	// no exported functions at all, handled the same as any other
	// unparseable image: an empty result, never a crash.
	//
	// st_value in the file is relative to the module's own link-time
	// base, so it is loadedBase (the actual runtime load address --
	// the "load bias" ModuleHandle reads via dlinfo) that turns it
	// into a real, callable address.
	// ---------------------------------------------------------------

	#include <elf.h>

	namespace
	{
		std::vector<char> readWholeFile(const std::string& path)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file)
			{
				return {};
			}
			std::streamsize length = file.tellg();
			if (length <= 0)
			{
				return {};
			}
			file.seekg(0, std::ios::beg);
			std::vector<char> buffer(static_cast<std::size_t>(length));
			if (!file.read(buffer.data(), length))
			{
				return {};
			}
			return buffer;
		}

		/// Parses one ELF class (32 or 64-bit); Ehdr/Shdr/Sym is
		/// Elf32_* or Elf64_* depending on the caller, matched against
		/// e_ident[EI_CLASS] before this is ever instantiated.
		template <typename Ehdr, typename Shdr, typename Sym, typename SymType, typename SymBind>
		std::vector<ExportedSymbol> parseElf(
			const std::vector<char>& file, void* loadedBase, SymType symType, SymBind symBind)
		{
			std::vector<ExportedSymbol> result;
			if (file.size() < sizeof(Ehdr))
			{
				return result;
			}

			const auto* ehdr = reinterpret_cast<const Ehdr*>(file.data());
			if (ehdr->e_shoff == 0 || ehdr->e_shnum == 0)
			{
				return result; // no section header table -- see file comment above
			}
			if (static_cast<std::uint64_t>(ehdr->e_shoff) + static_cast<std::uint64_t>(ehdr->e_shnum) * sizeof(Shdr) >
				file.size())
			{
				return result; // truncated/corrupt file
			}

			const auto* sections = reinterpret_cast<const Shdr*>(file.data() + ehdr->e_shoff);

			// Section header string table, to find sections by name.
			if (ehdr->e_shstrndx >= ehdr->e_shnum)
			{
				return result;
			}
			const char* shstrtab = file.data() + sections[ehdr->e_shstrndx].sh_offset;

			const Shdr* dynsymSection = nullptr;
			const Shdr* dynstrSection = nullptr;
			for (std::size_t i = 0; i < ehdr->e_shnum; ++i)
			{
				const char* name = shstrtab + sections[i].sh_name;
				if (std::strcmp(name, ".dynsym") == 0)
				{
					dynsymSection = &sections[i];
				}
				else if (std::strcmp(name, ".dynstr") == 0)
				{
					dynstrSection = &sections[i];
				}
			}
			if (dynsymSection == nullptr || dynstrSection == nullptr)
			{
				return result;
			}

			const char* dynstr = file.data() + dynstrSection->sh_offset;
			std::size_t symbolCount = dynsymSection->sh_size / sizeof(Sym);
			const auto* symbols = reinterpret_cast<const Sym*>(file.data() + dynsymSection->sh_offset);

			for (std::size_t i = 0; i < symbolCount; ++i)
			{
				const Sym& sym = symbols[i];
				if (sym.st_shndx == SHN_UNDEF || sym.st_name == 0)
				{
					continue; // imported/unresolved, not defined here
				}
				unsigned char bind = symBind(sym.st_info);
				if (bind != STB_GLOBAL && bind != STB_WEAK)
				{
					continue; // not part of this module's visible surface
				}

				ExportedSymbol exported;
				exported.name = dynstr + sym.st_name;
				exported.address = static_cast<char*>(loadedBase) + sym.st_value;
				exported.size = static_cast<std::size_t>(sym.st_size);

				bool isFuncType = symType(sym.st_info) == STT_FUNC;
				bool sectionExecutable = false;
				if (sym.st_shndx != SHN_ABS && sym.st_shndx != SHN_COMMON && sym.st_shndx < ehdr->e_shnum)
				{
					sectionExecutable = (sections[sym.st_shndx].sh_flags & SHF_EXECINSTR) != 0;
				}
				exported.isFunction = isFuncType && sectionExecutable;

				result.push_back(std::move(exported));
			}

			return result;
		}
	}

	std::vector<ExportedSymbol> readExportedSymbols(const std::string& imagePath, void* loadedBase)
	{
		std::vector<char> file = readWholeFile(imagePath);
		if (file.size() < EI_NIDENT)
		{
			return {};
		}

		unsigned char elfClass = static_cast<unsigned char>(file[EI_CLASS]);
		if (elfClass == ELFCLASS64)
		{
			return parseElf<Elf64_Ehdr, Elf64_Shdr, Elf64_Sym>(
				file, loadedBase, [](unsigned char info) { return ELF64_ST_TYPE(info); },
				[](unsigned char info) { return ELF64_ST_BIND(info); });
		}
		if (elfClass == ELFCLASS32)
		{
			return parseElf<Elf32_Ehdr, Elf32_Shdr, Elf32_Sym>(
				file, loadedBase, [](unsigned char info) { return ELF32_ST_TYPE(info); },
				[](unsigned char info) { return ELF32_ST_BIND(info); });
		}
		return {};
	}

#endif

}
