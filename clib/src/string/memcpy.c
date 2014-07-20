/*
 *     STRING
 *     Copying
 */

#include <string.h>
#include <stdint.h>
#include <internal/_string.h>

#define CPUID_FEAT_EDX_MMX      1 << 23
#define CPUID_FEAT_EDX_SSE		1 << 25

//Feats
uint32_t CpuFeatEcx = 0;
uint32_t CpuFeatEdx = 0;

void* memcpy_sse(void *destination, const void *source, size_t count)
{
	/* Pointers */
	uint32_t *Dest = (uint32_t*)destination;
	uint32_t *Source = (uint32_t*)source;
	
	/* Loop Count */
	uint32_t SseLoops = count / 16;
	uint32_t mBytes = count % 16;

	/* Assembly Train */
	_asm 
	{
		/* Setup Registers / Loop Prologue */
		pushad
		mov		edi, [Dest]
		mov		esi, [Source]
		mov		ecx, [SseLoops]
		test	ecx, ecx
		je		SseRemain

		/* Test if buffers are 16 byte aligned */
		test si, 0xF
		jne UnalignedLoop
		test di, 0xF
		jne UnalignedLoop

	/* Aligned Loop */
	AlignedLoop:
		movaps	xmm0, [esi]
		movaps	[edi], xmm0

		/* Increase Pointers */
		add esi, 16
		add edi, 16

		/* Loop Epilogue */
		dec	ecx							      
		jnz AlignedLoop
		jmp	SseDone
	
	/* Unaligned Loop */
	UnalignedLoop:
		movups	xmm0, [esi]
		movups	[edi], xmm0

		/* Increase Pointers */
		add esi, 16
		add edi, 16

		/* Loop Epilogue */
		dec	ecx							      
		jnz UnalignedLoop

	SseDone:
		/* Done, cleanup MMX */
		emms

		/* Remainders */
	SseRemain:
		mov ecx, [mBytes]
		test ecx, ecx
		je CpyDone

		/* Esi and Edi are already setup */
		rep movsb

	CpyDone:
		popad
	}

	return destination;
}

void* memcpy_mmx(void *destination, const void *source, size_t count)
{
	/* Pointers */
	uint32_t *Dest = (uint32_t*)destination;
	uint32_t *Source = (uint32_t*)source;

	/* Loop Count */
	uint32_t MmxLoops = count / 8;
	uint32_t mBytes = count % 8;

	/* Assembly Train */
	_asm 
	{
		/* Setup Registers / Loop Prologue */
		pushad
		mov		edi, [Dest]
		mov		esi, [Source]
		mov		ecx, [MmxLoops]
		test	ecx, ecx
		je		MmxRemain

	MmxLoop:
		movq	mm0, [esi]
		movq	[edi], mm0

		/* Increase Pointers */
		add esi, 8
		add edi, 8

		/* Loop Epilogue */
		dec	ecx							      
		jnz MmxLoop

		/* Done, cleanup MMX */
		emms

		/* Remainders */
	MmxRemain:
		mov ecx, [mBytes]
		test ecx, ecx
		je MmxDone

		/* Esi and Edi are already setup */
		rep movsb

	MmxDone:
		popad
	}

	return destination;
}

#pragma function(memcpy)
void* memcpy(void *destination, const void *source, size_t count)
{
	/* Sanity */
	if(CpuFeatEcx == 0 && CpuFeatEdx == 0)
	{
		_asm {
			mov	eax, 1
			cpuid
			mov	[CpuFeatEcx], ecx
			mov	[CpuFeatEdx], edx
		}
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