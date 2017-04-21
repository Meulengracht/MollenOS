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
; Common.inc
; - Contains shared routines and shared data
; 

%ifndef _MBOOT_COMMON_INC_
%define _MBOOT_COMMON_INC_

BITS 16

; Definitions
%define 		BLACK		0x0
%define 		BLUE 		0x1
%define 		GREEN 		0x2
%define 		CYAN 		0x3
%define 		RED 		0x4
%define 		MAGENTA 	0x5
%define 		BROWN 		0x6
%define 		LGRAY 		0x7
%define 		DGRAY		0x8
%define 		LBLUE		0x9
%define 		LGREEN		0xA
%define 		LCYAN		0xB
%define 		LRED		0xC
%define 		LMAGENTA	0xD
%define 		YELLOW		0xE
%define 		WHITE		0xF

; Flags
%define 		MB_INFO_MEMORY			0x1
%define 		MB_INFO_BOOTDEVICE		0x2
%define 		MB_INFO_CMDLINE			0x4
%define 		MB_INFO_MODULES			0x8

; The next two are mutually exclusive
%define 		MB_INFO_AOUT			0x10
%define 		MB_INFO_ELF				0x20

; More Symbols
%define 		MB_INFO_MEM_MAP			0x40
%define 		MB_INFO_DRIVE_INFO		0x80
%define 		MB_INFO_CONFIG_TABLE	0x100
%define 		MB_INFO_BOOT_LDR_NAME	0x200
%define 		MB_INFO_APM_TABLE		0x400
%define 		MB_INFO_VIDEO_INFO		0x800

; R/EAX must contain this
%define 		MULTIBOOT_MAGIC			0x2BADB002

; Structure Definitions
struc MultiBoot
	.Flags				resd	1
	.MemoryLo			resd	1
	.MemoryHi			resd	1
	.BootDevice			resd	1
	.CmdLine			resd	1
	.ModuleCount		resd	1
	.ModuleAddr			resd	1
	.Symbols0			resd	1
	.Symbols1			resd	1
	.Symbols2			resd	1
	.Symbols3			resd	1
	.MemMapLength		resd	1
	.MemMapAddr			resd	1
	.DrivesLength		resd	1
	.DrivesAddr			resd	1
	.ROMConfigTable		resd	1
	.BootLoaderName		resd	1
	.ApmTable			resd	1
	.VBEControllerInfo	resd	1
	.VBEModeInfo		resd	1
	.VBEMode			resd	1
	.VBEInterfaceSeg	resd	1
	.VBEInterfaceOff	resd	1
	.VBEInterfaceLen	resd	1
endstruc

struc MollenOsBootDescriptor
	.KernelAddr 		resd 	1
	.KernelSize		resd 	1
	.RamDiskAddr		resd 	1
	.RamDiskSize 		resd 	1
	.ExportAddr		resd 	1
	.ExportSize		resd 	1
	.SymbolsAddr		resd	1
	.SymbolsSize		resd	1
endstruc

; ********************************
; SetTextColor
; IN:
; 	- AL: Background Color
;	- AH: Text Color
; ********************************
SetTextColor:
	; Save state
	push 	ax
	push 	bx

	; Pack AL/AH into BL
	mov 	bl, ah
	shl 	bl, 4
	and 	al, 0x0F
	or 		bl, al

	; Done
	mov 	byte [bTextColor], bl

	; Restore & Return
	pop 	bx
	pop 	ax
	ret


; ********************************
; PrintChar
; IN:
; 	- AL: Char
; ********************************
PrintChar:
	; Save state
	push 	ax
	push 	bx

	; Setup INT
	mov 	ah, 0x0E
	mov 	bl, byte [bTextColor]
	mov 	bh, 0x00
	int 	0x10

	; Restore & Return
	pop 	bx
	pop 	ax
	ret

; ********************************
; Print
; IN:
; 	- SI: NULL-Terminated String
; ********************************
Print:
	; Save state
	pushad

	; Loop
	.cLoop:
		; Load next byte from string
		lodsb

		; Check if NULL (EOS)
		or	al, al
		jz	.PrintDone

		; Print it
		call PrintChar
		jmp	.cLoop

	.PrintDone:
		; Restore & Return
		popad
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
	xor 	ebx, ebx
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
	    add 	edx, 48

	    ; Store it
	    push 	edx
	    inc 	ebx

	    ; if eax is zero, we can quit
	    cmp eax, 0
	    jnz .DigitLoop

	.PrintLoop:
		pop 	eax

		; Print it
		call 	PrintChar

		; Decrease
		dec 	ebx
		jnz 	.PrintLoop

    ; Done
    popad
    ret

; ********************************
; SystemsFail
; Only call if fatal error
; ********************************
SystemsFail:
	int     0x16                    ; await keypress
	int     0x19                    ; warm boot computer

	; No return from this
	cli
	hlt

; ********************************
; Variables
; ********************************
bTextColor							db 		0x0F


; ********************************
; MultiBoot Header
; ********************************
align 32
BootHeader:
istruc MultiBoot
	at MultiBoot.Flags,				dd 0
	at MultiBoot.MemoryLo,			dd 0
	at MultiBoot.MemoryHi,			dd 0
	at MultiBoot.BootDevice,		dd 0
	at MultiBoot.CmdLine,			dd 0
	at MultiBoot.ModuleCount,		dd 0
	at MultiBoot.ModuleAddr,		dd 0
	at MultiBoot.Symbols0,			dd 0
	at MultiBoot.Symbols1,			dd 0
	at MultiBoot.Symbols2,			dd 0
	at MultiBoot.Symbols3, 			dd 0
	at MultiBoot.MemMapLength,		dd 0
	at MultiBoot.MemMapAddr,		dd 0
	at MultiBoot.DrivesLength,		dd 0
	at MultiBoot.DrivesAddr,		dd 0
	at MultiBoot.ROMConfigTable,	dd 0
	at MultiBoot.BootLoaderName,	dd 0
	at MultiBoot.ApmTable,			dd 0
	at MultiBoot.VBEControllerInfo,	dd 0
	at MultiBoot.VBEModeInfo,		dd 0
	at MultiBoot.VBEMode,			dd 0
	at MultiBoot.VBEInterfaceSeg,	dd 0
	at MultiBoot.VBEInterfaceOff,	dd 0
	at MultiBoot.VBEInterfaceLen,	dd 0
iend

; ********************************
; Os Boot Header
; ********************************
align 32
BootDescriptor:
istruc MollenOsBootDescriptor
	at MollenOsBootDescriptor.KernelAddr, 		dd 	0
	at MollenOsBootDescriptor.KernelSize,		dd 	0
	at MollenOsBootDescriptor.RamDiskAddr,		dd 	0
	at MollenOsBootDescriptor.RamDiskSize, 		dd 	0
	at MollenOsBootDescriptor.ExportAddr,		dd 	0
	at MollenOsBootDescriptor.ExportSize,		dd 	0
	at MollenOsBootDescriptor.SymbolsAddr,		dd	0
	at MollenOsBootDescriptor.SymbolsSize,		dd	0
iend

%endif