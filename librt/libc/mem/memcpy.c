/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
*
* This program is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation ? , either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.If not, see <http://www.gnu.org/licenses/>.
*
*
* MollenOS - Optimized memory copy
*/

/* Includes 
 * - Library */
#include <string.h>
#include <stdint.h>
#include <internal/_string.h>
#include <stddef.h>

#define CPUID_FEAT_EDX_MMX      1 << 23
#define CPUID_FEAT_EDX_SSE		1 << 25
#define CPUID_FEAT_EDX_SSE2     1 << 26
#define MEMCPY_ACCEL_THRESHOLD	10

/* This is the default non-accelerated byte copier, it's optimized
 * for transfering as much as possible, but no CPU acceleration */
void*
memcpy_base(
    _In_ void *Destination, 
    _In_ const void *Source, 
    _In_ size_t Count)
{
	char *dst = (char*)Destination;
	const char *src = (const char*)Source;
	long *aligned_dst;
	const long *aligned_src;

	/* If the size is small, or either SRC or DST is unaligned,
	then punt into the byte copy loop.  This should be rare.  */
	if (!TOO_SMALL(Count) && !UNALIGNED(src, dst)) {
		aligned_dst = (long*)dst;
		aligned_src = (long*)src;

		/* Copy 4X long words at a time if possible.  */
		while (Count >= BIGBLOCKSIZE) {
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			Count -= BIGBLOCKSIZE;
		}

		/* Copy one long word at a time if possible.  */
		while (Count >= LITTLEBLOCKSIZE) {
			*aligned_dst++ = *aligned_src++;
			Count -= LITTLEBLOCKSIZE;
		}

		/* Pick up any residual with a byte copier.  */
		dst = (char*)aligned_dst;
		src = (char*)aligned_src;
	}

	while (Count--)
		*dst++ = *src++;

	return Destination;
}

// Don't use SSE/MMX instructions in kernel environment
// it's way to fragile on task-switches as we can heavily use memcpy
#ifdef LIBC_KERNEL
#if defined(_MSC_VER) && !defined(__clang__)
#pragma function(memcpy)
#endif
void* memcpy(void *destination, const void *source, size_t count) {
	return memcpy_base(destination, source, count);
}
#elif defined(__amd64__) || defined(amd64)
// Use the sse2 by default as all 64 bit cpus support sse
extern void asm_memcpy_sse2(void *Dest, const void *Source, int Loops, int RemainingBytes);
void *memcpy(void *destination, const void *source, size_t count) {
	int Loops        = count / 128;
	int Remaining    = count % 128;
	if (Loops < MEMCPY_ACCEL_THRESHOLD) {
		return memcpy_base(destination, source, count);
	}
	asm_memcpy_sse2(destination, source, Loops, Remaining);
	return destination;
}
#else
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
typedef void *(*MemCpyTemplate)(void *Destination, const void *Source, size_t Count);
void *memcpy_select(void *Destination, const void *Source, size_t Count);
extern void asm_memcpy_mmx(void *Dest, const void *Source, int Loops, int RemainingBytes);
extern void asm_memcpy_sse(void *Dest, const void *Source, int Loops, int RemainingBytes);
extern void asm_memcpy_sse2(void *Dest, const void *Source, int Loops, int RemainingBytes);
static MemCpyTemplate __GlbMemCpyInstance = memcpy_select;

/* This is the SSE2 optimized version of memcpy, but there is a fallback
 * to the normal one, in case there isn't enough loops for overhead to be
 * worth it. */
void *memcpy_sse2(void *Destination, const void *Source, size_t Count) {
	int Loops        = Count / 128;
	int Remaining    = Count % 128;
	if (Loops < MEMCPY_ACCEL_THRESHOLD) {
		return memcpy_base(Destination, Source, Count);
	}
	asm_memcpy_sse2(Destination, Source, Loops, Remaining);
	return Destination;
}

/* This is the SSE optimized version of memcpy, but there is a fallback
 * to the normal one, in case there isn't enough loops for overhead to be
 * worth it. */
void *memcpy_sse(void *Destination, const void *Source, size_t Count) {
	int Loops        = Count / 128;
	int Remaining    = Count % 128;
	if (Loops < MEMCPY_ACCEL_THRESHOLD) {
		return memcpy_base(Destination, Source, Count);
	}
	asm_memcpy_sse(Destination, Source, Loops, Remaining);
	return Destination;
}

/* This is the MMX optimized version of memcpy, but there is a fallback
 * to the normal one, in case there isn't enough loops for overhead to be
 * worth it. */
void *memcpy_mmx(void *Destination, const void *Source, size_t Count) {
	int MmxLoops    = Count / 64;
	int mBytes      = Count % 64;
	if (MmxLoops < MEMCPY_ACCEL_THRESHOLD) {
		return memcpy_base(Destination, Source, Count);
	}
	asm_memcpy_mmx(Destination, Source, MmxLoops, mBytes);
	return Destination;
}

/* MemCpySelect
 * This is the default, initial routine, it selects the best
 * optimized memcpy for this system. It can be either SSE or MMX
 * or just the byte copier */
void *memcpy_select(void *Destination, const void *Source, size_t Count) {
	// Variables
	int CpuRegisters[4] = { 0 };
	int CpuFeatEcx = 0;
	int CpuFeatEdx = 0;

	// Now extract the cpu information 
	// so we can select a memcpy
#if defined(_MSC_VER) && !defined(__clang__)
	__cpuid(CpuRegisters, 1);
#else
    __cpuid(1, CpuRegisters[0], CpuRegisters[1], CpuRegisters[2], CpuRegisters[3]);
#endif
    // Features are in ecx/edx
    CpuFeatEcx = CpuRegisters[2];
    CpuFeatEdx = CpuRegisters[3];

    // Choose between SSE2, SSE, MMX and base
    if (CpuFeatEdx & CPUID_FEAT_EDX_SSE2) {
		__GlbMemCpyInstance = memcpy_sse2;
		return memcpy_sse2(Destination, Source, Count);
	}
	else if (CpuFeatEdx & CPUID_FEAT_EDX_SSE) {
		__GlbMemCpyInstance = memcpy_sse;
		return memcpy_sse(Destination, Source, Count);
	}
	else if (CpuFeatEdx & CPUID_FEAT_EDX_MMX) {
		__GlbMemCpyInstance = memcpy_mmx;
		return memcpy_mmx(Destination, Source, Count);
	}
	else {
		__GlbMemCpyInstance = memcpy_base;
		return memcpy_base(Destination, Source, Count);
	}
}

#if defined(_MSC_VER) && !defined(__clang__)
#pragma function(memcpy)
#endif
void *memcpy(void *destination, const void *source, size_t count) {
	return __GlbMemCpyInstance(destination, source, count);
}
#endif
