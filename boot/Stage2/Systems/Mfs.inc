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

%ifndef _MBOOT_MFS_INC_
%define _MBOOT_MFS_INC_

BITS 16

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
	.VolumeLabel		resb	8
endstruc

; **************************
; MFS GetNextBucket
; IN:
; 	- ESI Bucket
;
; OUT:
; 	- EAX BucketLength
;	- ESI NextBucket
; **************************
GetNextBucket:
	; Save states
	push 	ebx
	push 	ecx
	push 	edx
	push 	es

	; Calculte Bucket Sector Offset
	; BucketsPerSector = BytesPerSector / 8;
    ; SectorOffset = Bucket / BucketsPerSector;
    ; SectorIndex = Bucket % BucketsPerSector;
	xor 	eax, eax
	xor 	ebx, ebx
	xor 	edx, edx ; Zero out edx before division!!
	mov 	ax, word [MfsBootSector + MfsBootSectorStruct.BytesPerSector]
	mov		ebx, 0x8
	div 	ebx
	mov		ebx, eax ; ebx = BucketsPerSector
	xor		edx, edx
	mov		eax, esi
	div		ebx

	; EAX contains sector
	; EDX contains remainder
	push	edx
	push	eax

	mov		eax, dword [qBucketMapSector]
	mov		edx, dword [qBucketMapSector + 4]
	pop		ebx
	xor		ecx, ecx

	; addition
	add		eax, ebx
	adc		edx, ecx

	mov		esi, edx
	xor		edx, edx
	mov 	dx, word [MfsBootSector + MfsBootSectorStruct.BytesPerSector]
	mov 	ecx, 1
	xor		ebx, ebx
	mov 	es, bx
	mov 	bx, MEMLOCATION_FAT_GETCLUSTER
	push 	es
	push 	bx

	; Read Sector
	call 	ReadSector
	pop 	bx
	pop 	es

	; Find Entry
	pop 	edx
	xchg 	esi, edx
	shl		esi, 3
	mov 	ecx, dword [es:bx + si]
	mov 	eax, dword [es:bx + si + 4]
	mov 	esi, ecx

	; Restore & Return
	pop 	es
	pop 	edx
	pop 	ecx
	pop 	ebx
	ret

; **************************
; MFS ReadBucket
; IN: 
; 	- EAX BucketLength
;	- ESI Bucket
;	- ES:BX Buffer
;
; OUT:
; 	- EAX BucketLength
;	- ESI NextBucket
; **************************
ReadBucket:
	pushad

	; Save vars
	push	esi
	push 	bx
	push 	eax

	; Calculate Sector
	; Sector = Bucket * SectorsPerBucket
	mov		eax, esi
	xor		esi, esi
	mov		si, word [MfsBootSector + MfsBootSectorStruct.SectorsPerBucket]
	xor		ecx, ecx
	call	Mul64
	; eax is low dword
	; edx is high dword

	; Calculate sector count
	pop 	ecx
	push 	eax
	push 	edx
	xor 	eax, eax
	xor 	edx, edx
	mov 	ax, word [MfsBootSector + MfsBootSectorStruct.SectorsPerBucket]
	mul 	ecx
	mov 	ecx, eax

	; Restore earlier calcs 
	pop 	esi
	pop 	eax

	; Eax;esi is now the sector of data
	; Ecx is how many sectors to read
	xor 	ebx, ebx
	xor 	edx, edx
	pop 	bx
	mov 	dx, word [MfsBootSector + MfsBootSectorStruct.BytesPerSector]

	; Read
	call 	ReadSector

	; Save position
	pop		esi
	mov 	word [wMfsTemporary0], bx
	push 	es

	; Si still has cluster num, call next
	call 	GetNextBucket
	mov 	dword [wMfsTemporary1], esi
	mov 	dword [dMfsTemporary2], eax

	; Restore
	pop 	es

	; Done
	popad
	mov 	bx, word [wMfsTemporary0]
	mov 	esi, dword [wMfsTemporary1]
	mov 	eax, dword [dMfsTemporary2]
	ret


; ****************************
; MfsLoadFile
; IN:
; 	- DS:ESI: Filename
; 	- ES:BX: Buffer
; OUT:
;	- EAX: Filesize
; ****************************
MfsLoadFile:
	; Save arguments, we dont need them yet
	xchg 	bx, bx
	mov 	ax, es
	mov 	word [wMFSLoadFileFSegment], ax
	mov 	word [wMFSLoadFileFOffset], bx
	mov 	dword [dMFSLoadFileFName], esi
	mov		byte [bMfsEndOfString], 0

	; Step 1. Read root directory
	mov 	esi, dword [dRootBucket]

	; Read Loop
	.cLoop:
		mov 	bx, 0x0000
		mov 	es, bx
		mov 	bx, MEMLOCATION_FAT_FATTABLE

		; Calculate length of what we are looking for
		xor		eax, eax
		xor		ecx, ecx
		push	esi
		mov		esi, dword [dMFSLoadFileFName]

		.CountLoop:
			mov	al, [ds:esi]
			cmp al, 0x2F
			je .CountDone
			test al, al
			je .CountEnd
			inc ecx
			inc esi
			jmp .CountLoop

		.CountEnd:
			mov	byte [bMfsEndOfString], 1
		.CountDone:
			pop esi
		
		; ReadCluster returns next cluster in chain
		push	ecx
		mov 	eax, 1
		call 	ReadBucket
		pop		ecx
		push 	esi

		; Step 3. Parse entries and look for esi
		mov 	edi, MEMLOCATION_FAT_FATTABLE
		mov 	esi, dword [dMFSLoadFileFName]

		; Entry Count (dx)
		; Length of name in CX
		xor 	eax, eax
		xor 	ebx, ebx
		push	ecx
		xor		ecx, ecx
		mov 	bx, word [MfsBootSector + MfsBootSectorStruct.SectorsPerBucket]
		shr		ebx, 1
		mov 	edx, ebx
		pop		ecx

		; End of root?
		.EntryLoop:
			cmp 	[es:edi], ch
			je 		.ExitLoop

			; No, phew, lets check if filename matches
			pushad
			add		edi, 0x4C
        	repe    cmpsb
        	popad
        	jne 	.Next

        	; YAY WE FOUND IT!
        	; Get bucket
        	mov	    esi, dword [es:edi + 0x4]
        	mov 	eax, dword [es:edi + 0x8]

			; Was it end of chain?
			cmp		byte [bMfsEndOfString], 1
			je		.LoadKernel

			; Ok, so go deeper, set bucket as next
			mov		eax, esi
			pop		esi
			mov		esi, dword [dMFSLoadFileFName]
			add		esi, ecx
			inc		esi
			mov		dword [dMFSLoadFileFName], esi
			mov		esi, eax
			jmp		.cLoop

			.LoadKernel:
        	; Get filesize 
        	push 	eax
        	mov 	eax, dword [es:di + 0x3C]
        	mov 	dword [dMFSLoadFileFSize], eax

        	; Lets load the fuck out of this file
			mov 	ax, word [wMFSLoadFileFSegment]
			mov 	es, ax
			mov 	bx, word [wMFSLoadFileFOffset]
			pop 	eax

			; Load 
			.dLoop:
				; Clustertime
				call 	ReadBucket

				; Check
				cmp 	esi, 0xFFFFFFFF
				jne		.dLoop


        	; We are done, end this
        	pop 	eax 		; Fix stack
        	jmp 	.Success

        	; Next entry
        	.Next:
        		add     di, 0x400
        		dec 	dx
        		jnz 	.EntryLoop

		; Dont loop if esi is 0xFFFFFFFF
		pop 	esi
		cmp 	esi, 0xFFFFFFFF
		jne		.cLoop
		jmp 	.NotFound

	.ExitLoop:
		pop 	esi
		jmp 	.NotFound

	.Success:
		; Move filesize
		mov 	eax, dword [dMFSLoadFileFSize]
		ret

	.NotFound:
		; 0 Filesize
		mov 	eax, 0
		ret

; ****************************
; SetupMfs
; IN:
; 	- AL: 0 Expected
; 	- AH: Drive Number
; ****************************
SetupMfs:
	; Save State
	pushad
	xchg bx, bx

	; Save data
	mov 	byte [bMfsDriveNumber], ah

	; Copy bootcode to our structure
	mov 	esi, MEMLOCATION_BOOTCODE
	mov 	edi, MfsBootSector
	mov 	ecx, 0x34
	rep 	movsb

	; Done, I don't need distractions
	; when I do my calculations

	; Calculate where the bucket-map is
	xor 	eax, eax
	xor 	ebx, ebx
	xor		ecx, ecx

	; Get master bucket
	mov		eax, dword [MfsBootSector + MfsBootSectorStruct.MasterBucketSector]
	mov		esi, dword [MfsBootSector + MfsBootSectorStruct.MasterBucketSector + 4]
	mov		ecx, 1
	xor		ebx, ebx
	mov		es, bx
	mov		ebx, MEMLOCATION_FAT_GETCLUSTER
	call	ReadSector

	mov		eax, MEMLOCATION_FAT_GETCLUSTER
	mov		ebx, 12
	mov		ecx, [eax + ebx]
	mov		dword [dRootBucket], ecx
	mov 	ebx, 24
	mov 	ecx, [eax + ebx]
	mov 	dword [qBucketMapSector], ecx
	mov 	ecx, [eax + ebx + 4]
	mov 	dword [qBucketMapSector + 4], ecx

	; Done
	popad
	ret


; ****************************
; 64 Bit Division
; IN:
;	- EAX: Low Dword
;	- EDX: High Dword
;	- ECX: Divisor Value
; OUT:
;	- EAX: Low Dword Result
;	- EDX: High Dword Result
; ****************************
Div64:
	; Save ebx & init
	push ebx
	xor	ebx, ebx

	; Save lower 
	push eax
	mov eax, edx
	xor edx, edx

	; Don't divide by zero
	test eax, eax
	je .SkipHigh

	; Divide high by ECX
	div ecx 
	mov	ebx, eax
	
	.SkipHigh:
	pop eax
	div ecx
	mov edx, ebx
	pop ebx
	ret

; ****************************
; 64 Bit Mul
; IN:
;	- EAX: Right hand
;	- ESI: Low Dword left hand
;	- ECX: High Dword left hand
; OUT:
;	- EAX: Low Dword Result
;	- EDX: High Dword Result
; ****************************
Mul64:
	mov edx, eax
	sar edx, 31
	imul ecx, eax ; ecx = y_high *{signed} x
	mov ebx, edx
	imul ebx, esi ; ebx = sign_extension(x) *{signed} y_low
	add ecx, ebx ; ecx = y_high *{signed} x_low + x_high *{signed} y_low
	mul esi ; edx:eax = x_low *{unsigned} y_low
	lea edx, [ecx + edx] ; edx = high(x_low *{unsigned} y_low + y_high *{signed} x_low + x_high *{signed} y_low)
	ret

; ****************************
; Variables
; ****************************

; Strings - 0x0D (LineFeed), 0x0A (Carriage Return)


; Etc
bMfsDriveNumber					db 		0
bMfsEndOfString					db		0
qBucketMapSector				dq 		0
dRootBucket						dd		0
wMfsTemporary0					dw 		0
wMfsTemporary1					dd 		0
dMfsTemporary2					dd		0
dMFSLoadFileFName				dd 		0
dMFSLoadFileFSize				dd 		0
wMFSLoadFileFSegment 			dw 		0
wMFSLoadFileFOffset 			dw 		0


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
	at MfsBootSectorStruct.VolumeLabel,				db	0, 0, 0, 0, 0, 0, 0, 0
iend

%endif