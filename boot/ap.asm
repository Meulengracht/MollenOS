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
%include "bios/stage2/systems/defines.inc"

; Jump code to skip over all includes
jmp Entry

; Includes
%include "bios/stage2/systems/a20.inc"
%include "bios/stage2/systems/gdt.inc"

; ****************************
; 16 Bit Stage Below 
; ****************************
Entry:
    cli
    jmp 0x0:FixCS ; Far jump to fix segment registers

FixCS:
    ; Setup segments
    xor ax, ax
    mov	ds, ax
    mov	es, ax
    mov	fs, ax
    mov	gs, ax

    ; Setup stack
    mov	ss, ax
    mov ax, word [wBootStackSize]
    lock xadd word [wBootStackAddress], ax
    mov	sp, ax
    mov bp, ax
    xor ax, ax
    sti

    call A20Enable16
    call GdtInstall

    ; switch to 32 bit mode
    mov	eax, cr0
    or	eax, 1
    mov	cr0, eax
    jmp CODE_DESC:Entry32

; ****************************
; 32 Bit Stage Below
; **************************** 
BITS 32

; 32 Bit Includes
%include "bios/stage2/systems/cpu.inc"

Entry32:
    cli

    ; Setup Segments, Stack etc
    xor eax, eax
    mov ax, DATA_DESC
    mov ds, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov es, ax

    ; initialize 32 bit stack, we reuse the 16 bit stack while deciding on 32/64 bit mode
    mov ax, sp
    xor esp, esp
    mov sp, ax

%ifdef __amd64__
    call CpuDetect64
    test eax, eax
    jz Skip64BitMode
    jmp Switch64
%endif

Skip64BitMode:
    ; switch to new larger permanent stack
    mov eax, dword [dRunStackSize]
    lock xadd dword [dRunStackAddress], eax
    mov ebp, eax
    mov esp, eax

    ; Enable paging
    mov eax, dword [dSystemPageDirectory]
    mov cr3, eax
    mov eax, cr0
    or  eax, 0x80000000
    mov cr0, eax

    ; Setup Registers
    xor esi, esi
    xor edi, edi
    mov ecx, dword [dKernelAddress]
    jmp ecx

    ; Safety
EndOfStage:
    cli
    hlt

; ****************************
; 64 Bit Stage Below
; **************************** 
BITS 64
Switch64:
    ; switch to new larger permanent stack
    mov rax, qword [dRunStackSize]
    lock xadd qword [dRunStackAddress], rax
    mov rbp, rax
    mov rsp, rax

    ; Enable PAE paging
    mov rax, qword [dSystemPageDirectory]
    mov cr3, rax
    mov rax, cr4
    or  rax, 0x20
    mov cr4, rax

    ; Switch to compatability mode
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr

    ; Enable paging
    mov rax, cr0
    or  eax, 0x80000000
    mov cr0, rax

    ; Invoke a faux interrupt return to switch code segment
    ; to 64 bit
    mov rax, rsp
    sub rsp, 16
    mov qword [rsp + 8], DATA64_DESC
    mov qword [rsp], rax
    pushfq
    sub rsp, 16
    mov qword [rsp + 8], CODE64_DESC
    mov rax, Entry64
    mov qword [rsp], rax
    iretq

Entry64:
    xor eax, eax
    mov ax, DATA64_DESC
    mov ds, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov es, ax

    ; Setup Registers
    xor rsi, rsi
    xor rdi, rdi
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    mov rcx, qword [dKernelAddress]
    jmp rcx

    ; Safety
EndOfStage64:
    cli
    hlt

; **************************
; Variables
; **************************
wBootStackSize          dw  0x100
wBootStackAddress       dw  0x1100  ; Base-stack must not be below 0x1000
dRunStackSize           dq  0       ; size - 32
dRunStackAddress        dq  0       ; size - 24
dSystemPageDirectory    dq  0       ; size - 16
dKernelAddress          dq  0       ; size - 8
