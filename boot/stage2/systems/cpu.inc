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

; edi - location of page-table
; ebx - physical address start
CpuIdentityMapPageTable:
    push edi
    push ebx
    push ecx
    
    ; Mark the pages for PAGE_PRESENT | PAGE_WRITE
    and     edi, 0xFFFFF000 ; PAGE_MASK
    or      ebx, 0x00000003
    mov     ecx, 512
    .SetPageTableEntry:
        mov     dword [edi], ebx
        add     ebx, 0x00001000
        add     edi, 8
        loop    .SetPageTableEntry
        
    pop ecx
    pop ebx
    pop edi
    ret

; edi - location of page-directory
; ebx - physical address start
; ecx - length
CpuIdentityMapPageDirectory:
    mov esi, edi        ; esi points to page directory
    add edi, 0x1000     ; next physical address for page-table
    or  edi, 0x00000003 ; set PAGE_PRESENT | PAGE_WRITE
    .SetPageDirectoryEntry:
        mov dword [esi], edi
        call CpuIdentityMapPageTable
        
        add edi, 0x1000  ; edi points to next page table
        add esi, 8       ; esi points to next entry in page directory
        add ebx, PT_SIZE ; ebx points to next physical address start
        loop .SetPageDirectoryEntry
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
    mov     ebx, MEMLOCATION_PML4T
    
    mov     edi, ebx
    mov     cr3, edi
    xor     eax, eax
    mov     ecx, 0x1400
    rep     stosd

    ; Make PM4LT[0] => PDPT and set PAGE_PRESENT | PAGE_WRITE
    mov     edi, ebx
    add     ebx, 0x1000
    mov     dword [edi], ebx
    or      dword [edi], 0x00000003

    ; Make PDPT[0] => PDT and set PAGE_PRESENT | PAGE_WRITE
    mov     edi, ebx
    add     ebx, 0x1000
    mov     dword [edi], ebx
    or      dword [edi], 0x00000003

    ; ebx - initial physical address
    ; edi - location of page-directory
    ; ecx - count
    xor    edx, edx
    mov    eax, IDENTITY_MAP_RANGE
    mov    edi, ebx
    mov    ecx, PT_SIZE
    div    ecx
    mov    ecx, eax
    xor    ebx, ebx
    call   CpuIdentityMapPageDirectory

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