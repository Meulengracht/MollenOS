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
global asm_memcpy_sse2

; void asm_memcpy_sse2(void *Dest <rcx>, void *Source <rdx>, int Loops <r8>, int RemainingBytes <r9>)
asm_memcpy_sse2:
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
    prefetchnta [rsi + 128]
    prefetchnta [rsi + 160]
    prefetchnta [rsi + 192]
    prefetchnta [rsi + 224]

    movdqa xmm0, [rsi]
    movdqa xmm1, [rsi + 16]
    movdqa xmm2, [rsi + 32]
    movdqa xmm3, [rsi + 48]
    movdqa xmm4, [rsi + 64]
    movdqa xmm5, [rsi + 80]
    movdqa xmm6, [rsi + 96]
    movdqa xmm7, [rsi + 112]

    movntdq [rdi], xmm0
    movntdq [rdi + 16], xmm1
    movntdq [rdi + 32], xmm2
    movntdq [rdi + 48], xmm3
    movntdq [rdi + 64], xmm4
    movntdq [rdi + 96], xmm6
    movntdq [rdi + 80], xmm5
    movntdq [rdi + 112], xmm7

	; Increase Pointers
	add		rsi, 128
	add		rdi, 128
	dec		rcx

	; Loop Epilogue
	jnz		AlignedLoop
	jmp		SseDone
	
	; Unaligned Loop
UnalignedLoop:
    prefetchnta [rsi + 128]
    prefetchnta [rsi + 160]
    prefetchnta [rsi + 192]
    prefetchnta [rsi + 224]

    movdqu xmm0, [rsi]
    movdqu xmm1, [rsi + 16]
    movdqu xmm2, [rsi + 32]
    movdqu xmm3, [rsi + 48]
    movdqu xmm4, [rsi + 64]
    movdqu xmm5, [rsi + 80]
    movdqu xmm6, [rsi + 96]
    movdqu xmm7, [rsi + 112]

    movdqu [rdi], xmm0
    movdqu [rdi + 16], xmm1
    movdqu [rdi + 32], xmm2
    movdqu [rdi + 48], xmm3
    movdqu [rdi + 64], xmm4
    movdqu [rdi + 80], xmm5
    movdqu [rdi + 96], xmm6
    movdqu [rdi + 112], xmm7

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