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
; MollenOS x86-64 SSE Memcpy

bits 64
segment .text

;Functions in this asm
global _asm_memcpy_sse

; void asm_memcpy_sse(void *Dest, void *Source, int Loops, int RemainingBytes)
; We set the spinlock to value 0
_asm_memcpy_sse:
	; Save stuff
	push	rdi
	push	rsi

	; Get destination/source
	mov		rdi, rcx
	mov		rsi, rdx

	; get loop count
	mov		rcx, r8
	mov		rdx, r9
	
	test    rcx, rcx
	je		SseRemain

	; Test if buffers are 16 byte aligned
	test	si, 0xF
	jne		UnalignedLoop
	test	di, 0xF
	jne		UnalignedLoop

	; Aligned Loop
AlignedLoop:
	movaps	xmm0, [rsi]
	movaps	[rdi], xmm0

	; Increase Pointers
	add		rsi, 16
	add		rdi, 16

	; Loop Epilogue
	dec		rcx							      
	jnz		AlignedLoop
	jmp		SseDone
	
	; Unaligned Loop
UnalignedLoop:
	movups	xmm0, [rsi]
	movups	[rdi], xmm0

	; Increase Pointers
	add		rsi, 16
	add		rdi, 16

	; Loop Epilogue
	dec		rcx							      
	jnz		UnalignedLoop

SseDone:
	; Done, cleanup MMX
	emms

	; Remainders
SseRemain:
	mov		rcx, rdx
	test	rcx, rcx
	je		CpyDone

	; Esi and Edi are already setup
	rep		movsb

CpyDone:
	pop		rsi
	pop		rdi
	ret