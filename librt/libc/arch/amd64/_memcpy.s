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
	movq	mm0, [esi]
	movq	[edi], mm0

	; Increase Pointers
	add		esi, 8
	add		edi, 8

	; Loop Epilogue
	dec		ecx							      
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
; We set the spinlock to value 0
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

	; get loop count
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
	movaps	xmm0, [esi]
	movaps	[edi], xmm0

	; Increase Pointers
	add		esi, 16
	add		edi, 16

	; Loop Epilogue
	dec		ecx							      
	jnz		AlignedLoop
	jmp		SseDone
	
	; Unaligned Loop
UnalignedLoop:
	movups	xmm0, [esi]
	movups	[edi], xmm0

	; Increase Pointers
	add		esi, 16
	add		edi, 16

	; Loop Epilogue
	dec		ecx							      
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