; MollenOS
;
; Copyright 2016, Philip Meulengracht
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
; Mollen-OS Stage 2 Bootloader
; Version 1.0
; *******************************************************
; Cpu.inc
; - Contains cpu initialization code
; 

%ifndef _MBOOT_PAGING64_INC_
%define _MBOOT_PAGING64_INC_

; In 64 bit mode a page table is 2mb
%define PAGECOUNT64     512
%define PT64_SIZE       0x200000
%define PD64_SIZE       0x40000000
%define PDP64_SIZE_LOW  0
%define PDP64_SIZE_HIGH 0x00000080

BITS 32

; **************************
; PagingAllocateTable
; @brief Allocates a new memory area of size 0x1000 (4kb)
; that can be used for a paging table. The area is automatically
; zeroed out
; @returns The address of the allocated memory area
; **************************
PagingAllocateTable:
    push edi
    push VBOOT_MEMORY_TYPE_BOOT
    push 0x1000
    call MemoryAllocate
    add esp, 8
    cmp eax, 0
    jz .exit

    cld
    mov edx, eax  ; store for return value
    mov edi, eax
    xor eax, eax
    mov ecx, 0x400
    rep stosd
    mov eax, edx

    .exit:
        pop edi
        ret

; *************************
; PagingIdentityMapPD
; @brief Identity maps the provided PD64 starting from the provided physical
; address
; @param pdPhysical   [0-1, In] The physical address of the PD64
; @param physicalBase [2-3, In] The physical address to start mapping
; @param length       [4-5, In] The length of the memory
; @return                       1 if successful, 0 otherwise
PagingIdentityMapPD:
    STACK_FRAME_BEGIN32
    push esi

    mov esi, dword ARG0_32
    mov eax, dword ARG2_32
    mov edx, dword ARG3_32
    mov ecx, PAGECOUNT64   ; maximum entries in this PD
    .map_pd:
        ; map the entry in the PD64
        mov dword [esi], eax
        mov dword [esi + 4], edx
        or  dword [esi], 0x00000083 ; PAGE_PRESENT | PAGE_WRITE | PAGE_2MB

        ; move on to next
        add esi, 8
        add eax, PT64_SIZE
        adc edx, 0
        
        cmp edx, dword ARG5_32 ; cmp upper 32 bits
        jb .map_pd
        ja .exit
        cmp eax, dword ARG4_32 ; cmp lower 32 bits
        jb .map_pd
        ja .exit
        dec ecx
        jnz .map_pd

    .exit:
        pop esi
        STACK_FRAME_END32
        ret

; *************************
; PagingIdentityMapPDP
; @brief Identity maps the provided PDP64 starting from the provided physical
; address
; @param pdpPhysical  [0-1, In] The physical address of the PDP64
; @param physicalBase [2-3, In] The physical address to start mapping
; @param length       [4-5, In] The length of the memory
; @return                       1 if successful, 0 otherwise
PagingIdentityMapPDP:
    STACK_FRAME_BEGIN32
    sub esp, 8
    push ebx
    push esi

    mov eax, dword ARG2_32
    mov edx, dword ARG3_32
    mov LVAR0_32, eax
    mov LVAR1_32, edx

    mov esi, ARG0_32
    mov ebx, PAGECOUNT64   ; maximum entries in this PD
    .map_pdp:
        call PagingAllocateTable
        cmp eax, 0
        jz .exit

        ; map the entry in the PDP64
        mov dword [esi], eax
        or  dword [esi], 0x00000003 ; PAGE_PRESENT | PAGE_WRITE

        mov eax, dword ARG4_32
        mov edx, dword ARG5_32
        push edx ; upper 32 bits of length
        push eax ; lower 32 bits of length
        
        mov eax, LVAR0_32
        mov edx, LVAR1_32
        push edx ; upper 32 bits of physical address start
        push eax ; lower 32 bits of physical address start

        mov eax, dword [esi]
        and eax, 0xFFFFF000
        push 0   ; upper 32 bits of physical address
        push eax ; push the address of the PD
        
        call PagingIdentityMapPD
        add esp, 24

        ; move on to next
        add esi, 8

        ; add to physical address start
        add LVAR0_32, PD64_SIZE
        adc LVAR1_32, 0
        
        ; compare
        mov eax, LVAR0_32
        mov edx, LVAR1_32
        cmp edx, dword ARG5_32 ; cmp upper 32 bits
        jb .map_pdp
        ja .exit
        cmp eax, dword ARG4_32 ; cmp lower 32 bits
        jb .map_pdp
        ja .exit
        dec ebx
        jnz .map_pdp

    .exit:
        pop esi
        pop ebx
        STACK_FRAME_END32
        ret

; **************************
; PagingInitialize64
; @brief Sets up the long mode page-tables, 2mb pages will be used as we
; identity map all physical memory for speedup.
; @returns 1 for success, 0 otherwise
; **************************
PagingInitialize64:
	STACK_FRAME_BEGIN32
    sub esp, 0x10
    push esi

    ; Get the upper bound of physical memory from the memory system.
    call MemoryGetUpperBound

    ; lvar0/1 - iterator
    ; lvar2/3 - memory size
    mov LVAR0_32, 0
    mov LVAR1_32, 0
    mov LVAR2_32, eax
    mov LVAR3_32, edx

    ; Allocate the PML4 table 
    call PagingAllocateTable
    cmp eax, 0
    jz .exit

    ; store it in CR3 register
    mov cr3, eax
    mov esi, eax
    .fill_pml4:
        call PagingAllocateTable
        cmp eax, 0
        jz .exit

        ; Now we have allocated an entry for the PML4 table, install it
        ; and then identity map the PDP
        mov dword [esi], eax
        or  dword [esi], 0x00000003 ; PAGE_PRESENT | PAGE_WRITE | PAGE_2MB

        mov eax, LVAR2_32
        mov edx, LVAR3_32
        push edx ; upper 32 bits of length
        push eax ; lower 32 bits of length
        
        mov eax, LVAR0_32
        mov edx, LVAR1_32
        push edx ; upper 32 bits of physical address start
        push eax ; lower 32 bits of physical address start

        mov eax, dword [esi]
        and eax, 0xFFFFF000
        push 0   ; upper 32 bits of physical address
        push eax ; push the address of the PDP

        call PagingIdentityMapPDP
        add esp, 24

        ; move on to next
        add esi, 8
        
        ; add to physical address start
        add LVAR1_32, PDP64_SIZE_HIGH

        ; compare
        mov eax, LVAR0_32
        mov edx, LVAR1_32
        cmp edx, LVAR3_32 ; cmp upper 32 bits
        jb .fill_pml4     ; if we are below, then continue in any case
        ja .mapping_done  ; if we are above, then we can break immediately
        cmp eax, LVAR2_32 ; cmp lower 32 bits in case upper 32 bits are equal
        jb .fill_pml4     ; only if we have mapped less than, then continue

    .mapping_done:
        ; Enable PSE and PAE bits in the CR4 register, we assume that the cpu
        ; supports these bits as it supports 64 bit mode.
        mov eax, cr4
        or  eax, 0x10 ; Enable PSE (2mb pages)
        or  eax, 0x20
        mov cr4, eax

        ; Switch to compatability mode
        mov ecx, 0xC0000080
        rdmsr
        or eax, 0x100
        wrmsr

        ; Enable paging
        mov eax, cr0
        or  eax, 0x80000000
        mov cr0, eax

    .exit:
        pop esi
        STACK_FRAME_END32
        ret

%endif
