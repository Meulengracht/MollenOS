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
extern InitializeMachine

; Publics in this file
global kentry
global CpuEnableXSave
global CpuEnableAvx
global CpuEnableSse
global CpuEnableFpu
global CpuEnableGpe

; No matter what, this is booted by vboot, and thus
; We can assume the state when this point is reached.
; RBX - Contains address of the vboot structure.

kentry:
	;We disable interrupts, we have no IDT installed
	cli
    
	; Place the vboot header into first argument for InitializeMachine
	mov rcx, rbx

	;Now call the init function
    sub rsp, 0x20
	call InitializeMachine
	mov rax, 0x000000000000DEAD

	.idle:
		hlt
		jmp .idle

; Assembly routine to enable xsave support
CpuEnableXSave:
    mov rax, cr4
	bts rax, 18		; Set Operating System Support for XSave (Bit 18)
	mov cr4, rax
    
    ; Set the control register
    mov rcx, 0
    xgetbv
    or  eax, 6
    mov rcx, 0
    xsetbv
    ret

; Assembly routine to enable avx support
CpuEnableAvx:
	mov rax, cr4
	bts rax, 14		; Set Operating System Support for Advanced Vector Extensions (Bit 14)
	mov cr4, rax
	ret 

; Assembly routine to enable sse support
CpuEnableSse:
	mov rax, cr4
	bts rax, 9		; Set Operating System Support for FXSAVE and FXSTOR instructions (Bit 9)
	bts rax, 10		; Set Operating System Support for Unmasked SIMD Floating-Point Exceptions (Bit 10)
	mov cr4, rax
	ret 

; Assembly routine to enable fpu support
CpuEnableFpu:
	mov rax, cr0
	bts rax, 1		; Set Monitor co-processor (Bit 1)
	btr rax, 2		; Clear Emulation (Bit 2)
	bts rax, 5		; Set Native Exception (Bit 5)
	btr rax, 3		; Clear TS
	mov cr0, rax

	finit
	ret

; Assembly routine to enable global page support
CpuEnableGpe:
	mov rax, cr4
	bts rax, 7		; Set Operating System Support for Page Global Enable (Bit 7)
	mov cr4, rax
	ret
