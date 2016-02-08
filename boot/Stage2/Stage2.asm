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
; Memory Map:
; 0x00000000 - 0x000004FF		Reserved
; 0x00000500 - 0x00007AFF		Second Stage Bootloader (~29 Kb)
; 0x00007B00 - 0x00007BFF		Stack Space (256 Bytes)
; 0x00007C00 - 0x00007DFF		Bootloader (512 Bytes)
; 0x00007E00 - 0x00008FFF		Used by subsystems in this bootloader
; 0x00009000 - 0x00009FFF		Memory Map
; 0x0000A000 - 0x0000AFFF		Vesa Mode Map / Controller Information
; 0x0000B000 - 0x0007FFFF		File Loading Bay (467 Kb ? ish)
; Rest above is not reliable

; 16 Bit Code, Origin at 0x500
BITS 16
ORG 0x0500

; *****************************
; REAL ENTRY POINT
; *****************************
jmp Entry

; Definitions
%define 		MEMLOCATION_BOOTCODE			0x7C00
%define 		MEMLOCATION_FAT_GETCLUSTER		0x7E00
%define 		MEMLOCATION_FAT_FATTABLE		0x8000
%define 		MEMLOCATION_MEMORY_MAP			0x9000
%define 		MEMLOCATION_VESA_INFO_BASE		0xA000
%define 		MEMLOCATION_FLOAD_SEGMENT		0x0000
%define 		MEMLOCATION_FLOAD_OFFSET		0xB000
%define 		MEMLOCATION_FLOAD_LOWER			0xB000 
%define 		MEMLOCATION_KERNEL_UPPER		0x100000
%define 		MEMLOCATION_RAMDISK_UPPER		0x200000

; Includes
%include "Systems/Common.inc"
%include "Systems/Memory.inc"
%include "Systems/A20.inc"
%include "Systems/Gdt.inc"
%include "Systems/Vesa.inc"
%include "Systems/PELoader.inc"

; FileSystem Includes
%include "Systems/FsCommon.inc"

; *****************************
; Entry Point
; *****************************
Entry:
	; Clear interrupts
	cli

	; Clear registers (Not EDX, it contains stuff!)
	xor 	eax, eax
	xor 	ebx, ebx
	xor 	ecx, ecx
	xor 	esi, esi
	xor 	edi, edi

	; Setup segments
	mov		ds, ax
	mov		es, ax
	mov		fs, ax
	mov		gs, ax

	; Setup stack
	mov		ss, ax
	mov		ax, 0x7BFF
	mov		sp, ax
	xor 	ax, ax

	; Enable interrupts
	sti

	; Save information passed by Stage1
	mov 	byte [bDriveNumber], dl
	mov 	byte [bStage1Type], dh

	; Now we can clear EDX
	xor 	edx, edx

	; Set video mode to 80x25 (Color Mode)
	mov 	al, 0x03
	mov 	ah, 0x00
	int 	0x10

	; Set text color
	mov 	al, BLACK
	mov 	ah, GREEN
	call 	SetTextColor

	; Print Message
	mov 	esi, szWelcome0
	call 	Print

	mov 	esi, szWelcome1
	call 	Print

	mov 	esi, szWelcome2
	call 	Print

	mov 	esi, szWelcome3
	call 	Print

	; Set basic
	mov 	esi, szBootloaderName
	mov 	dword [BootHeader + MultiBoot.BootLoaderName], esi

	; Get memory map
	call 	SetupMemory

	; Enable A20 Gate
	call 	EnableA20

	; Install GDT
	call 	InstallGdt

	; VESA System Select
	call 	VesaSetup

	; Setup FileSystem (Based on Stage1 Type)
	xor 	eax, eax
	mov 	al, byte [bStage1Type]
	mov 	ah, byte [bDriveNumber]
	call 	SetupFS

	; Load Kernel
	mov 	esi, szPrefix
	call 	Print

	mov 	esi, szLoadingKernel
	call 	Print

	mov 	esi, szKernel
	mov		edi, szKernelUtf
	xor 	eax, eax
	xor 	ebx, ebx
	mov 	ax, MEMLOCATION_FLOAD_SEGMENT
	mov 	es, ax
	mov 	bx, MEMLOCATION_FLOAD_OFFSET
	call 	LoadFile

	; Did it load correctly?
	cmp 	eax, 0
	jne 	Continue

	; Damnit
	mov 	esi, szFailed
	call 	Print

	; Fuckup
	call 	SystemsFail

Continue:
	; Save
	mov 	dword [dKernelSize], eax

	; Print
	mov 	esi, szSuccess
	call 	Print

	; Now, the tricky thing comes
	; We must go to 32-bit, copy the kernel
	; to 0x1000000, then return back
	; so we can load ramdisk

	; GO PROTECTED MODE!
	mov		eax, cr0
	or		eax, 1
	mov		cr0, eax

	; Jump into 32 bit
	jmp 	CODE_DESC:LoadKernel32

LoadRamDisk:
	; Load 16 Idt 
	call	LoadIdt16

	; Disable Protected mode
	mov		eax, cr0
	and		eax, 0xFFFFFFFE
	mov		cr0, eax

	; Far jump to real mode unprotected
	jmp 	0:LeaveProtected

LeaveProtected:
	; Clear registers
	xor 	eax, eax
	xor 	ebx, ebx
	xor 	ecx, ecx
	xor		edx, edx
	xor 	esi, esi
	xor 	edi, edi

	; Setup segments, leave 16 bit protected mode
	mov		ds, ax
	mov		es, ax
	mov		fs, ax
	mov		gs, ax

	; Setup stack
	mov		ss, ax
	mov		ax, 0x7BFF
	mov		sp, ax
	xor 	ax, ax

	; Enable interrupts
	sti

	; Print load ramdisk
	mov 	esi, szPrefix
	call 	Print

	mov 	esi, szLoadingRamDisk
	call 	Print

	; Actually load it
	mov 	esi, szRamDisk
	mov		edi, szRamDiskUtf
	xor 	eax, eax
	xor 	ebx, ebx
	mov 	ax, MEMLOCATION_FLOAD_SEGMENT
	mov 	es, ax
	mov 	bx, MEMLOCATION_FLOAD_OFFSET
	call 	LoadFile

	; Did it load correctly?
	cmp 	eax, 0
	jne 	Finish16Bit

	; Damnit
	mov 	esi, szFailed
	call 	Print

	; Fuckup
	call 	SystemsFail

Finish16Bit:
	; Save
	mov 	dword [BootDescriptor + MollenOsBootDescriptor.RamDiskSize], eax
	mov	eax, MEMLOCATION_RAMDISK_UPPER
	mov 	dword [BootDescriptor + MollenOsBootDescriptor.RamDiskAddr], eax

	; Print
	mov 	esi, szSuccess
	call 	Print

	; Print last message 
	mov 	esi, szPrefix
	call 	Print

	mov 	esi, szFinishBootMsg
	call 	Print

	; Switch Video Mode
	call 	VesaFinish

	; GO PROTECTED MODE!
	mov		eax, cr0
	or		eax, 1
	mov		cr0, eax

	; Jump into 32 bit
	jmp 	CODE_DESC:Entry32

align 32
; ****************************
; 32 Bit Stage Below 
; ****************************
BITS 32

; 32 Bit Includes
%include "Systems/Cpu.inc"

LoadKernel32:
	; Disable Interrupts
	cli

	; Setup Segments, Stack etc
	xor 	eax, eax
	mov 	ax, DATA_DESC
	mov 	ds, ax
	mov 	fs, ax
	mov 	gs, ax
	mov 	ss, ax
	mov 	es, ax
	mov 	esp, 0x7BFF

	; Kernel Relocation to 1mb (PE, ELF, binary)
	mov 	esi, MEMLOCATION_FLOAD_LOWER
	mov 	edi, MEMLOCATION_KERNEL_UPPER
	call 	PELoad

	; Save entry
	mov 	dword [dKernelEntry], ebx

	; Perfect, now we must return to 16 bit
	; So we can load the RD
	
	; Load 16-bit protected mode descriptor
	mov eax, DATA16_DESC
	mov ds, eax
	mov es, eax
	mov fs, eax
	mov gs, eax
	mov ss, eax

	; Jump to protected real mode, set CS!
	jmp		CODE16_DESC:LoadRamDisk

Entry32:
	; Setup Segments, Stack etc
	xor 	eax, eax
	mov 	ax, DATA_DESC
	mov 	ds, ax
	mov 	fs, ax
	mov 	gs, ax
	mov 	ss, ax
	mov 	es, ax
	mov 	esp, 0x7BFF

	; Disable ALL irq
	mov 	al, 0xff
	out 	0xa1, al
	out 	0x21, al

	; But we cli aswell
	cli

	; RamDisk Relocation to 2mb
	mov 	esi, MEMLOCATION_FLOAD_LOWER
	mov 	edi, MEMLOCATION_RAMDISK_UPPER
	mov		ecx, dword [BootDescriptor + MollenOsBootDescriptor.RamDiskSize]
	shr		ecx, 2
	inc		ecx
	rep		movsd

	; Setup Cpu
	call	CpuInit

	; If eax is set to 1, 
	; we will enter 64 bit mode instead (todo)

	; Setup Registers
	xor 	esi, esi
	xor 	edi, edi
	mov 	ecx, dword [dKernelEntry]
	mov 	eax, MULTIBOOT_MAGIC
	mov 	ebx, BootHeader
	mov 	edx, BootDescriptor

	; MultiBoot structure also needs to be on stack
	push 	ebx

	; Jump to kernel (Entry Point in ECX)
	jmp 	ecx

	; Safety
	cli
	hlt

; ****************************
; Variables
; ****************************

; Strings - 0x0D (LineFeed), 0x0A (Carriage Return)
szBootloaderName				db 		"mBoot Version 1.0.0, Author: Philip Meulengracht", 0x00
szWelcome0 						db 		"                ***********************************************", 0x0D, 0x0A, 0x00
szWelcome1						db 		"                * MollenOS Stage 2 Bootloader (Version 1.0.0) *", 0x0D, 0x0A, 0x00
szWelcome2						db 		"                * Author: Philip Meulengracht                 *", 0x0D, 0x0A, 0x00
szWelcome3 						db 		"                ***********************************************", 0x0D, 0x0A, 0x0A, 0x00
szPrefix 						db 		"                   - ", 0x00
szSuccess						db 		" [OK]", 0x0D, 0x0A, 0x00
szFailed						db 		" [FAIL]", 0x0D, 0x0A, 0x00
szLoadingKernel					db 		"Loading MollenOS Kernel", 0x00
szLoadingRamDisk				db 		"Loading MollenOS RamDisk", 0x00
szFinishBootMsg 				db 		"Finishing Boot Sequence", 0x0D, 0x0A, 0x00

szKernel						db 		"MCORE   MOS"
szRamDisk						db		"INITRD  MOS"
szKernelUtf						db		"System/Sys32.mos", 0x0
szRamDiskUtf					db		"System/InitRd32.mos", 0x0

; Practical stuff
bDriveNumber 					db 		0
dKernelSize						dd 		0
dKernelEntry						dd		0

; 2 -> FAT12, 3 -> FAT16, 4 -> FAT32
; 5 -> MFS1
bStage1Type						db 		0