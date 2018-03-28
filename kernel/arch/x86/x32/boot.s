; MollenOS
;
; Copyright 2011-2014, Philip Meulengracht
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
; MollenOS x86-32 Boot Code
;
bits 32
segment .text

; Extern main function in C-code
extern _MCoreInitialize

; Publics in this file
global _kentry
global _CpuEnableXSave
global _CpuEnableAvx
global _CpuEnableSse
global _CpuEnableFpu

; No matter what, this is booted by multiboot, and thus
; We can assume the state when this point is reached.
; EAX - Multiboot Magic
; EBX - Contains address of the multiboot structure, but
;		it should be located in stack aswell.

_kentry:
	;We disable interrupts, we have no IDT installed
	cli

	;Now, we place multiboot structure and kernel
	;size information on the stack.
	push ebx

	;Now call the init function
	call _MCoreInitialize
	mov eax, 0x0000DEAD

	.idle:
		hlt
		jmp .idle

; Assembly routine to enable xsave support
_CpuEnableXSave:
	mov eax, cr4
	bts eax, 18		; Set Operating System Support for XSave (Bit 18)
	mov cr4, eax

    ; Initialize control register
    mov ecx, 0
    xgetbv     ; Load into EDX:EAX
    or  eax, 6
    mov ecx, 0
    xsetbv
	ret 

; Assembly routine to enable avx support
_CpuEnableAvx:
	mov eax, cr4
	bts eax, 14		;Set Operating System Support for Advanced Vector Extensions (Bit 14)
	mov cr4, eax
	ret 

; Assembly routine to enable sse support
_CpuEnableSse:
	mov eax, cr4
	bts eax, 9		;Set Operating System Support for FXSAVE and FXSTOR instructions (Bit 9)
	bts eax, 10		;Set Operating System Support for Unmasked SIMD Floating-Point Exceptions (Bit 10)
	mov cr4, eax
	ret 

; Assembly routine to enable fpu support
_CpuEnableFpu:
	mov eax, cr0
	bts eax, 1		;Set Monitor co-processor (Bit 1)
	btr eax, 2		;Clear Emulation (Bit 2)
	bts eax, 5		;Set Native Exception (Bit 5)
	btr eax, 3		;Clear TS
	mov cr0, eax

	finit
	ret 