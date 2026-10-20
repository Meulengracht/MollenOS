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
; along with this program. If not, see <http://www.gnu.org/licenses/>.
;
; MollenOS Smp Trampoline Code

; Relocatable SIPI page. CS contains the startup vector shifted by eight.
; No firmware calls or stack memory are used before paging is enabled.
BITS 16
ORG 0

Entry:
    cli
    cld

    ; align cs and ds segments
    mov   ax, cs
    mov   ds, ax

    ; initialize the storage, as we are loaded
    ; from a relocatable address, CS has the startup
    ; vector that we shift by 8
    movzx ebx, ax
    shl   ebx, 4

    ; fixup gdt
    lea eax, [ebx + Gdt]
    mov [GdtPointer + 2], eax

    ; fixup entry for 32 bit
    lea eax, [ebx + Entry32]
    mov [ProtectedEntry], eax

%ifdef __amd64__
    ; fixup entry for 64 bit
    lea eax, [ebx + Entry64]
    mov [LongEntry], eax
%endif

    ; load the gdt
    lgdt [GdtPointer]

    ; switch on protected mode
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    ; jump into 32 bit space
    jmp dword far [ProtectedEntry]

BITS 32
Entry32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

%ifdef __amd64__
    ; These instructions must be encoded as 32-bit until CS.L is set.
    mov eax, cr4
    or  eax, 0x20 ; CR4.PAE
    mov cr4, eax
%endif

    mov eax, [ebx + dSystemPageDirectory]
    mov cr3, eax
    
%ifdef __amd64__
    mov ecx, 0xC0000080
    rdmsr
    or  eax, 0x100       ; EFER.LME
    wrmsr
%endif

    mov eax, cr0
    and eax, 0x9FFFFFFF  ; clear reset-state CR0.CD/NW (enable caching)
    or  eax, 0x80010000  ; CR0.PG/WP; the SIPI page is identity mapped
    mov cr0, eax

%ifdef __amd64__
    jmp far [ebx + LongEntry]

BITS 64
Entry64:
    ; initialize all the segments
    mov eax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; allocate stack space, and we do it locked to
    ; avoid clashing with other cpus
    mov rax, [rel dRunStackSize]
    lock xadd [rel dRunStackAddress], rax

    ; setup the stack
    mov rsp, rax
    and rsp, -16
    xor ebp, ebp

    ; x64 ABI home space; CALL supplies return slot
    sub rsp, 32
    call [rel dKernelAddress]
%else
    ; allocate stack space, and we do it locked to
    ; avoid clashing with other cpus
    mov  eax, [ebx + dRunStackSize]
    lock xadd [ebx + dRunStackAddress], eax
    
    ; setup the stack
    mov esp, eax
    and esp, -16
    xor ebp, ebp

    ; jump to the kernel
    call [ebx + dKernelAddress]
%endif

Halted:
    cli
    hlt
    jmp Halted

align 8
Gdt:
    dq 0
    dq 0x00CF9a000000FFFF  ; 0x08: flat 32-bit code
    dq 0x00CF92000000FFFF  ; 0x10: flat data
    dq 0x00209A0000000000  ; 0x18: 64-bit code
GdtEnd:
GdtPointer:
    dw GdtEnd - Gdt - 1
    dd 0

ProtectedEntry:
    dd 0
    dw 0x08

%ifdef __amd64__
LongEntry:
    dd 0
    dw 0x18
%endif

; Fixed trailer shared with InitializeApplicationJumpSpace. 
; Keep last and aligned.
align 8
dRunStackSize           dq 0
dRunStackAddress        dq 0
dSystemPageDirectory    dq 0
dKernelAddress          dq 0
