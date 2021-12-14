; MollenOS
;
; Copyright 2021, Philip Meulengracht
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

; *********************
; MemoryMoveMapDown
; @brief Moves all entries past the given index a full entry down
; in memory. This acts like a memmove where it copies the data in a reverse
; fashion to allow for move in-place.
; @param Index [0, In] The index to start moving from
; @param Count [1, In] The number of entries to move
; @return <none>
MemoryMoveMapDown:
    STACK_FRAME_BEGIN32
    push esi
    push edi

    ; Calculate size 
    ; (VBoot->Memory.NumberOfEntries - i) * sizeof(struct VBootMemoryEntry)
    xor eax, eax
    xor edx, edx
    mov al, byte [bMemoryEntryCount]
    sub eax, ARG0_32
    mov ecx, VBootMemoryEntry_size
    mul ecx
    push eax

    ; Calculate ith entry
    mov eax, ARG0_32
    xor edx, edx
    mov ecx, VBootMemoryEntry_size
    mul ecx
    pop ecx

    ; Calculate source position
    ; &VBoot->Memory.Entries[i] + size - 1
    mov esi, MEMLOCATION_MEMORY_MAP
    add esi, eax ; get ith entry
    add esi, ecx ; add size
    dec esi      ; - 1

    ; Calculate destination position
    ; &VBoot->Memory.Entries[i + 1] + size - 1 
    mov edi, MEMLOCATION_MEMORY_MAP
    add edi, eax                   ; get ith entry
    add edi, VBootMemoryEntry_size ; get i+1th entry
    add edi, ecx                   ; add size
    dec edi                        ; - 1

    std       ; set direction to backwards
    rep movsb
    cld       ; set direction to forwards again

    pop edi
    pop esi
    STACK_FRAME_END32
    ret

; *********************
; MemoryMoveMapUp
; @brief Moves all entries past the given index a full entry up
; in memory. This acts like a memmove where it copies the data in a forward
; fashion to allow for move in-place.
; @param dest   [0, In] The number of entries to move
; @param source [1, In] The source to move from
; @param index  [2, In] The index to start moving from
MemoryMoveMapUp:
    STACK_FRAME_BEGIN32
    push esi
    push edi

    mov edi, ARG0_32
    mov esi, ARG1_32

    ; Calculate size from current index
    ; bMemoryEntryCount - index - 1
    xor eax, eax
    mov al, byte [bMemoryEntryCount]
    sub eax, ARG2_32
    dec eax
    
    xor edx, edx
    mov ecx, VBootMemoryEntry_size
    mul ecx
    mov ecx, eax

    cld
    rep movsb

    pop edi
    pop esi
    STACK_FRAME_END32
    ret

; *********************
; MemoryAllocateFixed
; @brief Allocates a fixed physical memory address from the available memory.
; @param Address [0, In] The physical address to allocate
; @param Size    [1, In] The size of the allocation
; @return                0 on error, 1 on success
MemoryAllocateFixed:
    STACK_FRAME_BEGIN32
    push esi

    ; Find region that contains the address wanted, then split it into two/three parts.
    ; remember to update the number of memory region.
    mov esi, MEMLOCATION_MEMORY_MAP
    xor ecx, ecx
    mov cl, byte [bMemoryEntryCount]
    
    .check_entry:
        cmp dword [esi], VBOOT_MEMORY_TYPE_AVAILABLE
        jne .next_entry

        ; Check if the address is within start of region
        mov eax, ARG0_32
        mov edx, dword [esi + VBootMemoryEntry.PhysicalBase]
        cmp eax, edx
        jb .next_entry ; actually we could exit if the base is higher than the address

        add edx, dword [esi + VBootMemoryEntry.Length]
        cmp eax, edx
        jae .next_entry

        ; ok so our address is within the region, now we need to verify
        ; that the size is also within the region.
        add eax, ARG1_32
        cmp eax, edx
        ja .next_entry
        je .change_entry

        ; Ok, we reached here, we need to split it. Three cases, either we are splitting
        ; from the front, the middle or the back. 
        mov eax, ARG0_32
        mov edx, dword [esi + VBootMemoryEntry.PhysicalBase]
        cmp eax, edx
        ja .split_middle
        
        .split_front:
            ; calculate index by subtracting current from start
            mov ch, cl
            mov cl, byte [bMemoryEntryCount]
            sub cl, ch
            mov ch, 0

            push 1    ; move down once
            push ecx  ; push from current position
            call MemoryMoveMapDown
            add esp, 8

            ; Change length to be the size of the new entry
            mov ecx, ARG1_32
            mov dword [esi + VBootMemoryEntry.Length], ecx

            ; Change type to be _FIRMWARE
            mov dword [esi + VBootMemoryEntry.Type], VBOOT_MEMORY_TYPE_FIRMWARE
            mov eax, dword [esi + VBootMemoryEntry.PhysicalBase] ; store base address

            ; Adjust the following entry's length and physical base
            add esi, VBootMemoryEntry_size
            add dword [esi + VBootMemoryEntry.PhysicalBase], ecx
            sub dword [esi + VBootMemoryEntry.Length], ecx

            ; increase the number of entries
            add byte [bMemoryEntryCount], 1

            ; set success
            mov eax, 1
            jmp .exit

        .split_middle:
            ; check if we are splitting from back
            add eax, ARG1_32
            add edx, dword [esi + VBootMemoryEntry.Length]
            cmp eax, edx
            je .split_back

            ; calculate index by subtracting current from start
            mov ch, cl
            mov cl, byte [bMemoryEntryCount]
            sub cl, ch
            mov ch, 0

            ; otherwise we are splitting from the middle which means
            ; we need to move the memory map two spots down. One for the 
            ; existing segment, one for the allocated segment, and one for
            ; the remaining segment
            push 2    ; move down twice
            push ecx  ; push from current position
            call MemoryMoveMapDown
            add esp, 8

            push ebx ; we must save ebx as its a preserved register
            push edi
            
            ; ith entry = existing segment
            ; from base ... to arg0
            mov ebx, dword [esi + VBootMemoryEntry.Length] ; save original length
            mov edi, dword [esi + VBootMemoryEntry.Length + 4]

            mov eax, dword [esi + VBootMemoryEntry.PhysicalBase]
            mov edx, ARG0_32
            sub edx, eax
            mov dword [esi + VBootMemoryEntry.Length], edx
            mov dword [esi + VBootMemoryEntry.Length + 4], 0

            ; subtract ith length from original length
            sub ebx, edx
            sbb edi, 0

            ; i+1th entry = allocated segment
            ; from arg0 ... to arg0+arg1
            add esi, VBootMemoryEntry_size
            mov eax, ARG0_32
            mov edx, ARG1_32
            mov dword [esi + VBootMemoryEntry.Type], VBOOT_MEMORY_TYPE_FIRMWARE
            mov dword [esi + VBootMemoryEntry.PhysicalBase], eax
            mov dword [esi + VBootMemoryEntry.Length], edx
            mov dword [esi + VBootMemoryEntry.Length + 4], 0
            mov dword [esi + VBootMemoryEntry.Attributes], 0xF

            ; subtract (i+1)th length from original length
            sub ebx, edx
            sbb edi, 0

            ; i+2th entry = remaining segment
            ; from arg0+arg1 ... to length - rest of lengths
            add esi, VBootMemoryEntry_size
            add eax, edx
            mov dword [esi + VBootMemoryEntry.Type], VBOOT_MEMORY_TYPE_AVAILABLE
            mov dword [esi + VBootMemoryEntry.PhysicalBase], eax
            mov dword [esi + VBootMemoryEntry.Length], ebx
            mov dword [esi + VBootMemoryEntry.Length + 4], edi

            pop edi ; restore both preserved registers again
            pop ebx

            ; increase the number of entries
            add byte [bMemoryEntryCount], 2

            ; set success
            mov eax, 1
            jmp .exit

        .split_back:
            ; add extra region to the back, easy peasy
            ; start by reducing length of this one
            sub dword [esi + VBootMemoryEntry.Length], eax
            sbb dword [esi + VBootMemoryEntry.Length + 4], 0

            ; save the physical base, and add length
            mov ecx, dword [esi + VBootMemoryEntry.PhysicalBase]
            mov edx, dword [esi + VBootMemoryEntry.PhysicalBase + 4]
            
            add ecx, dword [esi + VBootMemoryEntry.Length]
            adc edx, dword [esi + VBootMemoryEntry.Length + 4]

            ; move to next entry
            add esi, VBootMemoryEntry_size

            ; initialize entry with new physical base and length
            mov dword [esi + VBootMemoryEntry.Type], VBOOT_MEMORY_TYPE_FIRMWARE
            mov dword [esi + VBootMemoryEntry.PhysicalBase], ecx
            mov dword [esi + VBootMemoryEntry.PhysicalBase + 4], edx
            mov dword [esi + VBootMemoryEntry.Length], eax
            mov dword [esi + VBootMemoryEntry.Length + 4], 0
            mov dword [esi + VBootMemoryEntry.Attributes], 0xF

            ; increase the number of entries
            add byte [bMemoryEntryCount], 1

            ; set success
            mov eax, 1
            jmp .exit

        .change_entry:
            ; Entry just so happened to match both start and size... weird
            ; no need to split, just mark it _FIRMWARE
            mov dword [esi + VBootMemoryEntry.Type], VBOOT_MEMORY_TYPE_FIRMWARE
            mov eax, 1
            jmp .exit

        .next_entry:
            add esi, VBootMemoryEntry_size
            dec ecx
            jnz .check_entry
            xor eax, eax ; set 0 to indicate error

    .exit:
        pop esi
        STACK_FRAME_END32
        ret

; **********************
; MemoryAllocate
; @brief Allocates a block of memory from the memory map, marks it as type provided and
; always allocates memory from top to bottom.
; @param Size [0, In] The size of the memory block to allocate.
; @param Type [1, In] The type of memory to allocate.
; @return The address of the allocated memory block.
MemoryAllocate:
    STACK_FRAME_BEGIN32
    push esi

    ; page align the size
    mov eax, ARG0_32
    mov ecx, eax
    and eax, 0x00000FFF
    jz .size_aligned

    and ecx, 0xFFFFF000 
    add ecx, 0x1000

    .size_aligned:
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
        dec al ; go from 0..n-1
    
    .is_available:
        ; entry must be available
        cmp dword [esi], VBOOT_MEMORY_TYPE_AVAILABLE
        jne .next_entry

        ; physical base must be below 4gb
        cmp dword [esi + VBootMemoryEntry.PhysicalBase + 4], 0
        jne .next_entry

        ; length must be larger or equal to size
        cmp ecx, dword [esi + VBootMemoryEntry.Length]
        ja .next_entry
        je .change_entry
        jmp .split_entry

        .next_entry:
            sub esi, VBootMemoryEntry_size
            dec eax
            test eax, eax
            jns .is_available
            
            ; we're at the end of the memory map, so we can't allocate
            xor eax, eax
            jmp .exit

    .change_entry:
        ; so unlikely it hurts, but should we make an allocation
        ; that just happens to fit into this entry, then just change
        ; the type to _FIRMWARE and return the base address.
        mov eax, ARG1_32
        mov dword [esi + VBootMemoryEntry.Type], eax
        mov eax, dword [esi + VBootMemoryEntry.PhysicalBase]
        jmp .exit

    .split_entry:
        push ecx  ; save our size
        push 1    ; move down once
        push eax  ; push from current position
        call MemoryMoveMapDown
        add esp, 8
        pop ecx

        ; Split the entry from the behind, so the 
        ; ith entry must look like this
        ; i.Length -= ecx
        ; i+1.Start = (i.Start + i.Length)
        ; i+1.Length = ecx

        ; Adjust length of current entry
        sub dword [esi + VBootMemoryEntry.Length], ecx

        ; Update new entry that we have created space for
        mov eax, dword [esi + VBootMemoryEntry.PhysicalBase]
        mov edx, dword [esi + VBootMemoryEntry.PhysicalBase + 4]
        add eax, dword [esi + VBootMemoryEntry.Length]
        adc edx, dword [esi + VBootMemoryEntry.Length + 4]
        add esi, VBootMemoryEntry_size ; switch to next entry

        ; Initialize new entry
        mov dword [esi + VBootMemoryEntry.PhysicalBase], eax
        mov dword [esi + VBootMemoryEntry.PhysicalBase + 4], edx
        mov dword [esi + VBootMemoryEntry.Length], ecx
        mov dword [esi + VBootMemoryEntry.Length + 4], 0

        mov eax, ARG1_32
        mov dword [esi + VBootMemoryEntry.Type], eax
        mov dword [esi + VBootMemoryEntry.Attributes], 0xF

        ; move start address into eax
        ; ASSUMPTION: we're always allocating under 4gb
        mov eax, dword [esi + VBootMemoryEntry.PhysicalBase]

        ; increase the number of entries
        add byte [bMemoryEntryCount], 1

    .exit:
        pop esi
        STACK_FRAME_END32
        ret

; **********************
; MemoryFree
; @brief Frees a block of memory from the memory map, marks it as _AVAILABLE.
;
; @param address [0, In] The address of the memory block to free.
; @return                1 if successful, 0 otherwise.
; ********************** 
MemoryFree:
    STACK_FRAME_BEGIN32
    push esi
    
    ; Find the entry and mark it as _AVAILABLE
    mov esi, MEMLOCATION_MEMORY_MAP
    xor ecx, ecx
    mov cl, byte [bMemoryEntryCount]
    mov eax, ARG0_32

    .check_entry:
        cmp dword [esi + VBootMemoryEntry.PhysicalBase], eax
        jne .next_entry

        ; found it, change it to _AVAILABLE
        mov dword [esi + VBootMemoryEntry.Type], VBOOT_MEMORY_TYPE_AVAILABLE

        ; set as success
        mov eax, 1
        jmp .exit

        .next_entry:
            add esi, VBootMemoryEntry_size
            dec ecx
            jnz .check_entry
            xor eax, eax ; set 0 to indicate error

    .exit:
        pop esi
        STACK_FRAME_END32
        ret

; **********************
; MemoryFinalizeMap
; @brief Finalizes the memory map, by performing cleanup and corrections before
; passing it to the operating system.
; @return 0 on success, 1 on error.
; **********************
MemoryFinalizeMap:
    STACK_FRAME_BEGIN32
    push esi
    push edi

    ; Go through memory map and merge adjacent entries that are marked as same type
    ; and have the same attributes (but they all do when booting from bios).
    ; We also need to make sure that all _BOOT entries get marked as _AVAILABLE.
    mov esi, MEMLOCATION_MEMORY_MAP
    xor ecx, ecx
    mov cl, byte [bMemoryEntryCount]

    ; Mark all _BOOT entries as _AVAILABLE
    .free_boot:
        cmp dword [esi + VBootMemoryEntry.Type], VBOOT_MEMORY_TYPE_BOOT
        jne .next_entry_0
        mov dword [esi + VBootMemoryEntry.Type], VBOOT_MEMORY_TYPE_AVAILABLE

        .next_entry_0:
            add esi, VBootMemoryEntry_size
            dec ecx
            jnz .free_boot

    ; Consolidate all entries of same type
    mov esi, MEMLOCATION_MEMORY_MAP
    mov edi, MEMLOCATION_MEMORY_MAP
    xor ecx, ecx
    mov cl, byte [bMemoryEntryCount]

    ; start iterating from entry 1
    add esi, VBootMemoryEntry_size
    dec ecx

    .consolidate_map:
        ; cmp entries[i -1].Type == entries[i].Type
        ; edi = i-1
        ; esi = i
        mov eax, dword [edi + VBootMemoryEntry.Type]
        cmp eax, dword [esi + VBootMemoryEntry.Type]
        jne .next_entry_1

        ; ok they match, what we do now is entries[i-1].Length += entries[i].Length
        mov eax, dword [esi + VBootMemoryEntry.Length]
        mov edx, dword [esi + VBootMemoryEntry.Length + 4]
        add dword [edi + VBootMemoryEntry.Length], eax
        adc dword [edi + VBootMemoryEntry.Length + 4], edx

        ; and then we memmove(
        ;                     dest: i+1, src: i, 
        ;                     len: VBootMemoryEntry_size * (entryCount - i - 1)))
        ; Move up, calculate the number of entries to move
        mov eax, esi
        add eax, VBootMemoryEntry_size ; eax is now i+1

        push ecx ; index
        push esi ; src
        push eax ; dest
        call MemoryMoveMapUp
        add esp, 8 ; skip src+dest
        pop ecx ; restore index

        ; subtract a count of 1 from total count and current remaining
        dec byte [bMemoryEntryCount]
        dec ecx
        jmp .consolidate_map

        .next_entry_1:
            add esi, VBootMemoryEntry_size
            add edi, VBootMemoryEntry_size
            dec ecx
            jnz .consolidate_map

    ; lastly, store information about memory map in vboot header
    xor eax, eax
    mov al, byte [bMemoryEntryCount]
    mov dword [BootHeader + VBoot.MemoryCount], eax
    mov dword [BootHeader + VBoot.MemoryEntries], MEMLOCATION_MEMORY_MAP

    pop edi
    pop esi
    STACK_FRAME_END32
    ret

; **********************
; MemoryGetUpperBound
; @brief Finds the memory map entry with the highest upper physical memory
; address. This is used to determine the upper bound of the memory map.
; 
; @return [eax] Lower 32 bits of the memory upper bound.
; @return [ecx] Upper 32 bits of the memory upper bound.
; **********************
MemoryGetUpperBound:
    STACK_FRAME_BEGIN32
    push esi

    ; Get upper bound of memory map
    mov esi, MEMLOCATION_MEMORY_MAP
    xor ecx, ecx
    mov cl, byte [bMemoryEntryCount]

    ; The map is already sorted by address, so just take n-1 entry
    ; and add length. Should we take highest non-reserved instead??
    xor edx, edx
    mov eax, VBootMemoryEntry_size
    mul ecx
    sub eax, VBootMemoryEntry_size
    add esi, eax

    .check:
        cmp dword [esi + VBootMemoryEntry.Type], VBOOT_MEMORY_TYPE_RESERVED
        je .next_entry

        ; found it, return it
        mov eax, dword [esi + VBootMemoryEntry.PhysicalBase]
        mov ecx, dword [esi + VBootMemoryEntry.PhysicalBase + 4]
        add eax, dword [esi + VBootMemoryEntry.Length]
        adc ecx, dword [esi + VBootMemoryEntry.Length + 4]
        jmp .exit

        .next_entry:
            add esi, VBootMemoryEntry_size
            dec ecx
            jnz .check

    .exit:
        pop esi
        STACK_FRAME_END32
        ret

%endif
