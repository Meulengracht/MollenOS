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

%ifndef _MBOOT_CPU_INC_
%define _MBOOT_CPU_INC_

BITS 32

; **************************
; CpuDetect64
; @brief Detects whether or not 64 bit mode is supported by the CPU
; @returns 1 if 64 bit mode is supported, 0 otherwise
; **************************
CpuDetect64:
    push ebx

    ; Use cpuid to detect features
    mov eax, 0x80000001
    cpuid
    test edx, 0x20000000
    jz .no_64bit
    
    ; yes, it is supported
	mov eax, 1
    jmp .exit

    .no_64bit:
        xor eax, eax

    .exit:
        pop ebx
        ret

%endif
