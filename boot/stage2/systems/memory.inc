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
; Memory.inc
; - Contains routines for detecting available memory
; 

%ifndef _MBOOT_MEMORY_INC_
%define _MBOOT_MEMORY_INC_

BITS 16

; Definitions
%define 		BIOS_MEMMAP_SIGNATURE		0x0534D4150


; ********************************
; LoadMemoryMap
; IN:
;	- ES:EDI: Destination Buffer
; OUT:
; 	- EBX: 0 -> Success, 1 -> Err
; ********************************
LoadMemoryMap:
	; Save buffer
	push 	edi

	; Clear out space for memory map (1 Page)
	xor 	eax, eax
	mov 	ecx, 1024
	rep 	stosd

	; Restore
	pop 	edi

	; Setup INT 0x15
	mov 	eax, 0xE820
	xor 	ebx, ebx
	xor 	ebp, ebp
	mov 	ecx, 24
	mov 	edx, BIOS_MEMMAP_SIGNATURE

	; Ready, make sure we force a valid ACPI 3.X Entry
	mov 	[es:di + 20], dword 1

	; Gogo!
	int 	0x15
	jc 		.NoMemoryMap

	; Restore EDX for safety (EAX contains signature on success)
	mov 	edx, BIOS_MEMMAP_SIGNATURE
	cmp 	eax, edx
	jne 	.NoMemoryMap

	; If ebx is 0, only 1 entry, we cant use that for shit
	test 	ebx, ebx
	je 		.NoMemoryMap
	jmp 	.MediasRes

	; Loop time
	.cLoop:
		; Query time
		mov 	eax, 0xE820
		mov 	ecx, 24
		mov 	[es:di + 20], dword 1
		int 	0x15
		jc 		.Done
		mov 	edx, BIOS_MEMMAP_SIGNATURE

	.MediasRes:
		; Parse Entry
		test 	ecx, ecx
		je 		.SkipEntry
		cmp 	cl, 20
		jbe 	.Acpi2
		test 	byte [es:di + 20], 1
		je 		.SkipEntry

	.Acpi2:
		; Acpi2 Entry

		; Get lower dword of memory length
		mov 	ecx, [es:di + 8]
		test 	ecx, ecx
		jne 	.GoodEntry

		; Get higher dword of memory length
		mov 	ecx, [es:di + 12]
		test 	ecx, ecx
		je 		.SkipEntry

	.GoodEntry:
		; Increase count & move to next
		inc 	ebp
		add 	edi, 0x20

	.SkipEntry:
		; If ebx rsets to 0, list is complete
		test 	ebx, ebx
		jne 	.cLoop
		jmp 	.Done

	.NoMemoryMap:
		;Fuck...
		mov 	ebx, 1
		mov 	ebp, 0
		ret

	.Done:
		;Yay!
		mov 	ebx, 0
		ret

; ********************************
; GetMemorySize
; OUT:
; 	- EAX: KB between 1 mb and 16 mb (0 on err)
; 	- EBX: Num of 64 Kb blocks above 16 mb (0 on err)
; ********************************
GetMemorySize:
	; Clear registers
	xor 	ecx, ecx
	xor 	edx, edx
	
	; Setup INT 0x15
	mov 	eax, 0xE801
	int 	0x15

	; Error?
	jc 		.Error

	; Unsupported??
	cmp 	ah, 0x86
	je 		.Error

	; Invalid Cmd??
	cmp 	ah, 0x80
	je 		.Error

	; It is possible that bios uses ECX/EDX instead
	test 	ecx, ecx
	je 		.UseEAX

	; Yea they did
	mov 	eax, ecx
	mov 	ebx, edx

	.UseEAX:
		; Done!
		ret

	.Error:
		; Damn, ffs
		mov 	eax, 0
		mov 	ebx, 0
		ret
	

; ********************************
; SetupMemory
; ********************************
SetupMemory:
	; Save state 
	pushad

	; Print
	mov 	esi, szPrefix
	call 	Print

	mov 	esi, szMemorySetup
	call 	Print

	; Step 1, try to get memory map
	mov 	edi, MEMLOCATION_MEMORY_MAP
	mov 	dword [BootHeader + MultiBoot.MemMapAddr], edi
	call 	LoadMemoryMap

	; Save entry count
	mov 	dword [BootHeader + MultiBoot.MemMapLength], ebp

	; Step 2. try to get memory size
	call 	GetMemorySize

	; Ok if both EAX & EBX is 0, we have a serious problem
	cmp 	ebx, 0
	jne 	.NoError
	cmp 	eax, 0
	jne 	.NoError

	.Error:
		; Nooo :(
		mov 	esi, szFailed
		call 	Print

		; Halt
		call 	SystemsFail
		cli
		hlt

	.NoError:
		; Add lower 1 mb and save
		add 	eax, 0x400
		mov 	dword [BootHeader + MultiBoot.MemoryLo], eax
		mov 	dword [BootHeader + MultiBoot.MemoryHi], ebx

		; Print
		mov 	esi, szSuccess
		call 	Print

		; Restore
		popad
		ret 



; ********************************
; Variables
; ********************************

; Strings - 0x0D (LineFeed), 0x0A (Carriage Return)
szMemorySetup 						db 		"Retrieving Memory Information", 0x00

%endif