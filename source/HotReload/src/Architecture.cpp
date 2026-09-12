#include "Architecture.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>

/* WHY THERE CAN BE A "LANDING PAD" IN FRONT OF A FUNCTION
 * -----------------------------------------------------------
 * Modern toolchains can emit a marker instruction as the very first
 * instruction of a function, required by a CPU-level indirect-branch
 * protection feature: x86 CET ("Control-flow Enforcement Technology",
 * ENDBR64/ENDBR32) and ARM64 BTI ("Branch Target Identification").
 * When the CPU enforces either, an indirect call/jump (which is
 * exactly how a call through a function pointer, or a call across a
 * shared-library boundary via the PLT/GOT, actually reaches a
 * function) is only allowed to land on one of these marker
 * instructions -- landing anywhere else raises a fault.
 *
 * This matters here because it is genuinely on by default in
 * mainstream environments, not a rare opt-in: this exact toolchain
 * (Ubuntu 24.04's system GCC 13, x86-64) emits ENDBR64 at every
 * function's entry with no extra flags beyond -fpatchable-function-entry,
 * confirmed empirically while building this library --
 * `gcc -O2 -fpatchable-function-entry=16,0` alone was enough to produce
 * an `endbr64` as the very first instruction, ahead of the requested
 * NOP padding. Overwriting it unconditionally would silently work on
 * a CPU/kernel combination that does not enforce IBT and silently
 * crash on one that does -- exactly the kind of environment-dependent
 * failure this library exists to avoid.
 *
 * So every backend below first checks the live bytes at the function's
 * entry for its architecture's landing-pad encoding and, if present,
 * treats it as a fixed, immovable prefix: the jump is written
 * immediately after it rather than over it. Since a landing pad is
 * defined to otherwise behave as a NOP, falling through it into our
 * jump is always correct whether or not the CPU actually enforces the
 * protection it marks.
 *
 * WRITE ORDERING (AND WHAT IT DOES / DOES NOT PROTECT AGAINST)
 * -----------------------------------------------------------
 * Every backend below writes the target-address bytes first and the
 * leading opcode/instruction word that "arms" the jump last, with a
 * release fence in between. This protects the realistic case this
 * library is designed for: hot_reload_reload() runs at a quiescent
 * point (e.g. your main thread between frames) where nothing is
 * actively calling INTO the function while it is being patched, and
 * this ordering guarantees that any call arriving right at that
 * boundary sees either the fully-old or the fully-new jump, never a
 * target address that is half-old-half-new. It does NOT make the
 * write safe against a thread that is concurrently, actively
 * executing through the exact bytes being patched -- doing that in
 * general on a variable-length ISA needs a signal-handler-based retry
 * protocol (a much bigger mechanism, well beyond a dev-loop tool's
 * requirements). See the README for this precondition stated plainly.
 */

namespace hot_reload::detail
{
	namespace
	{
#if defined(__x86_64__) || defined(_M_X64)

		constexpr std::size_t kLandingPadBytes = 4;
		constexpr unsigned char kEndbr64[kLandingPadBytes] = {0xF3, 0x0F, 0x1E, 0xFA};
		// jmp qword ptr [rip+0]; <8-byte absolute address> -- reaches
		// any 64-bit address regardless of how far apart the old and
		// new modules were loaded, unlike a 5-byte relative jump
		// (E9 rel32) which only reaches +/-2GB. Verified byte-for-byte
		// against `as`/objdump output while building this library.
		constexpr std::size_t kJumpBytes = 14;

		bool hasLandingPad(const unsigned char* entry)
		{
			return std::memcmp(entry, kEndbr64, kLandingPadBytes) == 0;
		}

		void writeJumpAt(unsigned char* site, const void* target)
		{
			std::uint64_t targetValue = 0;
			std::memcpy(&targetValue, &target, sizeof(target));

			// Payload first: the absolute address (site+6) and the
			// constant zero rip-displacement (site+2) are only ever
			// consumed as DATA once the opcode below is in place, so
			// writing them early cannot be misinterpreted as a
			// different, fresh instruction stream by anything that
			// might fall through this region in its old, all-NOP state.
			std::memcpy(site + 6, &targetValue, sizeof(targetValue));
			std::memcpy(site + 2, "\x00\x00\x00\x00", 4);
			std::atomic_thread_fence(std::memory_order_release);

			// Arm last: until these 2 bytes land, site+0 still reads as
			// whatever it was (a NOP, on first patch; a previous FF 25
			// on a later reload -- either way harmless/inert).
			unsigned char opcode[2] = {0xFF, 0x25};
			std::memcpy(site, opcode, sizeof(opcode));
			std::atomic_thread_fence(std::memory_order_release);
		}

#elif defined(__i386__) || defined(_M_IX86)

		constexpr std::size_t kLandingPadBytes = 4;
		constexpr unsigned char kEndbr32[kLandingPadBytes] = {0xF3, 0x0F, 0x1E, 0xFB};
		// e9 rel32 -- a plain relative near jump. On 32-bit x86 the
		// entire address space fits in the signed 32-bit displacement
		// (mod 2^32 wraparound gives the right answer regardless of
		// "distance"), so unlike x86-64 there is no need for an
		// absolute/indirect form here.
		constexpr std::size_t kJumpBytes = 5;

		bool hasLandingPad(const unsigned char* entry)
		{
			return std::memcmp(entry, kEndbr32, kLandingPadBytes) == 0;
		}

		void writeJumpAt(unsigned char* site, const void* target)
		{
			std::int32_t rel32 = static_cast<std::int32_t>(
				reinterpret_cast<std::intptr_t>(target) - reinterpret_cast<std::intptr_t>(site) - 5);

			// Payload (the displacement) first, opcode byte last --
			// same reasoning as the x86-64 backend above.
			std::memcpy(site + 1, &rel32, sizeof(rel32));
			std::atomic_thread_fence(std::memory_order_release);
			site[0] = 0xE9;
			std::atomic_thread_fence(std::memory_order_release);
		}

#elif defined(__aarch64__) || defined(_M_ARM64)

		constexpr std::size_t kLandingPadBytes = 4;

		// Matches any BTI variant (plain/C/J/JC): bits [31:6] are fixed
		// at 0b010101000000001001001, differing only in bits [6:5]
		// (the qualifier) -- equivalently, the low byte is one of
		// 0x1F/0x5F/0x9F/0xDF and the next 3 bytes are always
		// 24 03 D5. Verified with `as`/objdump for all 4 variants
		// while building this library.
		bool isBti(const unsigned char* entry)
		{
			return (entry[0] & 0x3Fu) == 0x1Fu && entry[1] == 0x24 && entry[2] == 0x03 && entry[3] == 0xD5;
		}

		bool hasLandingPad(const unsigned char* entry)
		{
			return isBti(entry);
		}

		// ldr x17, #8 ; br x17 ; <8-byte absolute address>. x16/x17
		// are AArch64's designated "intra-procedure-call" scratch
		// registers (IP0/IP1) -- the AAPCS64 explicitly reserves them
		// for exactly this kind of linker/veneer-style long branch, so
		// clobbering them at a function's entry point, before any of
		// its real prologue has executed, is always safe. Byte values
		// verified with `as`/objdump while building this library.
		constexpr std::size_t kJumpBytes = 16;
		constexpr unsigned char kLdrX17Pc8[4] = {0x51, 0x00, 0x00, 0x58};
		constexpr unsigned char kBrX17[4] = {0x20, 0x02, 0x1F, 0xD6};

		void writeJumpAt(unsigned char* site, const void* target)
		{
			std::uint64_t targetValue = 0;
			std::memcpy(&targetValue, &target, sizeof(target));

			// Payload (the literal address) first. ARM64 instructions
			// are fixed-width and naturally aligned, so -- unlike the
			// variable-length x86 case -- each 4-byte slot here is
			// unambiguous: it is either the old NOP, the old
			// instruction, or the new one, never a byte-misaligned
			// mix. Writing high-to-low keeps the two real instructions
			// (which is what any in-flight fall-through NOP execution
			// could actually reach) inert for as long as possible.
			std::memcpy(site + 8, &targetValue, sizeof(targetValue));
			std::atomic_thread_fence(std::memory_order_release);
			std::memcpy(site + 4, kBrX17, sizeof(kBrX17));
			std::atomic_thread_fence(std::memory_order_release);

			// Arm last: until this word lands, site+0 still reads as a
			// NOP (or a previous ldr, on a later reload) and execution
			// never reaches the br at site+4 via fall-through.
			std::memcpy(site, kLdrX17Pc8, sizeof(kLdrX17Pc8));
			std::atomic_thread_fence(std::memory_order_release);
		}

#endif
	}

	bool isArchitectureSupported()
	{
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86) || defined(__aarch64__) || \
	defined(_M_ARM64)
		return true;
#else
		return false;
#endif
	}

	PatchLayout planPatch(const void* entryAddress)
	{
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86) || defined(__aarch64__) || \
	defined(_M_ARM64)
		const unsigned char* entry = static_cast<const unsigned char*>(entryAddress);
		PatchLayout layout;
		layout.patchOffset = hasLandingPad(entry) ? kLandingPadBytes : 0;
		layout.totalBytes = layout.patchOffset + kJumpBytes;
		return layout;
#else
		(void) entryAddress;
		return PatchLayout{};
#endif
	}

	bool writeJump(void* entryAddress, const void* targetAddress)
	{
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86) || defined(__aarch64__) || \
	defined(_M_ARM64)
		PatchLayout layout = planPatch(entryAddress);
		unsigned char* site = static_cast<unsigned char*>(entryAddress) + layout.patchOffset;
		writeJumpAt(site, targetAddress);
		return true;
#else
		(void) entryAddress;
		(void) targetAddress;
		return false;
#endif
	}

}
