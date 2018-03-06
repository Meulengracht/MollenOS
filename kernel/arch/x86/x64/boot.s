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
; MollenOS x86-64 Boot Code
;
bits 64
segment .text

; Extern main function in C-code
extern _MCoreInitialize

; Publics in this file
global _kentry
global _CpuEnableAvx
global _CpuEnableSse
global _CpuEnableFpu

; No matter what, this is booted by multiboot, and thus
; We can assume the state when this point is reached.
; RAX - Multiboot Magic
; RBX - Contains address of the multiboot structure, but
;		it should be located in stack aswell.

_kentry:
	;We disable interrupts, we have no IDT installed
	cli
    
	;Now, we place multiboot structure and kernel
	;size information on the stack.
	mov rcx, rbx

	;Now call the init function
	call _MCoreInitialize

	;When we return from here, we just
	;enter into an idle loop.
	mov rax, 0x000000000000DEAD

	.idle:
		hlt
		jmp .idle


; Assembly routine to enable avx support
_CpuEnableAvx:
	; Save registers
	push rax
    push rcx
    push rdx

	; Enable
	mov rax, cr4
	bts rax, 14		;Set Operating System Support for Advanced Vector Extensions (Bit 14)
	mov cr4, rax

    ; Modify the extended control register
    mov rcx, 0
    xgetbv     ; Load into RDX:RAX
    or eax, 6
    mov rcx, 0
    xsetbv

	; Restore registers
    pop rdx
    pop rcx
	pop rax
	ret 

; Assembly routine to enable sse support
_CpuEnableSse:
	; Save EAX
	push rax

	; Enable
	mov rax, cr4
	bts rax, 9		;Set Operating System Support for FXSAVE and FXSTOR instructions (Bit 9)
	bts rax, 10		;Set Operating System Support for Unmasked SIMD Floating-Point Exceptions (Bit 10)
	mov cr4, rax

	; Restore EAX
	pop rax
	ret 

; Assembly routine to enable fpu support
_CpuEnableFpu:
	; Save EAX
	push rax

	; Enable
	mov rax, cr0
	bts rax, 1		;Set Monitor co-processor (Bit 1)
	btr rax, 2		;Clear Emulation (Bit 2)
	bts rax, 5		;Set Native Exception (Bit 5)
	btr rax, 3		;Clear TS
	mov cr0, rax

	finit

	; Restore
	pop rax
	ret