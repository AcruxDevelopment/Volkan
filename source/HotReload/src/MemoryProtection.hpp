#pragma once

#include <cstddef>

namespace hot_reload::detail
{

	/// RAII guard: makes [address, address + length) writable for its
	/// lifetime (the OS only grants permissions in whole pages, so the
	/// guard rounds out to the enclosing page range internally) and
	/// restores the original protection when it goes out of scope.
	///
	/// Assumes the range started out READ+EXECUTE, as every platform's
	/// loader maps a .text/code section -- restoring to exactly that,
	/// rather than querying and restoring whatever the previous
	/// protection actually was, since POSIX mprotect() has no portable
	/// way to ask for the current protection of a range and code pages
	/// are never anything else in practice.
	class ScopedCodeWriteAccess
	{
	public:
		ScopedCodeWriteAccess(void* address, std::size_t length);
		~ScopedCodeWriteAccess();

		ScopedCodeWriteAccess(const ScopedCodeWriteAccess&) = delete;
		ScopedCodeWriteAccess& operator=(const ScopedCodeWriteAccess&) = delete;

		/// False if the platform call to relax protection failed --
		/// when false, nothing was changed and the range must not be
		/// written to (the destructor is still safe to run either way).
		bool isValid() const
		{
			return m_valid;
		}

		/// Call exactly once, after writing the new bytes and before
		/// this guard is destroyed: flushes the instruction cache over
		/// the original [address, address + length) range so every
		/// core sees the new instructions rather than a stale fetch
		/// (required on ARM64, where instruction and data caches are
		/// not automatically coherent; effectively a formality on
		/// x86/x86-64, called anyway for correctness on that
		/// architecture too).
		void finishWrite();

	private:
		void* m_pageAddress;
		std::size_t m_pageLength;
		void* m_rangeAddress;
		std::size_t m_rangeLength;
		unsigned long m_previousProtection;
		bool m_valid;
	};

}
