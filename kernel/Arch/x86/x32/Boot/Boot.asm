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
extern _init

; Publics in this file
global _kentry
global _CpuEnableSse
global _CpuEnableFpu
global _CpuId

; No matter what, this is booted by multiboot, and thus
; We can assume the state when this point is reached.
; EAX - Multiboot Magic
; EBX - Contains address of the multiboot structure, but
;		it should be located in stack aswell.
; EDX - Should contain size of the kernel file

_kentry:
	;We disable interrupts, we have no IDT installed
	cli

	;If any important information has been passed to 
	;us through the stack, we need to save it now.

	;Setup a new stack to an unused
	;place in memory. This will be temporary.
	mov eax, 0x10
	mov ss, ax					
	mov esp, 0x9F000
	mov ebp, esp

	;Now, we place multiboot structure and kernel
	;size information on the stack.
	push edx
	push ebx

	;Now call the init function
	call _init

	;When we return from here, we just
	;enter into an idle loop.
	mov eax, 0x0000DEAD
	
	.idle:
		hlt
		jmp .idle


; Assembly routine to enable
; sse support
_CpuEnableSse:
	; Save EAX
	push eax

	; Enable
	mov eax, cr4
	bts eax, 9		;Set Operating System Support for FXSAVE and FXSTOR instructions (Bit 9)
	bts eax, 10		;Set Operating System Support for Unmasked SIMD Floating-Point Exceptions (Bit 10)
	mov cr4, eax

	; Restore EAX
	pop eax
	ret 

; Assembly routine to enable
; fpu support
_CpuEnableFpu:
	; Save EAX
	push eax

	; Enable
	mov eax, cr0
	bts eax, 1		;Set Monitor co-processor (Bit 1)
	btr eax, 2		;Clear Emulation (Bit 2)
	bts eax, 5		;Set Native Exception (Bit 5)
	btr eax, 3		;Clear TS
	mov cr0, eax

	finit

	; Restore
	pop eax
	ret 

; Assembly routine to get
; cpuid information
; void cpuid(uint32_t cpuid, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
_CpuId:
	; Stack Frame
	push ebp
	mov ebp, esp
	pushad

	; Get CPUID
	mov eax, [ebp + 8]
	cpuid
	mov edi, [ebp + 12]
	mov [edi], eax
	mov edi, [ebp + 16]
	mov [edi], ebx
	mov edi, [ebp + 20]
	mov [edi], ecx
	mov edi, [ebp + 24]
	mov [edi], edx

	; Release stack frame
	popad
	pop ebp
	ret 