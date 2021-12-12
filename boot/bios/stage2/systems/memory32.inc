; MollenOS
;
; Copyright 2011 - 2016, Philip Meulengracht
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
; Memory.inc
; - Contains routines for detecting available memory
; 

%ifndef _MBOOT_MEMORY32_INC_
%define _MBOOT_MEMORY32_INC_

BITS 32

MemoryMoveMapDown:
    STACK_FRAME_BEGIN32


    STACK_FRAME_END32
    ret

; bMemoryEntryCount - Number of firmware entries starting from MEMLOCATION_MEMORY_MAP
; Entries are encoded as VBootMemoryEntry's
; VBootMemoryEntry_size variable is the size

MemoryAllocateFixed:
    ; Find region that contains the address wanted, then split it into two/three parts.
    ; remember to update the number of memory region.
    ret

; **********************
; @brief Allocates a block of memory from the memory map, marks it as _FIRMWARE and
; always allocates memory from top to bottom.
; @param Size [In] The size of the memory block to allocate.
; @return The address of the allocated memory block.
MemoryAllocateFirmware:
    STACK_FRAME_BEGIN32
    push esi

    ; Allocate from last entry that is marked as _AVAILABLE. We then create modify that
    ; entry to be marked as _FIRMWARE, and move the entire memory map one entry down.
    ; remember to update the number of memory region.
    mov esi, MEMLOCATION_MEMORY_MAP
    
    ; calculate offset into memory map, clear out registers
    xor edx, edx
    mov eax, VBootMemoryEntry_size
    mul byte [bMemoryEntryCount]
    add esi, eax

    ; esi now points to end of memory map, subtract size of entry
    sub esi, VBootMemoryEntry_size
    xor eax, eax
    mov al, byte [bMemoryEntryCount]
    dec al
    .is_available:
        cmp dword [esi], VBOOT_MEMORY_TYPE_AVAILABLE
        jne .next
        cmp dword [esi + VBootMemoryEntry.PhysicalBase + 4], 0
        jne .next
        jmp .split_entry

        .next:
            sub esi, VBootMemoryEntry_size
            cmp esi, MEMLOCATION_MEMORY_MAP
            jae .is_available
            
            ; we're at the end of the memory map, so we can't allocate
            xor eax, eax
            jmp .exit

    .split_entry:
        push 1    ; move down once
        push eax  ; push from position
        call MemoryMoveMapDown
        add esp, 8

        ; Change length to be the size of the new entry
        mov dword [esi + VBootMemoryEntry.Length], 0
        mov dword [esi + VBootMemoryEntry.Length + 4], 0

        ; Change type to be _FIRMWARE
        mov dword [esi + VBootMemoryEntry.Type], VBOOT_MEMORY_TYPE_FIRMWARE

        ; Return the address of the new entry
        mov eax, dword [esi + VBootMemoryEntry.PhysicalBase]

    .exit:
        STACK_FRAME_END32
        ret

MemoryFreeFirmware:
    ; Hopefully we can merge entry with the previous or next entry, and then move the
    ; memory map. If not, then we keep it as a free entry seperately by just changing
    ; type from _FIRMWARE to _AVAILABLE.
    ret

%endif
