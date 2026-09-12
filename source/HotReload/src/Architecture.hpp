#pragma once

#include <cstddef>

namespace hot_reload::detail
{

	/// What planPatch() found at one specific function's entry point --
	/// how many bytes from the entry address must be safe to overwrite,
	/// and how many of those (if any) are a landing pad that must be
	/// preserved rather than overwritten. See Architecture.cpp's file
	/// comment for why a landing pad can be present at all.
	struct PatchLayout
	{
		/// Total bytes from the function's entry address that this
		/// patch touches, landing pad included -- compare this against
		/// the symbol's compiled size before writing anything.
		std::size_t totalBytes = 0;

		/// How many leading bytes are a landing pad and must be left
		/// untouched; the jump itself is written starting at
		/// entryAddress + patchOffset.
		std::size_t patchOffset = 0;
	};

	/// True if this build has a jump-patch backend for the CPU
	/// architecture it was compiled for (decided at compile time --
	/// see the top of Architecture.cpp). x86, x86-64, and ARM64 are
	/// supported; anything else (including 32-bit ARM, whose ARM/Thumb
	/// interworking this library does not attempt -- see the README)
	/// returns false here, and HotReloadContext surfaces that as
	/// HOT_RELOAD_ERROR_UNSUPPORTED_ARCHITECTURE.
	bool isArchitectureSupported();

	/// Inspects the bytes already sitting at entryAddress to determine
	/// the real patch layout for THIS function, accounting for a
	/// possible leading landing pad. Safe to call even when
	/// isArchitectureSupported() is false (returns a zeroed PatchLayout).
	PatchLayout planPatch(const void* entryAddress);

	/// Overwrites entryAddress so that control transfers unconditionally
	/// to targetAddress, following the layout planPatch() would return
	/// for this same entryAddress. The caller must already have made
	/// [entryAddress, entryAddress + planPatch(entryAddress).totalBytes)
	/// writable (see MemoryProtection.hpp) and must flush the
	/// instruction cache over that same range afterwards -- this
	/// function does neither.
	///
	/// Only meaningful precondition: the caller has already checked the
	/// target symbol's compiled size against planPatch(entryAddress)
	/// .totalBytes; this function does not re-check it and will happily
	/// overwrite whatever comes next in memory if asked to.
	///
	/// Returns false only if isArchitectureSupported() is false.
	bool writeJump(void* entryAddress, const void* targetAddress);

}
