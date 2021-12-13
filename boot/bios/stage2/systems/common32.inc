; MollenOS
;
; Copyright 2011, Philip Meulengracht
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
; *******************************************************
; Common.inc
; - Contains shared routines and shared data
; 

%ifndef _MBOOT_COMMON32_INC_
%define _MBOOT_COMMON32_INC_

BITS 32

; Utilities for me
%macro TRACE32 1
    push %1
    push Print16
    call BiosCallWrapper
    add esp, 8
%endmacro

; @param buffer [In] The lz-compressed buffer that should be unpacked
; @return [eax] A new buffer with unpacked contents
; @return [ecx] Size of the new buffer
UnpackFile:
    STACK_FRAME_BEGIN32
    sub esp, 8
    push esi
    push edi

    mov LVAR0_32, 0 ; unpackedBuffer
    mov LVAR1_32, 0 ; unpackedSize

    push dword ARG0_32
    call lz_validate
    add esp, 4
    cmp eax, -1
    je .exit

    mov LVAR1_32, eax

    push eax
    call MemoryAllocateFirmware
    add esp, 4
    cmp eax, 0
    je .exit

    mov LVAR0_32, eax

    mov esi, ecx
    mov edi, eax
    call lz_decompress

    ; free file buffer, not needed anymore
    push dword ARG0_32
    call MemoryFreeFirmware
    add esp, 4

    .exit:
        mov eax, LVAR0_32
        mov ecx, LVAR1_32

    pop edi
    pop esi
    STACK_FRAME_END32
    ret

; ***********************
; SystemsFail32
; @brief System failure handler for 32 bit mode. This prints the provided
; message, and then halts the system. Switches to 16 bit mode first to provide
; message environment
; @param message [In] The message to print
; @returns <none>
; ***********************
SystemsFail32:
    STACK_FRAME_BEGIN32
    push dword ARG0_32
    push SystemsFail
    call BiosCallWrapper
    STACK_FRAME_END32
    ret

%endif
