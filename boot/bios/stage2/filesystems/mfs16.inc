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
; Mfs.inc
; - Contains filesystem routines for MFS
; 

%ifndef _MBOOT_MFS16_INC_
%define _MBOOT_MFS16_INC_

BITS 16

%define MFS_MEMORY_SCRACT_SPACE512 0x7E00
%define MFS_DIRECTORY_ENTRY_SIZE   1024

; Structure Definitions
; Size is 52 bytes
struc MfsBootSectorStruct
    .JumpCode			resb	3
    .Magic				resd	1
    .Version			resb	1
    .Flags				resb	1
    .MediaType		    resb	1
    .BytesPerSector		resw	1
    .SectorsPerTrack	resw	1
    .HeadsPerCylinder	resw	1
    .TotalSectors		resq	1
    .ReservedSectors	resw	1
    .SectorsPerBucket	resw	1
    .MasterBucketSector	resq	1
    .MasterBucketMirror	resq	1
endstruc

; ****************************
; MfsInitialize16
; @brief Initializes the mfs filesystem driver. This reads data
; from the bootsector to determine location of important components
; as the MasterRecord and BucketMap.
; @param <none>
; @returns <none>
; ****************************
MfsInitialize16:
    ; Copy bootcode header to our structure and store it here
    ; so if someone overwrites the bootsector location we don't care
    mov esi, MEMLOCATION_BOOTCODE
    mov edi, MfsBootSector
    mov ecx, 0x2C
    rep movsb

    ; Initialize the fscommon so we can read from disk
    mov ax, word [MfsBootSector + MfsBootSectorStruct.BytesPerSector]
    mov cx, word [MfsBootSector + MfsBootSectorStruct.SectorsPerTrack]
    mov dx, word [MfsBootSector + MfsBootSectorStruct.HeadsPerCylinder]
    push ax
    push cx
    push dx
    call FsCommonSetGeometry16
    add sp, 6

    ; Calculate the number of directory entries per bucket
    xor eax, eax
    xor ecx, ecx
    mov ax, word [MfsBootSector + MfsBootSectorStruct.SectorsPerBucket]
    mov cx, word [MfsBootSector + MfsBootSectorStruct.BytesPerSector]
    mul ecx
    mov ecx, MFS_DIRECTORY_ENTRY_SIZE
    div ecx
    mov word [wMfsEntriesPerBucket], ax

    ; Get master record, this builds on the assumption that the master record
    ; is located inside the first 2TB of the disk.
    mov	eax, dword [MfsBootSector + MfsBootSectorStruct.MasterBucketSector]
    push eax
    push 1
    push MFS_MEMORY_SCRACT_SPACE512
    push 0
    call ReadSectorsLBA16
    add sp, 10

    mov	eax, MFS_MEMORY_SCRACT_SPACE512
    mov	ecx, [eax + 80]
    mov	dword [dRootBucket], ecx
    mov ecx, [eax + 92]
    mov dword [qBucketMapSector], ecx
    mov ecx, [eax + 96]
    mov dword [qBucketMapSector + 4], ecx
    ret

; ****************************
; Variables
; ****************************
bMfsEndOfString       db 0
qBucketMapSector      dq 0
dRootBucket           dd 0
dLastReadBucketSector dd 0
wMfsEntriesPerBucket  dw 0

; ****************************
; BOOT CODE & BPB
; ****************************
MfsBootSector:
istruc MfsBootSectorStruct
    at MfsBootSectorStruct.JumpCode,				db	0, 0, 0
    at MfsBootSectorStruct.Magic,					dd	0
    at MfsBootSectorStruct.Version,					db	0
    at MfsBootSectorStruct.Flags,   				db	0
    at MfsBootSectorStruct.MediaType,				db	0
    at MfsBootSectorStruct.BytesPerSector,			dw	0
    at MfsBootSectorStruct.SectorsPerTrack,			dw	0
    at MfsBootSectorStruct.HeadsPerCylinder,		dw	0
    at MfsBootSectorStruct.TotalSectors,			dq	0
    at MfsBootSectorStruct.ReservedSectors,			dw	0
    at MfsBootSectorStruct.SectorsPerBucket,		dw	0
    at MfsBootSectorStruct.MasterBucketSector,		dq	0
    at MfsBootSectorStruct.MasterBucketMirror,		dq	0
iend

%endif
