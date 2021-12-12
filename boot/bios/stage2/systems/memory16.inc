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

%ifndef _MBOOT_MEMORY16_INC_
%define _MBOOT_MEMORY16_INC_

BITS 16

%define BIOS_MEMMAP_SIGNATURE 0x0534D4150

struc BiosMemoryMapEntry
    .PhysicalBase resq 1
    .Length       resq 1
    .Type         resd 1
    .AcpiAttribs  resd 1 ; ACPI 3.0 Extended Attributes bitfield
endstruc

; ********************************
; MemoryConvertEntry
; @brief Converts a BIOS memory map entry to VBoot format
;
; @param biosEntry  [0, In] The BIOS memory map entry
; @param vbootEntry [1, In] The VBoot memory map entry
; @return <none>
MemoryConvertEntry:
    STACK_FRAME_BEGIN16
    push si
    push di

    mov si, ARG0_16
    mov di, ARG1_16

    mov ax, [si + BiosMemoryMapEntry.Type]
    mov ecx, 0 ; ecx will hold the memory attributes
    cmp ax, 0x01
    je .set_available
    cmp ax, 0x03
    je .set_acpi_reclaim
    cmp ax, 0x04
    je .set_acpi_nvs
    mov ax, VBOOT_MEMORY_TYPE_RESERVED ; otherwise bad memory

    .set_available:
        mov ax, VBOOT_MEMORY_TYPE_AVAILABLE
        mov ecx, 0xF
        jmp .set

    .set_acpi_reclaim:
        mov ax, VBOOT_MEMORY_TYPE_ACPI
        mov ecx, 0xF
        jmp .set
    
    .set_acpi_nvs:
        mov ax, VBOOT_MEMORY_TYPE_NVS
        mov ecx, 0xF
    
    .set: ; set attribs and type
    mov word [di + VBootMemoryEntry.Type], ax
    mov dword [di + VBootMemoryEntry.Attributes], ecx

    ; set physical base
    mov eax, dword [si + BiosMemoryMapEntry.PhysicalBase]
    mov ecx, dword [si + BiosMemoryMapEntry.PhysicalBase + 4]
    mov dword [di + VBootMemoryEntry.PhysicalBase], eax
    mov dword [di + VBootMemoryEntry.PhysicalBase + 4], ecx

    ; set length
    mov eax, dword [si + BiosMemoryMapEntry.Length]
    mov ecx, dword [si + BiosMemoryMapEntry.Length + 4]
    mov dword [di + VBootMemoryEntry.Length], eax
    mov dword [di + VBootMemoryEntry.Length + 4], ecx
    
    pop di
    pop si
    STACK_FRAME_END16
    ret

; ********************************
; MemoryGetMap
; @brief Returns the memory map from the BIOS INT15 method
; and writes it into the provided buffer
;
; @param bufferSegment [0, In] Segment of the buffer to write to
; @param bufferOffset  [1, In] Offset into the segment of the buffer
; @returns 1 if successful, 0 if not
; ********************************
MemoryGetMap:
    STACK_FRAME_BEGIN16
    push si
    push di
    push ebx

    ; initialize buffer location to es:di
    ; ASSUMPTION buffer is located inside es 0 segment
    mov di, MmEntry
    mov si, ARG1_16

    ; Setup INT 0x15
    mov eax, 0xE820  ; Functino E820
    xor ebx, ebx     ; should be clear
    mov ecx, 24      ; Tell bios we want 24 bytes at the time (max size entry)
    mov edx, BIOS_MEMMAP_SIGNATURE

    ; In case ACPI 3.0 entries are not returned, lets mark bit 1 to indicate that
    ; this entry is indeed valid.
    mov [MmEntry + BiosMemoryMapEntry.AcpiAttribs], dword 1

    int 0x15
    jc .NoMemoryMap

    ; Restore EDX for safety (EAX contains signature on success)
    mov edx, BIOS_MEMMAP_SIGNATURE
    cmp eax, edx
    jne .NoMemoryMap

    ; If ebx is 0, only 1 entry, something is off 
    test ebx, ebx
    je  .NoMemoryMap
    jmp .MediasRes

    .GetNextEntry:
        mov eax, 0xE820
        mov ecx, 24
        mov [MmEntry + BiosMemoryMapEntry.AcpiAttribs], dword 1
        int 0x15
        jc  .Done
        mov edx, BIOS_MEMMAP_SIGNATURE

    .MediasRes:
        test ecx, ecx
        je .SkipEntry
        mov al, byte [MmEntry + BiosMemoryMapEntry.AcpiAttribs]
        and al, 0x1
        jz .SkipEntry

        ; entry is good, lets convert it to a vboot entry
        push si
        push di
        call MemoryConvertEntry
        add sp, 4

    .GoodEntry:
        inc byte [bMemoryEntryCount]
        add si, VBootMemoryEntry_size ; next vboot entry 

    .SkipEntry:
        ; If ebx resets to 0, list is complete
        test ebx, ebx
        jne .GetNextEntry
        jmp .Done

    .NoMemoryMap:
        xor eax, eax
        jmp .Exit

    .Done:
        mov eax, 1

    .Exit:
        pop ebx
        pop di
        pop si
        STACK_FRAME_END16
        ret

; ********************************
; GetMemorySize
; OUT:
; 	- EAX: KB between 1 mb and 16 mb (0 on err)
; 	- EBX: Num of 64 Kb blocks above 16 mb (0 on err)
; ********************************
GetMemorySize:
    ; Clear registers
    xor ecx, ecx
    xor edx, edx
    
    ; Setup INT 0x15
    mov eax, 0xE801
    int 0x15
    jc .Error

    ; Unsupported??
    cmp ah, 0x86
    je .Error

    ; Invalid Cmd??
    cmp ah, 0x80
    je .Error

    ; It is possible that bios uses ECX/EDX instead
    test ecx, ecx
    je .UseEAX

    mov eax, ecx
    mov ebx, edx

    .UseEAX:
        ret

    .Error:
        mov eax, 0
        mov ebx, 0
        ret
    

; ********************************
; MemoryInitialize16
; ********************************
MemoryInitialize16:
    STACK_FRAME_BEGIN16
    push di

    ; clear out buffer we store the memory map in
    xchg bx, bx
    xor eax, eax
    mov di, MEMLOCATION_MEMORY_MAP
    mov ecx, 1024
    rep stosd

    ; load initial memory map into the buffer, they will converted
    ; to vboot format before they are stored
    mov di, MEMLOCATION_MEMORY_MAP
    push di
    push 0
    call MemoryGetMap
    add sp, 4

    pop di
    STACK_FRAME_END16
    ret

; Store for parsing entries
bMemoryEntryCount db 0

MmEntry:
istruc BiosMemoryMapEntry
    at BiosMemoryMapEntry.PhysicalBase, dq 0
    at BiosMemoryMapEntry.Length,       dq 0
    at BiosMemoryMapEntry.Type,         dd 0
    at BiosMemoryMapEntry.AcpiAttribs,  dd 0 ; ACPI 3.0 Extended Attributes bitfield
iend

%endif
