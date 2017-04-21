; *******************************************************
; Mollen-OS Stage 2 Bootloader
; Copyright 2015 (C)
; Author: Philip Meulengracht
; Version 1.0
; *******************************************************
; FsCommon.inc
; - Contains shared filesystem routines
; 

%ifndef _MBOOT_FAT_INC_
%define _MBOOT_FAT_INC_

BITS 16

; Definitions
%define 		FAT_TYPE_FLOPPY12		0
%define 		FAT_TYPE_HDD16			1
%define 		FAT_TYPE_HDD32			2

; Structure Definitions
; Size is 90 bytes
struc FatBootSectorStruct
	.JumpCode			resb	3
	.OemName			resb	8
	.BytesPerSector		resw	1
	.SectorsPerCluster	resb	1
	.ReservedSecCount   resw	1
	.NumFATS			resb	1
	.RootEntryCount		resw	1
	.TotalSectors16		resw	1
	.Media				resb	1
	.SectorsPerFAT16	resw	1
	.SectorsPerTrack	resw	1
	.NumHeads			resw	1
	.HiddenSectors		resd	1
	.TotalSectors32		resd	1
	.SectorsPerFAT32	resd	1
	.Flags 				resw	1
	.Version 			resw	1
	.RootCluster		resd	1
	.FSInfo				resw	1
	.BackupBootSector	resw	1
	.Reserved0			resb	12
	.DriveNumber		resb	1
	.Reserved1			resb	1
	.BootSignature		resb	1
	.VolumeID			resd	1
	.VolumeLabel		resb	11
	.FileSystemType		resb	8
endstruc

; **************************
; GetNextCluster (ONLY WORKS FOR FAT32)
; IN:
; 	- ESI ClusterNum
;
; OUT:
;	- ESI NextClusterNum
; **************************
GetNextCluster:
	; Save states
	push 	eax
	push 	ebx
	push 	ecx
	push 	edx
	push 	es

	; Calculte Sector in FAT
	xor 	eax, eax
	xor 	ebx, ebx
	xor 	edx, edx ; Zero out edx before division!!
	mov 	eax, esi
	shl 	eax, 2 	 ; REM * 4, since entries are 32 bits long, and not 8
	mov 	bx, word [FatBootSector + FatBootSectorStruct.BytesPerSector]
	div 	ebx
	mov 	bx, word [FatBootSector + FatBootSectorStruct.ReservedSecCount]
	add 	eax, ebx
	push 	edx

	; EAX contains sector
	; EDX contains remainder
	xor 	edx, edx
	mov 	dx, word [FatBootSector + FatBootSectorStruct.BytesPerSector]
	mov 	ecx, 1
	mov 	bx, 0x0000
	mov 	es, bx
	mov 	bx, MEMLOCATION_FAT_GETCLUSTER
	push 	es
	push 	bx

	; Read Sector
	push	esi
	xor		esi, esi
	call 	ReadSector
	pop		esi
	pop 	bx
	pop 	es

	; Find Entry
	pop 	edx
	xchg 	esi, edx
	mov 	esi, dword [es:bx + si]

	; Restore & Return
	pop 	es
	pop 	edx
	pop 	ecx
	pop 	ebx
	pop 	eax
	ret

; **************************
; FAT ReadCluster
; IN: 
;	- ES:BX Buffer
;	- ESI ClusterNum
;
; OUT:
;	- ESI NextClusterInChain
; **************************
ReadCluster:
	pushad

	; Save Bx
	push 	bx

	; Calculate Sector
	; FirstSectorofCluster = ((N – 2) * BPB_SecPerClus) + FirstDataSector;
	xor 	eax, eax
	xor 	ebx, ebx
	mov 	eax, esi
	sub 	eax, 2
	mov 	bl, byte [FatBootSector + FatBootSectorStruct.SectorsPerCluster]
	mul 	ebx
	add 	eax, dword [dFirstDataSector]

	; Eax is now the sector of data
	xor 	ebx, ebx
	xor 	ecx, ecx
	xor 	edx, edx
	pop 	bx
	mov 	cl, byte [FatBootSector + FatBootSectorStruct.SectorsPerCluster]
	mov 	dx, word [FatBootSector + FatBootSectorStruct.BytesPerSector]

	; Read
	push	esi
	xor		esi, esi
	call 	ReadSector
	pop		esi

	; Save position
	mov 	word [wFATTemporary0], bx
	push 	es

	; Si still has cluster num, call next
	call 	GetNextCluster
	mov 	dword [dFATTemporary1], esi

	; Restore
	pop 	es

	; Done
	popad
	mov 	bx, word [wFATTemporary0]
	mov 	esi, dword [dFATTemporary1]
	ret


; ****************************
; FatLoadFile
; IN:
; 	- DS:SI: Filename
; 	- ES:BX: Buffer
; OUT:
;	- EAX: Filesize
; ****************************
FatLoadFile:
	; Save arguments, we dont need them yet
	mov 	ax, es
	mov 	word [wFATLoadFileFSegment], ax
	mov 	word [wFATLoadFileFOffset], bx
	mov 	dword [dFATLoadFileFName], esi

	; Step 1. Read FAT Table
	mov 	esi, dword [FatBootSector + FatBootSectorStruct.RootCluster]

	; Read Loop
	.cLoop:
		mov 	bx, 0x0000
		mov 	es, bx
		mov 	bx, MEMLOCATION_FAT_FATTABLE
		
		; ReadCluster returns next cluster in chain
		call 	ReadCluster
		push 	esi

		; Step 3. Parse entries and look for esi
		mov 	edi, MEMLOCATION_FAT_FATTABLE
		mov 	esi, dword [dFATLoadFileFName]

		; Entry Count (dx)
		; Lengtho f name in CX
		xor 	eax, eax
		xor 	ebx, ebx
		xor 	ecx, ecx
		mov 	cx, 0x000B
		mov 	ax, 0x0020
		mov 	bl, byte [FatBootSector + FatBootSectorStruct.SectorsPerCluster]
		mul 	ebx
		mov 	edx, eax

		; End of root?
		.EntryLoop:
			cmp 	[es:di], ch
			je 		.ExitLoop

			; No, phew, lets check if filename matches
			pushad
        	repe    cmpsb
        	popad
        	jne 	.Next

        	; YAY WE FOUND IT!
        	; Get clusterLo & clusterHi
        	push    word [es:di + 0x14]
        	push    word [es:di + 0x1A]
        	pop 	esi

        	; Get filesize 
        	mov 	eax, dword [es:di + 0x1C]
        	mov 	dword [dFATLoadFileFSize], eax

        	; Lets load the fuck out of this file
			mov 	ax, word [wFATLoadFileFSegment]
			mov 	es, ax
			mov 	bx, word [wFATLoadFileFOffset]

			; Load 
			.dLoop:
				; Clustertime
				call 	ReadCluster

				; Check
				cmp 	esi, 0x0FFFFFF8
				jb 		.dLoop


        	; We are done, end this
        	pop 	eax 		; Fix stack
        	jmp 	.Success

        	; Next entry
        	.Next:
        		add     di, 0x20
        		dec 	dx
        		jnz 	.EntryLoop

		; Dont loop if esi is above 0x0FFFFFFF5
		pop 	esi
		cmp 	esi, 0x0FFFFFF8
		jb 		.cLoop
		jmp 	.NotFound

	.ExitLoop:
		pop 	esi
		jmp 	.NotFound

	.Success:
		; Move filesize
		mov 	eax, dword [dFATLoadFileFSize]
		ret

	.NotFound:
		; 0 Filesize
		mov 	eax, 0
		ret

; ****************************
; SetupFat
; IN:
; 	- AL: Fat Type
; 	- AH: Drive Number
; ****************************
SetupFat:
	; Save State
	pushad

	; Save data
	mov 	byte [bFatType], al
	mov 	byte [bFatDriveNumber], ah

	; Copy bootcode to our structure
	mov 	esi, MEMLOCATION_BOOTCODE
	mov 	edi, FatBootSector
	mov 	ecx, 0x5A
	rep 	movsb

	; Done, I don't need distractions
	; when I do my calculations

	; Calculate how many sectors is used by the root directory
	; on FAT32 this is always 0
	xor 	eax, eax
	xor 	ebx, ebx

	; RootDirSectors = ((BPB_RootEntCnt * 32) + (BPB_BytsPerSec – 1)) / BPB_BytsPerSec;
	mov 	ax, word [FatBootSector + FatBootSectorStruct.RootEntryCount]
	mov 	bx, 32
	mul 	bx
	mov 	bx, word [FatBootSector + FatBootSectorStruct.BytesPerSector]
	add 	ax, bx
	dec 	ax
	div 	bx

	; Save
	mov 	word [wRootDirSectors], ax

	; Fat 12+16/32?
	xor 	eax, eax
	cmp 	word [FatBootSector + FatBootSectorStruct.SectorsPerFAT16], 0
	je 		.FatType32

	; Fat 12 / 16
	mov 	ax, word [FatBootSector + FatBootSectorStruct.SectorsPerFAT16]
	jmp 	.Else

	.FatType32:
		mov 	eax, dword [FatBootSector + FatBootSectorStruct.SectorsPerFAT32]
	.Else:

	; Compute first data sector
	; FirstDataSector = BPB_ResvdSecCnt + (BPB_NumFATs * FATSz) + RootDirSectors
	xor 	ebx, ebx
	mov 	bl, byte [FatBootSector + FatBootSectorStruct.NumFATS]
	mul 	ebx
	mov 	bx, word [wRootDirSectors]
	add 	eax, ebx
	mov 	bx,  word [FatBootSector + FatBootSectorStruct.ReservedSecCount]
	add 	eax, ebx

	; Save
	mov 	dword [dFirstDataSector], eax

	; Done
	popad
	ret



; ****************************
; Variables
; ****************************

; Strings - 0x0D (LineFeed), 0x0A (Carriage Return)


; Etc
bFatType						db 		0
bFatDriveNumber					db 		0
wRootDirSectors					dw 		0
dFirstDataSector				dd 		0
wFATTemporary0					dw 		0
dFATTemporary1					dd 		0
dFATLoadFileFName				dd 		0
dFATLoadFileFSize				dd 		0
wFATLoadFileFSegment 			dw 		0
wFATLoadFileFOffset 			dw 		0


; ****************************
; BOOT CODE & BPB
; ****************************
FatBootSector:
istruc FatBootSectorStruct
	at FatBootSectorStruct.JumpCode,			db	0, 0, 0
	at FatBootSectorStruct.OemName,				db	0, 0, 0, 0, 0, 0, 0, 0
	at FatBootSectorStruct.BytesPerSector,		dw	0
	at FatBootSectorStruct.SectorsPerCluster,	db	0
	at FatBootSectorStruct.ReservedSecCount,   	dw	0
	at FatBootSectorStruct.NumFATS,				db	0
	at FatBootSectorStruct.RootEntryCount,		dw	0
	at FatBootSectorStruct.TotalSectors16,		dw	0
	at FatBootSectorStruct.Media,				db	0
	at FatBootSectorStruct.SectorsPerFAT16,		dw	0
	at FatBootSectorStruct.SectorsPerTrack,		dw	0
	at FatBootSectorStruct.NumHeads,			dw	0
	at FatBootSectorStruct.HiddenSectors,		dd	0
	at FatBootSectorStruct.TotalSectors32,		dd	0
	at FatBootSectorStruct.SectorsPerFAT32,		dd	0
	at FatBootSectorStruct.Flags, 				dw	0
	at FatBootSectorStruct.Version,				dw	0
	at FatBootSectorStruct.RootCluster,			dd	0
	at FatBootSectorStruct.FSInfo,				dw	0
	at FatBootSectorStruct.BackupBootSector,	dw	0
	at FatBootSectorStruct.Reserved0,			db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	at FatBootSectorStruct.DriveNumber,			db	0
	at FatBootSectorStruct.Reserved1,			db	0
	at FatBootSectorStruct.BootSignature,		db	0
	at FatBootSectorStruct.VolumeID,			dd	0
	at FatBootSectorStruct.VolumeLabel,			db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	at FatBootSectorStruct.FileSystemType,		db	0, 0, 0, 0, 0, 0, 0, 0
iend

%endif