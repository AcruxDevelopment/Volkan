#include "MemoryProtection.hpp"

#include <cstdint>

#if defined(_WIN32)
	// NOMINMAX: prevents windows.h from defining min/max macros that
	// would otherwise clash with std::min/std::max and anything else
	// named min/max in this translation unit -- found via an actual
	// Windows build/run of this library, not just documentation.
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <sys/mman.h>
	#include <unistd.h>
#endif

namespace hot_reload::detail
{
	namespace
	{
		std::size_t systemPageSize()
		{
#if defined(_WIN32)
			SYSTEM_INFO info;
			GetSystemInfo(&info);
			return static_cast<std::size_t>(info.dwPageSize);
#else
			long size = sysconf(_SC_PAGESIZE);
			return size > 0 ? static_cast<std::size_t>(size) : 4096;
#endif
		}
	}

	ScopedCodeWriteAccess::ScopedCodeWriteAccess(void* address, std::size_t length)
		: m_pageAddress(nullptr)
		, m_pageLength(0)
		, m_rangeAddress(address)
		, m_rangeLength(length)
		, m_previousProtection(0)
		, m_valid(false)
	{
		const std::size_t page = systemPageSize();
		const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(address);
		const std::uintptr_t alignedStart = start - (start % page);
		const std::size_t frontSlack = static_cast<std::size_t>(start - alignedStart);
		const std::size_t alignedLength = ((frontSlack + length + page - 1) / page) * page;

		m_pageAddress = reinterpret_cast<void*>(alignedStart);
		m_pageLength = alignedLength;

#if defined(_WIN32)
		DWORD oldProtect = 0;
		if (VirtualProtect(m_pageAddress, m_pageLength, PAGE_EXECUTE_READWRITE, &oldProtect))
		{
			m_previousProtection = oldProtect;
			m_valid = true;
		}
#else
		if (mprotect(m_pageAddress, m_pageLength, PROT_READ | PROT_WRITE | PROT_EXEC) == 0)
		{
			m_previousProtection = PROT_READ | PROT_EXEC;
			m_valid = true;
		}
#endif
	}

	ScopedCodeWriteAccess::~ScopedCodeWriteAccess()
	{
		if (!m_valid)
		{
			return;
		}

#if defined(_WIN32)
		DWORD ignored = 0;
		VirtualProtect(m_pageAddress, m_pageLength, static_cast<DWORD>(m_previousProtection), &ignored);
#else
		mprotect(m_pageAddress, m_pageLength, static_cast<int>(m_previousProtection));
#endif
	}

	void ScopedCodeWriteAccess::finishWrite()
	{
		if (!m_valid)
		{
			return;
		}

#if defined(_WIN32)
		FlushInstructionCache(GetCurrentProcess(), m_rangeAddress, m_rangeLength);
#else
		char* begin = static_cast<char*>(m_rangeAddress);
		__builtin___clear_cache(begin, begin + m_rangeLength);
#endif
	}

}
