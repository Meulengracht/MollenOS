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
; Cpu.inc
; - Contains cpu initialization code
; 

%ifndef _MBOOT_CPU_INC_
%define _MBOOT_CPU_INC_

BITS 32

; **************************
; CpuDetect64 
; Out:
;   - EAX: 1 if 64 bit is ok, 0 if stay in 32 bit
;
; Registers:
; 	- Conserves all
; **************************
CpuDetect64:
	pushad

    ; Use cpuid to detect features
    mov     eax, 0x80000001
    cpuid
    test    edx, 0x20000000
    jz      .NotSupported
    popad
	mov 	eax, 1
    ret

    .NotSupported:
        popad
        xor     eax, eax
        ret

CpuIdentityMapPageTable:
    ; Mark the pages for PAGE_PRESENT | PAGE_WRITE
    or      ebx, 0x00000003
    mov     ecx, 512
    .SetPageTableEntry:
        mov     dword [edi], ebx
        add     ebx, 0x00001000
        add     edi, 8
        loop    .SetPageTableEntry
    ret

; **************************
; CpuSetupLongMode 
; Out:
;   - EAX: 1 if ok 0 if error
;
; Registers:
; 	- Conserves all
; **************************
CpuSetupLongMode:
	pushad

    ; Initialize the PM4LT and set CR3 to it
    ; This zeroes out all memory needed for paging 
    mov     edi, MEMLOCATION_PML4T
    mov     cr3, edi
    xor     eax, eax
    mov     ecx, 0x1400
    rep     stosd
    mov     edi, cr3

    ; Make PM4LT[0] => PDPT and set PAGE_PRESENT | PAGE_WRITE
    mov     dword [edi], MEMLOCATION_PDPT
    or      dword [edi], 0x00000003

    ; Make PDPT[0] => PDT and set PAGE_PRESENT | PAGE_WRITE
    mov     edi, MEMLOCATION_PDPT
    mov     dword [edi], MEMLOCATION_PDT
    or      dword [edi], 0x00000003

    ; Make PDT[0] => PT and set PAGE_PRESENT | PAGE_WRITE
    mov     edi, MEMLOCATION_PDT
    mov     dword [edi], MEMLOCATION_PT
    or      dword [edi], 0x00000003
    mov     dword [edi + 8], MEMLOCATION_PT2
    or      dword [edi + 8], 0x00000003

    ; Setup the page-table to be identity mapped to the first 4mb
    mov     edi, MEMLOCATION_PT
    mov     ebx, 0x00000000
    call    CpuIdentityMapPageTable
    mov     edi, MEMLOCATION_PT2
    mov     ebx, 0x00200000
    call    CpuIdentityMapPageTable
    
    ; Enable PAE paging
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

    popad
    ret

%endif