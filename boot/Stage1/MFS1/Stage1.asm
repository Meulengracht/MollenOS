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
; Mollen-OS Stage 1 Bootloader - MFS1
; Version 1.0
; *******************************************************
; Memory Map:
; 0x00000000 - 0x000004FF		Reserved
; 0x00000500 - 0x00007AFF		Second Stage Bootloader (~29 Kb)
; 0x00007B00 - 0x00007BFF		Stack Space (256 Bytes)
; 0x00007C00 - 0x00007DFF		Bootloader (512 Bytes)
; 0x00007E00 - 0x0007FFFF		Kernel Loading Bay (480.5 Kb)
; Rest above is not reliable


; 16 Bit Code, Origin at 0x0
BITS 16
ORG 0x7C00


; Jump Code, 3 Bytes
jmp short Main
nop

; *************************
; MFS1 Boot Parameter Block
; *************************
dMagic						dd		0
bVersion					db		0
bFlags						db		0
bMediaType					db		0
wBytesPerSector				dw		0
wSectorsPerTrack			dw		0
wHeadsPerCylinder			dw		0
qTotalSectors				dq		0

wReservedSectorCount		dw		0
wSectorsPerBucket			dw		0

qMasterBucketSector			dq		0
qMasterBucketMirror			dq		0
szVolumeLabel				db		"MollenOS"

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

	; Step 1. Load Stage2
	xor		eax, eax
	mov		es, ax
	mov		bx, 0x0500
	mov		ax, 1
	mov		cx, word [wReservedSectorCount]
	call	ReadSector

	; Done, jump
	mov 	dl, byte [bPhysicalDriveNum]
	mov 	dh, 5
	jmp 	0x0:0x500

	; Safety catch
	cli
	hlt

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
bPhysicalDriveNum				db		0

; Fill out bootloader
times 510-($-$$) db 0

; Boot Signature
db 0x55, 0xAA