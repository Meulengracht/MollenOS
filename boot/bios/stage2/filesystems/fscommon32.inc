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
; along with this program. If not, see <http://www.gnu.org/licenses/>.
;
; Mollen-OS Stage 2 Bootloader
; Version 1.0
; *******************************************************
; FsCommon.inc
; - Contains shared filesystem routines
; 

%ifndef _MBOOT_FILESYSTEM_COMMON32_INC_
%define _MBOOT_FILESYSTEM_COMMON32_INC_

BITS 32

struc FsFileHandle
    .Position:   resd 1
    .Size:       resd 1
    .BaySegment: resw 1
    .BayOffset:  resw 1
    .BaySize:    resd 1
    .Data1:      resd 1
    .Data2:      resd 1
    .Data3:      resd 1
endstruc

; include supported filesystems
%include "filesystems/fat32.inc"
%include "filesystems/mfs32.inc"

; ****************************
; LoadFile32
; @brief Loads a file from the filesystem. The file is loaded into the target
; address in 32 bit mode. This will happen over a number of iterations dependendant
; on the size of the file. The maximum chunk size is 400 kb.
; @param filenameDos   [0, In] The filename to load in the format of 8:3
; @param filenameUtf8  [1, In] The filename to load in the format of utf8
; @return [eax] A pointer to the filebuffer
; @return [ecx] The number of bytes read
; ****************************
LoadFile32:
    STACK_FRAME_BEGIN32
    sub esp, 4
    push edi

    ; Initialize the local variables
    mov LVAR0_32, 0

    ; Initialize bay stuff, so the filesystems below has some space
    ; to play around with. This is static anyway.
    mov word  [FileHandle + FsFileHandle.BaySegment], FILELOADING_BAY_SEGMENT
    mov word  [FileHandle + FsFileHandle.BayOffset],  FILELOADING_BAY_OFFSET
    mov dword [FileHandle + FsFileHandle.BaySize],    FILELOADING_BAY_SIZE
    mov dword [FileHandle + FsFileHandle.Size],       0

    ; push the file handle first
    push FileHandle

    ; open the file and hope it exists
    mov cl, byte [bFsCommonType]
    cmp cl, 5
    jb  .open_fat
    jmp .open_mfs

    .open_fat:
        mov eax, ARG0_32
        push eax
        call FatOpenFile
        add esp, 8
        test eax, eax
        jz  .exit
        jmp .read

    .open_mfs:
        mov eax, ARG1_32
        push eax
        call MfsOpenFile
        add esp, 8
        test eax, eax
        jz .exit

    ; allocate space for the file
    push VBOOT_MEMORY_TYPE_BOOT
    push dword [FileHandle + FsFileHandle.Size]
    call MemoryAllocate
    add esp, 4
    mov LVAR0_32, eax
    mov edi, eax

    ; read the file into the buffer
    .read:
        .loop:
            push FileHandle
            call MfsReadFile
            add esp, 8
            cmp eax, 0
            jz  .exit

            ; add bytes read to position
            add dword [FileHandle + FsFileHandle.Position], eax
            mov ecx, dword [FileHandle + FsFileHandle.Position]
            mov edx, dword [FileHandle + FsFileHandle.Size]
            cmp ecx, edx
            jbe .copy

            ; otherwise we have 'read too much in terms of sector sizes'
            ; as we have to read in multiple of sectors, adjust the number of
            ; bytes to copy
            sub ecx, edx    ; overage = position - size
            sub eax, ecx    ; bytesToCopy -= overage
            sub dword [FileHandle + FsFileHandle.Position], ecx ; position -= overage

            .copy:
                mov esi, FILELOADING_BAY_LINEAR
                mov ecx, eax
                rep movsb

            ; are we done?
            mov ecx, dword [FileHandle + FsFileHandle.Position]
            mov edx, dword [FileHandle + FsFileHandle.Size]
            cmp ecx, edx
            jne .loop

    .exit:
        mov eax, LVAR0_32
        mov ecx, dword [FileHandle + FsFileHandle.Size]
        pop edi
        STACK_FRAME_END32
        ret

; ****************************
; Variables
; ****************************
FileHandle:
istruc FsFileHandle
    at FsFileHandle.Position,   dd 0
    at FsFileHandle.Size,       dd 0
    at FsFileHandle.BaySegment, dw 0
    at FsFileHandle.BayOffset,  dw 0
    at FsFileHandle.BaySize,    dd 0
    at FsFileHandle.Data1,      dd 0
    at FsFileHandle.Data2,      dd 0
    at FsFileHandle.Data3,      dd 0
iend

%endif
