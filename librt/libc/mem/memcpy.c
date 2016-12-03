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

/* Includes */
#include <string.h>
#include <stdint.h>
#include <internal/_string.h>

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif

/* Typedef the memset fingerprint */
typedef void *(*MemCpyTemplate)(void *Destination, const void *Source, size_t Count);

/* Definitions CPUID */
#define CPUID_FEAT_EDX_MMX      1 << 23
#define CPUID_FEAT_EDX_SSE		1 << 25

#define MEMCPY_ACCEL_THRESHOLD	10

/* ASM Externs + Prototypes */
extern void asm_memcpy_mmx(void *Dest, const void *Source, int Loops, int RemainingBytes);
extern void asm_memcpy_sse(void *Dest, const void *Source, int Loops, int RemainingBytes);
void *MemCpyBase(void *Destination, const void *Source, size_t Count);

/* Global */
MemCpyTemplate __GlbMemCpyInstance = NULL;

/* This is the SSE optimized version of memcpy, but there is a fallback
 * to the normal one, in case there isn't enough loops for overhead to be
 * worth it. */
void *MemCpySSE(void *Destination, const void *Source, size_t Count)
{
	/* Loop Count */
	uint32_t SseLoops = Count / 16;
	uint32_t mBytes = Count % 16;

	/* Sanity, we don't want to go through the
	 * overhead if it's less than a certain threshold */
	if (SseLoops < MEMCPY_ACCEL_THRESHOLD) {
		return MemCpyBase(Destination, Source, Count);
	}

	/* Call asm */
	asm_memcpy_sse(Destination, Source, SseLoops, mBytes);
	
	/* Done */
	return Destination;
}

/* This is the MMX optimized version of memcpy, but there is a fallback
 * to the normal one, in case there isn't enough loops for overhead to be
 * worth it. */
void *MemCpyMMX(void *Destination, const void *Source, size_t Count)
{
	/* Loop Count */
	uint32_t MmxLoops = Count / 8;
	uint32_t mBytes = Count % 8;

	/* Sanity, we don't want to go through the
	* overhead if it's less than a certain threshold */
	if (MmxLoops < MEMCPY_ACCEL_THRESHOLD) {
		return MemCpyBase(Destination, Source, Count);
	}

	/* Call asm */
	asm_memcpy_mmx(Destination, Source, MmxLoops, mBytes);

	/* Done */
	return Destination;
}

/* This is the default non-accelerated byte copier, it's optimized
 * for transfering as much as possible, but no CPU acceleration */
void *MemCpyBase(void *Destination, const void *Source, size_t Count)
{
	char *dst = (char*)Destination;
	const char *src = (const char*)Source;
	long *aligned_dst;
	const long *aligned_src;

	/* If the size is small, or either SRC or DST is unaligned,
	then punt into the byte copy loop.  This should be rare.  */
	if (!TOO_SMALL(Count) && !UNALIGNED(src, dst))
	{
		aligned_dst = (long*)dst;
		aligned_src = (long*)src;

		/* Copy 4X long words at a time if possible.  */
		while (Count >= BIGBLOCKSIZE)
		{
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			*aligned_dst++ = *aligned_src++;
			Count -= BIGBLOCKSIZE;
		}

		/* Copy one long word at a time if possible.  */
		while (Count >= LITTLEBLOCKSIZE)
		{
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

/* This is the default, initial routine, it selects the best
 * optimized memcpy for this system. It can be either SSE or MMX
 * or just the byte copier */
void *MemCpySelect(void *Destination, const void *Source, size_t Count)
{
	/* Variables */
	uint32_t CpuFeatEcx = 0;
	uint32_t CpuFeatEdx = 0;

	/* Now extract the cpu information 
	 * so we can select a memcpy */
#ifdef _MSC_VER
	int CpuInfo[4] = { 0 };
	__cpuid(CpuInfo, 1);
	CpuFeatEcx = CpuInfo[2];
#else
	int unused = 0;
	__cpuid(1, unused, unused, CpuFeatEcx, CpuFeatEdx);
#endif

	/* Now do the select */
	if (CpuFeatEdx & CPUID_FEAT_EDX_SSE) {
		__GlbMemCpyInstance = MemCpySSE;
		return MemCpySSE(Destination, Source, Count);
	}
	else if (CpuFeatEdx & CPUID_FEAT_EDX_MMX) {
		__GlbMemCpyInstance = MemCpyMMX;
		return MemCpyMMX(Destination, Source, Count);
	}
	else {
		__GlbMemCpyInstance = MemCpyBase;
		return MemCpyBase(Destination, Source, Count);
	}
}

#ifdef _MSC_VER
#pragma function(memcpy)
#endif

/* The memory copy function 
 * It simply calls the selector */
void* memcpy(void *destination, const void *source, size_t count)
{
	/* Sanity, just in case */
	if (__GlbMemCpyInstance == NULL) {
		__GlbMemCpyInstance = MemCpySelect;
	}

	/* Just return the selected template */
	return __GlbMemCpyInstance(destination, source, count);
}
