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
; MollenOS x86-64 SSE2 Memcpy

bits 64
segment .text

;Functions in this asm
global _asm_memcpy_sse2

; void asm_memcpy_sse2(void *Dest, void *Source, int Loops, int RemainingBytes)
_asm_memcpy_sse2:
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
    movntdq [edi + 96], xmm6
    movntdq [edi + 80], xmm5
    movntdq [edi + 112], xmm7

	; Increase Pointers
	add		rsi, 128
	add		rdi, 128
	dec		rcx

	; Loop Epilogue
	jnz		AlignedLoop
	jmp		SseDone
	
	; Unaligned Loop
UnalignedLoop:
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
	add		rsi, 128
	add		rdi, 128
	dec		rcx

	; Loop Epilogue
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