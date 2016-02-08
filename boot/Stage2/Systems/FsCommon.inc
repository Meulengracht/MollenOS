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
; FsCommon.inc
; - Contains shared filesystem routines
; 

%ifndef _MBOOT_FILESYSTEM_COMMON_INC_
%define _MBOOT_FILESYSTEM_COMMON_INC_

BITS 16

; Definitions


; Includes
%include "Systems/Fat.inc"
%include "Systems/Mfs.inc"

; **************************
; BIOS ReadSector 
; IN:
; 	- ES:BX: Buffer
;	- EAX: Sector start low-dword
;	- ESI: Sector start high-dword
; 	- ECX: Sector count
; 	- EDX: Sector size in bytes
;
; Registers:
; 	- Conserves all but ES:BX
; **************************
ReadSector:
	pushad

	; Set initial 
	mov 	word [DiskPackage.Segment], es
	mov 	word [DiskPackage.Offset], bx
	mov 	dword [DiskPackage.Sector], eax
	mov		dword [DiskPackage.Sector + 4], esi

	.sLoop:
		; Setup Package
		mov 	word [DiskPackage.SectorsToRead], 1

		; Setup INT
		push 	edx
		mov 	al, 0
		mov 	ah, 0x42
		mov 	dl, byte [bDriveNumber]
		mov 	si, DiskPackage
		int 	0x13

		; It's important we check for offset overflow
		pop 	edx
		mov 	ax, word [DiskPackage.Offset]
		add 	ax, dx
		mov 	word [DiskPackage.Offset], ax
		test 	ax, ax
		jne 	.NoOverflow

	.Overflow:
		; So overflow happened
		add 	word [DiskPackage.Segment], 0x1000
		mov 	word [DiskPackage.Offset], 0x0000

	.NoOverflow:
		; Loop
		inc 	dword [DiskPackage.Sector]
		loop 	.sLoop

	.End:
	; Restore registers 
	popad

	; Save position
	push 	eax
	xor 	eax, eax
	mov 	ax, word [DiskPackage.Segment]
	mov 	es, ax
	mov 	bx, word [DiskPackage.Offset]
	pop 	eax
	ret

; ****************************
; LoadFile
; IN:
; 	- DS:ESI: Filename (8:3)
;	- DS:EDI: Filename (UTF-8)
; 	- ES:EBX: Buffer
; OUT:
;	- EAX: Filesize
; ****************************
LoadFile:
	; Save state
	push 	ecx
	push 	edx
	push	esi
	push 	edi

	; Get type of FS
	xor 	ecx, ecx
	mov 	cl, byte [bFileSysType]

	; MFS (Old)? 
	cmp 	cl, 0
	je 		.Done
	cmp 	cl, 1
	je 		.Done

	; Fat? 
	cmp 	cl, 2
	je 		.FAT
	cmp 	cl, 3
	je 		.FAT
	cmp 	cl, 4
	je 		.FAT

	; Mfs?
	cmp		cl, 5
	je		.MFS

	; Wtf?
	jmp 	.Done

	; MFS
	.MFS:
		mov		esi, edi
		call	MfsLoadFile
		jmp		.Done

	; FAT
	.FAT:
		call 	FatLoadFile

	.Done:
		pop 	edi
		pop		esi
		pop 	edx
		pop 	ecx
		ret


; ****************************
; SetupFS
; IN:
; 	- AL: Filesystem type
; 	- AH: Drive Number
; ****************************
SetupFS:
	; Save state
	pushad
	push 	ax

	; Save
	mov 	byte [bFileSysType], al

	; Print
	mov 	esi, szPrefix
	call 	Print

	; 0, 1 => MFS (Old)
	cmp 	al, 0
	je 		.NotSupported

	cmp 	al, 1
	je 		.NotSupported

	; 2, 3, 4 => FAT
	cmp 	al, 2
	je 		.SetupFAT12

	cmp 	al, 3
	je 		.SetupFAT16

	cmp 	al, 4
	je 		.SetupFAT32

	; 5 => MFS1
	cmp 	al, 5
	je 		.iSetupMFS

	; End of list
	jmp 	.NotSupported


	; FAT
	.SetupFAT12:
		; Print
		mov 	esi, szFsSetup
		call 	Print

		mov 	esi, szFat12
		call 	Print

		; Setup
		pop 	ax
		mov 	al, FAT_TYPE_FLOPPY12
		call 	SetupFat
		jmp 	.Done

	.SetupFAT16:
		; Print
		mov 	esi, szFsSetup
		call 	Print

		mov 	esi, szFat16
		call 	Print

		; Setup
		pop 	ax
		mov 	al, FAT_TYPE_HDD16
		call 	SetupFat
		jmp 	.Done
		
	.SetupFAT32:
		; Print
		mov 	esi, szFsSetup
		call 	Print

		mov 	esi, szFat32
		call 	Print

		; Setup
		pop 	ax
		mov 	al, FAT_TYPE_HDD32
		call 	SetupFat
		jmp 	.Done

	.iSetupMFS:
		; Print
		mov 	esi, szFsSetup
		call 	Print

		mov 	esi, szMfs
		call 	Print

		; Setup
		pop 	ax
		mov 	al, 0
		call 	SetupMfs
		jmp 	.Done

	; Not supported
	.NotSupported:
		; Print
		mov 	esi, szNotSupported
		call 	Print

		;Damn
		call 	SystemsFail
		cli
		hlt

	; Done
	.Done:
		; Print
		mov 	esi, szSuccess
		call 	Print

		; Restore & Return
		popad
		ret


; ****************************
; Variables
; ****************************

; Strings - 0x0D (LineFeed), 0x0A (Carriage Return)
szFsSetup						db		"Setting up ", 0x00
szFsLoading						db 		"Loading Kernel", 0x00
szNotSupported					db 		"Filesystem is not supported", 0x00
szMfs							db 		"MFS-1", 0x00
szFat12							db		"FAT 12", 0x00
szFat16							db		"FAT 16", 0x00
szFat32 						db 		"Fat 32", 0x00

; Misc
bFileSysType					db 		0

; This is used for the extended read function (int 0x13)
DiskPackage:				db 0x10
							db 0
	.SectorsToRead			dw 0
	.Offset					dw 0
	.Segment 				dw 0
	.Sector 				dq 0

%endif