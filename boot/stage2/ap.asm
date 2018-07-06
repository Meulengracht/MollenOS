; MollenOS
;
; Copyright 2018, Philip Meulengracht
;
; This program is free software : you can redistribute it and / or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation ? , either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.If not, see <http://www.gnu.org/licenses/>.
;
; MollenOS Smp Trampoline Code
; Version 1.0
;
BITS 16
ORG TRAMPOLINE_LOCATION

; Definitions
%define __BOOT_SILENCE
%include "systems/defines.inc"

; Jump code to skip over all includes
jmp Entry

; Includes
%include "systems/a20.inc"
%include "systems/gdt.inc"

; ****************************
; 16 Bit Stage Below 
; ****************************
Entry:
	cli
	jmp 	0x0:FixCS ; Far jump to fix segment registers

FixCS:
	; Setup segments
    xor     ax, ax
	mov		ds, ax
	mov		es, ax
	mov		fs, ax
	mov		gs, ax

	; Setup stack
	mov		ss, ax
    mov     ax, word [wStackSize]
	lock xadd word [wStackAddress], ax
	mov		sp, ax
    mov     bp, ax
	xor 	ax, ax
    sti

	; Enable A20 Gate
	call 	EnableA20

	; Install GDT
	call 	InstallGdt

	; GO PROTECTED MODE!
	mov		eax, cr0
	or		eax, 1
	mov		cr0, eax

	; Jump into 32 bit
	jmp 	CODE_DESC:Entry32

; ****************************
; 32 Bit Stage Below
; **************************** 
BITS 32

; 32 Bit Includes
%include "systems/cpu.inc"

Entry32:
	; Disable Interrupts
	cli

	; Setup Segments, Stack etc
	xor 	eax, eax
	mov 	ax, DATA_DESC
	mov 	ds, ax
	mov 	fs, ax
	mov 	gs, ax
	mov 	ss, ax
	mov 	es, ax
	movzx 	esp, bp

	; Setup Cpu
%ifdef __amd64__
	call	CpuDetect64
    cmp     eax, 1
    jne     Skip64BitMode

	; Enable PAE paging
    mov     eax, dword [dSystemPageDirectory]
    mov     cr3, eax
    mov     eax, cr4
    or      eax, 0x20
    mov     cr4, eax

    ; Switch to compatability mode
    mov     ecx, 0xC0000080
    rdmsr
    or      eax, 0x100
    wrmsr

    ; Enable paging
    mov     eax, cr0
    or      eax, 0x80000000
    mov     cr0, eax
    jmp     CODE64_DESC:Entry64
%endif

Skip64BitMode:
	; Enable paging
    mov     eax, dword [dSystemPageDirectory]
    mov     cr3, eax
    mov     eax, cr0
    or      eax, 0x80000000
    mov     cr0, eax

	; Setup Registers
	xor 	esi, esi
	xor 	edi, edi
	mov 	ecx, dword [dKernelAddress]
	jmp 	ecx

	; Safety
EndOfStage:
	cli
	hlt

; ****************************
; 64 Bit Stage Below
; **************************** 
BITS 64
Entry64:
    xor 	eax, eax
	mov 	ax, DATA64_DESC
	mov 	ds, ax
	mov 	fs, ax
	mov 	gs, ax
	mov 	ss, ax
	mov 	es, ax
	movzx 	rsp, bp

    ; Setup Registers
	xor 	rsi, rsi
	xor 	rdi, rdi
    xor     rax, rax
    xor     rbx, rbx
    xor     rcx, rcx
	mov 	ecx, dword [dKernelAddress]
	jmp 	rcx

	; Safety
EndOfStage64:
	cli
	hlt

; **************************
; Variables
; **************************
wStackSize              dw  0x500
wStackAddress           dw  0x1500  ; Base-stack must not be below 0x1000
dSystemPageDirectory    dd  0       ; size - 8
dKernelAddress          dd  0       ; size - 4
