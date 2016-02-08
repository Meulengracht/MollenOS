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
; Mollen-OS Stage 1 Bootloader - FAT32 
; Version 1.0
; *******************************************************
; Memory Map:
; 0x00000000 - 0x000004FF		Reserved
; 0x00000500 - 0x00007AFF		Second Stage Bootloader (~29 Kb)
; 0x00007B00 - 0x00007BFF		Stack Space (256 Bytes)
; 0x00007C00 - 0x00007DFF		Bootloader (512 Bytes)
; 0x00007E00 - 0x0007FFFF		Kernel Loading Bay (480.5 Kb)
; Rest above is not reliable

; In this bootloader we use the kernel loading bay a lot
; 0x00007E00 - 0x00007FFF 		GetNextCluster (Usage)
; 0x00008000 - ClusterSize		Used by LocateFile Procedure


; 16 Bit Code, Origin at 0x0
BITS 16
ORG 0x7C00


; Jump Code, 3 Bytes
jmp short Main
nop

; *************************
; FAT Boot Parameter Block
; *************************
szOemName					db		"MollenOS"
wBytesPerSector				dw		0
bSectorsPerCluster			db		0
wReservedSectors			dw		0
bNumFATs					db		0
wRootEntries				dw		0
wTotalSectors				dw		0
bMediaType					db		0
wSectorsPerFat				dw		0
wSectorsPerTrack			dw		0
wHeadsPerCylinder			dw		0
dHiddenSectors				dd 		0
dTotalSectors				dd 		0

; *************************
; FAT32 Extension Block
; *************************
dSectorsPerFat32			dd 		0
wFlags						dw		0
wVersion					dw		0
dRootDirStart				dd 		0
wFSInfoSector				dw		0
wBackupBootSector			dw		0

; Reserved 
dReserved0					dd		0 	;FirstDataSector
dReserved1					dd		0 	;ReadCluster
dReserved2					dd 		0 	;ReadCluster

bPhysicalDriveNum			db		0
bReserved3					db		0
bBootSignature				db		0
dVolumeSerial				dd 		0
szVolumeLabel				db		"NO NAME    "
szFSName					db		"FAT32   "


; *************************
; Bootloader Entry Point
; *************************

Main:
	; Disable Interrupts, unsafe passage
	cli

	; Far jump to fix segment registers
	jmp 	0x0:FixCS

FixCS:
	; Fix segment registers to 0
	xor 	ax, ax
	mov		ds, ax
	mov		es, ax

	; Set stack
	mov		ss, ax
	mov		ax, 0x7C00
	mov		sp, ax

	; Done, now we need interrupts again
	sti

	; Step 0. Save DL
	mov 	byte [bPhysicalDriveNum], dl

	; Step 1. Calculate FAT32 Data Sector
	xor		eax, eax
	mov 	al, byte [bNumFATs]
	mov 	ebx, dword [dSectorsPerFat32]
	mul 	ebx
	xor 	ebx, ebx
	mov 	bx, word [wReservedSectors]
	add 	eax, ebx
	mov 	dword [dReserved0], eax

	; Step 2. Read FAT Table
	mov 	esi, dword [dRootDirStart]

	; Read Loop
	.cLoop:
		mov 	bx, 0x0000
		mov 	es, bx
		mov 	bx, 0x8000
		
		; ReadCluster returns next cluster in chain
		call 	ReadCluster
		push 	esi

		; Step 3. Parse entries and look for szStage2
		mov 	di, 0x8000
		mov 	si, szStage2
		mov 	cx, 0x000B
		mov 	dx, 0x0020
		;mul by bSectorsPerCluster

		; End of root?
		.EntryLoop:
			cmp 	[es:di], ch
			je 		.cEnd

			; No, phew, lets check if filename matches
			cld
			pusha
        	repe    cmpsb
        	popa
        	jne 	.Next

        	; YAY WE FOUND IT!
        	; Get clusterLo & clusterHi
        	push    word [es:di + 14h]
        	push    word [es:di + 1Ah]
        	pop     esi
        	pop 	eax ; fix stack
        	jmp 	LoadFile

        	; Next entry
        	.Next:
        		add     di, 0x20
        		dec 	dx
        		jnz 	.EntryLoop

		; Dont loop if esi is above 0x0FFFFFFF5
		pop 	esi
		cmp 	esi, 0x0FFFFFF8
		jb 		.cLoop

	; Ehh if we reach here, not found :s
	.cEnd:
	mov 	eax, 1
	call 	PrintNumber
	cli
	hlt

; **************************
; Load 2 Stage Bootloader
; IN:
; 	- ESI Start cluster of file
; **************************
LoadFile:
	; Lets load the fuck out of this file
	; Step 1. Setup buffer
	mov 	bx, 0x0000
	mov 	es, bx
	mov 	bx, 0x0500

	; Load 
	.cLoop:
		; Clustertime
		call 	ReadCluster

		; Check
		cmp 	esi, 0x0FFFFFF8
		jb 		.cLoop

	; Done, jump
	mov 	dl, byte [bPhysicalDriveNum]
	mov 	dh, 4
	jmp 	0x0:0x500

	; Safety catch
	cli
	hlt


; **************************
; FAT ReadCluster
; IN: 
;	- ES:BX Buffer
;	- SI ClusterNum
;
; OUT:
;	- ESI NextClusterInChain
; **************************
ReadCluster:
	pusha

	; Save Bx
	push 	bx

	; Calculate Sector
	; FirstSectorofCluster = ((N – 2) * BPB_SecPerClus) + FirstDataSector;
	xor 	eax, eax
	xor 	bx, bx
	xor 	ecx, ecx
	mov 	ax, si
	sub 	ax, 2
	mov 	bl, byte [bSectorsPerCluster]
	mul 	bx
	add 	eax, dword [dReserved0]

	; Eax is now the sector of data
	pop 	bx
	mov 	cl, byte [bSectorsPerCluster]

	; Read
	call 	ReadSector

	; Save position
	mov 	word [dReserved2], bx
	push 	es

	; Si still has cluster num, call next
	call 	GetNextCluster
	mov 	dword [dReserved1], esi

	; Restore
	pop 	es

	; Done
	popa
	mov 	bx, word [dReserved2]
	mov 	esi, dword [dReserved1]
	ret

; **************************
; BIOS ReadSector 
; IN:
; 	- ES:BX: Buffer
;	- AX: Sector start
; 	- CX: Sector count
;
; Registers:
; 	- Conserves all but ES:BX
; **************************
ReadSector:
	; Error Counter
	.Start:
		mov 	di, 5

	.sLoop:
		; Save states
		push 	ax
		push 	bx
		push 	cx

		; Convert LBA to CHS
		xor     dx, dx
        div     WORD [wSectorsPerTrack]
        inc     dl ; adjust for sector 0
        mov     cl, dl ;Absolute Sector
        xor     dx, dx
        div     WORD [wHeadsPerCylinder]
        mov     dh, dl ;Absolute Head
        mov     ch, al ;Absolute Track

        ; Bios Disk Read -> 01 sector
		mov 	ax, 0x0201
		mov 	dl, byte [bPhysicalDriveNum]
		int 	0x13
		jnc 	.Success

	.Fail:
		; HAHA fuck you
		xor 	ax, ax
		int 	0x13
		dec 	di
		pop 	cx
		pop 	bx
		pop 	ax
		jnz 	.sLoop
		
		; Give control to next OS, we failed 
		mov 	eax, 2
		call 	PrintNumber
		cli 
		hlt

	.Success:
		; Next sector
		pop 	cx
		pop 	bx
		pop 	ax

		add 	bx, word [wBytesPerSector]
		jnc 	.SkipEs
		mov 	dx, es
		add 	dh, 0x10
		mov 	es, dx

	.SkipEs:
		inc 	ax
		loop 	.Start

	; Done
	ret

; **************************
; GetNextCluster
; IN:
; 	- SI ClusterNum
;
; OUT:
;	- ESI NextClusterNum
;
; Registers:
; 	- Trashes EAX, BX, ECX, EDX, ES
; **************************
GetNextCluster:
	; Calculte Sector in FAT
	xor 	eax, eax
	xor 	edx, edx
	mov 	ax, si
	shl 	ax, 2 			; REM * 4, since entries are 32 bits long, and not 8
	div 	word [wBytesPerSector]
	add 	ax, word [wReservedSectors]
	push 	dx

	; AX contains sector
	; DX contains remainder
	mov 	ecx, 1
	mov 	bx, 0x0000
	mov 	es, bx
	mov 	bx, 0x7E00
	push 	es
	push 	bx

	; Read Sector
	call 	ReadSector
	pop 	bx
	pop 	es

	; Find Entry
	pop 	dx
	xchg 	si, dx
	mov 	esi, dword [es:bx + si]
	ret



; ********************************
; PrintChar
; IN:
; 	- al: Char to print
; ********************************
PrintChar:
	pusha

	; Setup INT
	mov 	ah, 0x0E
	mov 	bx, 0x00
	int 	0x10

	; Restore & Return
	popa
	ret

; ********************************
; PrintNumber
; IN:
; 	- EAX: NumberToPrint
; ********************************
PrintNumber:
	; Save state
	pushad

	; Loops
	xor 	bx, bx
    mov 	ecx, 10

	.DigitLoop:
	    xor 	edx, edx
	    div 	ecx

	    ; now eax <-- eax/10
	    ;     edx <-- eax % 10

	    ; print edx
	    ; this is one digit, which we have to convert to ASCII
	    ; the print routine uses edx and eax, so let's push eax
	    ; onto the stack. we clear edx at the beginning of the
	    ; loop anyway, so we don't care if we much around with it

	    ; convert dl to ascii
	    add 	dx, 48

	    ; Store it
	    push 	dx
	    inc 	bx

	    ; if eax is zero, we can quit
	    cmp 	eax, 0
	    jnz 	.DigitLoop

	.PrintLoop:
		pop 	ax

		; Print it
		call 	PrintChar

		; Decrease
		dec 	bx
		jnz 	.PrintLoop

    ; Done
    popad
    ret

; **************************
; Variables
; **************************
szStage2					db 		"SSBL    STM"

; Fill out bootloader
times 510-($-$$) db 0

; Boot Signature
db 0x55, 0xAA