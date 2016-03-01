/*
 *     STRING
 *     Copying
 */

/* Includes */
#include <string.h>
#include <stdint.h>
#include <internal/_string.h>
#include <cpuid.h>

/* Definitions CPUID */
#define CPUID_FEAT_EDX_MMX      1 << 23
#define CPUID_FEAT_EDX_SSE		1 << 25

//Feats
uint32_t CpuFeatEcx = 0;
uint32_t CpuFeatEdx = 0;

/* ASM Externs */
extern void asm_memcpy_mmx(void *Dest, const void *Source, int Loops, int RemainingBytes);
extern void asm_memcpy_sse(void *Dest, const void *Source, int Loops, int RemainingBytes);

void* memcpy_sse(void *destination, const void *source, size_t count)
{
	/* Loop Count */
	uint32_t SseLoops = count / 16;
	uint32_t mBytes = count % 16;

	/* Call asm */
	asm_memcpy_sse(destination, source, SseLoops, mBytes);
	
	/* Done */
	return destination;
}

void* memcpy_mmx(void *destination, const void *source, size_t count)
{
	/* Loop Count */
	uint32_t MmxLoops = count / 8;
	uint32_t mBytes = count % 8;

	/* Call asm */
	asm_memcpy_mmx(destination, source, MmxLoops, mBytes);

	/* Done */
	return destination;
}

#pragma function(memcpy)
void* memcpy(void *destination, const void *source, size_t count)
{
	/* Sanity */
	if(CpuFeatEcx == 0 && CpuFeatEdx == 0)
	{
		int unused = 0;
		__cpuid(1, unused, unused, CpuFeatEcx, CpuFeatEdx);
	}

	//Can we use SSE?
	if(CpuFeatEdx & CPUID_FEAT_EDX_SSE)
		return memcpy_sse(destination, source, count);
	else if(CpuFeatEdx & CPUID_FEAT_EDX_MMX)
		return memcpy_mmx(destination, source, count);
	else
	{
		char *dst = (char*)destination;
		const char *src = (const char*)source;
		long *aligned_dst;
		const long *aligned_src;

		/* If the size is small, or either SRC or DST is unaligned,
			then punt into the byte copy loop.  This should be rare.  */
		if (!TOO_SMALL(count) && !UNALIGNED (src, dst))
		{
			aligned_dst = (long*)dst;
			aligned_src = (long*)src;

			/* Copy 4X long words at a time if possible.  */
			while (count >= BIGBLOCKSIZE)
			{
				*aligned_dst++ = *aligned_src++;
				*aligned_dst++ = *aligned_src++;
				*aligned_dst++ = *aligned_src++;
				*aligned_dst++ = *aligned_src++;
				count -= BIGBLOCKSIZE;
			}

			/* Copy one long word at a time if possible.  */
			while (count >= LITTLEBLOCKSIZE)
			{
				*aligned_dst++ = *aligned_src++;
				count -= LITTLEBLOCKSIZE;
			}

			/* Pick up any residual with a byte copier.  */
			dst = (char*)aligned_dst;
			src = (char*)aligned_src;
		}

		while (count--)
		*dst++ = *src++;

		return destination;
	}
}