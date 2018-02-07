; MollenOS
; Copyright 2011-2016, Philip Meulengracht
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation?, either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <http://www.gnu.org/licenses/>.
;
;
; MollenOS x86 MMX/SSE Memcpy

bits 32
segment .text

;Functions in this asm
global _asm_memcpy_mmx
global _asm_memcpy_sse
global _asm_memcpy_sse2

; void asm_memcpy_mmx(void *Dest, void *Source, int Loops, int RemainingBytes)
; We wait for the spinlock to become free
; then set value to 1 to mark it in use.
_asm_memcpy_mmx:
	; Stack Frame
	push	ebp
	mov		ebp, esp
	
	; Save stuff
	push	edi
	push	esi

	; Get destination/source
	mov		edi, dword [ebp + 8]
	mov		esi, dword [ebp + 12]

	; get loop count
	mov		ecx, dword [ebp + 16]
	mov		edx, dword [ebp + 20]

	; Make sure there is loops to do
	test	ecx, ecx
	je		MmxRemain

MmxLoop:
	movq    mm0, [esi]
	movq	mm1, [esi + 8]
	movq	mm2, [esi + 16]
	movq	mm3, [esi + 24]
	movq	mm4, [esi + 32]
	movq	mm5, [esi + 40]
	movq	mm6, [esi + 48]
	movq	mm7, [esi + 56]

	movq	[edi], mm0
	movq    [edi + 8], mm1
	movq	[edi + 16], mm2
	movq	[edi + 24], mm3
	movq	[edi + 32], mm4
	movq	[edi + 40], mm5
	movq	[edi + 48], mm6
	movq	[edi + 56], mm7

	; Increase Pointers
	add		esi, 64
	add		edi, 64
	dec		ecx

	; Loop Epilogue
	jnz		MmxLoop

	; Done, cleanup MMX
	emms
	
	; Remainders
MmxRemain:
	mov		ecx, edx
	test	ecx, ecx
	je		MmxDone

	; Esi and Edi are already setup
	rep		movsb

MmxDone:
	pop		esi
	pop		edi
	
	; Unwind & return
	pop     ebp
	ret


; void asm_memcpy_sse(void *Dest, void *Source, int Loops, int RemainingBytes)
_asm_memcpy_sse:
	; Stack Frame
	push	ebp
	mov		ebp, esp
	
	; Save stuff
	push	edi
	push	esi

	; Get destination/source
	mov		edi, dword [ebp + 8]
	mov		esi, dword [ebp + 12]

	; get loop counters
	mov		ecx, dword [ebp + 16]
	mov		edx, dword [ebp + 20]
	
	test ecx, ecx
	je		SseRemain

	; Test if buffers are 16 byte aligned
	test	si, 0xF
	jne		UnalignedLoop
	test	di, 0xF
	jne		UnalignedLoop

	; Aligned Loop
AlignedLoop:
	movdqa xmm0, [esi]
    movdqa xmm1, [esi + 16]
    movdqa xmm2, [esi + 32]
    movdqa xmm3, [esi + 48]
    movdqa xmm4, [esi + 64]
    movdqa xmm5, [esi + 80]
    movdqa xmm6, [esi + 96]
    movdqa xmm7, [esi + 112]

    movntdq [edi], xmm0
    movntdq [edi + 16], xmm1
    movntdq [edi + 32], xmm2
    movntdq [edi + 48], xmm3
    movntdq [edi + 64], xmm4
    movntdq [edi + 80], xmm5
    movntdq [edi + 96], xmm6
    movntdq [edi + 112], xmm7

	; Increase Pointers
	add		esi, 128
	add		edi, 128
	dec		ecx

	; Loop Epilogue
	jnz		AlignedLoop
	jmp		SseDone
	
	; Unaligned Loop
UnalignedLoop:
    movdqu xmm0, [esi]
    movdqu xmm1, [esi + 16]
    movdqu xmm2, [esi + 32]
    movdqu xmm3, [esi + 48]
    movdqu xmm4, [esi + 64]
    movdqu xmm5, [esi + 80]
    movdqu xmm6, [esi + 96]
    movdqu xmm7, [esi + 112]

    movdqu [edi], xmm0
    movdqu [edi + 16], xmm1
    movdqu [edi + 32], xmm2
    movdqu [edi + 48], xmm3
    movdqu [edi + 64], xmm4
    movdqu [edi + 80], xmm5
    movdqu [edi + 96], xmm6
    movdqu [edi + 112], xmm7

	; Increase Pointers
	add		esi, 128
	add		edi, 128
	dec		ecx

	; Loop Epilogue
	jnz		UnalignedLoop

SseDone:
	; Done, cleanup MMX
	emms

	; Remainders
SseRemain:
	mov		ecx, edx
	test	ecx, ecx
	je		CpyDone

	; Esi and Edi are already setup
	rep		movsb

CpyDone:
	pop		esi
	pop		edi

	; Unwind & return
	pop     ebp
	ret

; void asm_memcpy_sse2(void *Dest, void *Source, int Loops, int RemainingBytes)
_asm_memcpy_sse2:
	; Stack Frame
	push	ebp
	mov		ebp, esp
	
	; Save stuff
	push	edi
	push	esi

	; Get destination/source
	mov		edi, dword [ebp + 8]
	mov		esi, dword [ebp + 12]

	; get loop counters
	mov		ecx, dword [ebp + 16]
	mov		edx, dword [ebp + 20]
	
	test ecx, ecx
	je		Sse2Remain

	; Test if buffers are 16 byte aligned
	test	si, 0xF
	jne		UnalignedLoop2
	test	di, 0xF
	jne		UnalignedLoop2

	; Aligned Loop
AlignedLoop2:
    prefetchnta [esi + 128]
    prefetchnta [esi + 160]
    prefetchnta [esi + 192]
    prefetchnta [esi + 224]

	movdqa xmm0, [esi]
    movdqa xmm1, [esi + 16]
    movdqa xmm2, [esi + 32]
    movdqa xmm3, [esi + 48]
    movdqa xmm4, [esi + 64]
    movdqa xmm5, [esi + 80]
    movdqa xmm6, [esi + 96]
    movdqa xmm7, [esi + 112]

    movntdq [edi], xmm0
    movntdq [edi + 16], xmm1
    movntdq [edi + 32], xmm2
    movntdq [edi + 48], xmm3
    movntdq [edi + 64], xmm4
    movntdq [edi + 80], xmm5
    movntdq [edi + 96], xmm6
    movntdq [edi + 112], xmm7

	; Increase Pointers
	add		esi, 128
	add		edi, 128
	dec		ecx

	; Loop Epilogue
	jnz		AlignedLoop2
	jmp		Sse2Done
	
	; Unaligned Loop
UnalignedLoop2:
    prefetchnta [esi + 128]
    prefetchnta [esi + 160]
    prefetchnta [esi + 192]
    prefetchnta [esi + 224]

    movdqu xmm0, [esi]
    movdqu xmm1, [esi + 16]
    movdqu xmm2, [esi + 32]
    movdqu xmm3, [esi + 48]
    movdqu xmm4, [esi + 64]
    movdqu xmm5, [esi + 80]
    movdqu xmm6, [esi + 96]
    movdqu xmm7, [esi + 112]

    movdqu [edi], xmm0
    movdqu [edi + 16], xmm1
    movdqu [edi + 32], xmm2
    movdqu [edi + 48], xmm3
    movdqu [edi + 64], xmm4
    movdqu [edi + 80], xmm5
    movdqu [edi + 96], xmm6
    movdqu [edi + 112], xmm7

	; Increase Pointers
	add		esi, 128
	add		edi, 128
	dec		ecx

	; Loop Epilogue
	jnz		UnalignedLoop2

Sse2Done:
	; Done, cleanup MMX
	emms

	; Remainders
Sse2Remain:
	mov		ecx, edx
	test	ecx, ecx
	je		CpyDone2

	; Esi and Edi are already setup
	rep		movsb

CpyDone2:
	pop		esi
	pop		edi

	; Unwind & return
	pop     ebp
	ret