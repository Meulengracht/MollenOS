; MollenOS
;
; Copyright 2011, Philip Meulengracht
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
; *******************************************************
; Common.inc
; - Contains shared routines and shared data
; 

%ifndef _MBOOT_COMMON16_INC_
%define _MBOOT_COMMON16_INC_

BITS 16

; Calling convention helpers
%macro STACK_FRAME_BEGIN16 0
    push bp
    mov bp, sp
%endmacro
%macro STACK_FRAME_END16 0
    mov sp, bp
    pop bp
%endmacro

%define ARG0_16 [bp + 4]
%define ARG1_16 [bp + 6]
%define ARG2_16 [bp + 8]
%define ARG3_16 [bp + 10]
%define ARG4_16 [bp + 12]
%define ARG5_16 [bp + 14]

%define LVAR0_16 word [bp - 2]
%define LVAR1_16 word [bp - 4]
%define LVAR2_16 word [bp - 6]
%define LVAR3_16 word [bp - 8]
%define LVAR4_16 word [bp - 10]

%macro STACK_FRAME_BEGIN32 0
    push ebp
    mov ebp, esp
%endmacro
%macro STACK_FRAME_END32 0
    mov esp, ebp
    pop ebp
%endmacro

%define ARG0_32 [ebp + 8]
%define ARG1_32 [ebp + 12]
%define ARG2_32 [ebp + 16]
%define ARG3_32 [ebp + 20]
%define ARG4_32 [ebp + 24]
%define ARG5_32 [ebp + 28]

%define XARG0_32 [esp + 4]
%define XARG1_32 [esp + 8]
%define XARG2_32 [esp + 12]
%define XARG3_32 [esp + 16]
%define XARG4_32 [esp + 20]
%define XARG5_32 [esp + 24]

%define LVAR0_32 dword [ebp - 4]
%define LVAR1_32 dword [ebp - 8]
%define LVAR2_32 dword [ebp - 12]
%define LVAR3_32 dword [ebp - 16]
%define LVAR4_32 dword [ebp - 20]

; Utilities for me
%macro TRACE16 1
    push %1
    call Print16
    add sp, 2
%endmacro

; IO ports
%define UART_PORT_BASE          0x3F8
%define UART_PORT_DATA          UART_PORT_BASE
%define UART_PORT_LINE_STATUS   UART_PORT_BASE + 5

; Definitions
%define BLACK		0x0
%define BLUE 		0x1
%define GREEN 		0x2
%define CYAN 		0x3
%define RED 		0x4
%define MAGENTA 	0x5
%define BROWN 		0x6
%define LGRAY 		0x7
%define DGRAY		0x8
%define LBLUE		0x9
%define LGREEN		0xA
%define LCYAN		0xB
%define LRED		0xC
%define LMAGENTA	0xD
%define YELLOW		0xE
%define WHITE		0xF

%define VBOOT_MAGIC   0xAEB007AE
%define VBOOT_VERSION 0x00010000

%define VBOOT_FIRMWARE_BIOS 0

%define VBOOT_MEMORY_TYPE_RESERVED  0x00000000
%define VBOOT_MEMORY_TYPE_FIRMWARE  0x00000001
%define VBOOT_MEMORY_TYPE_ACPI      0x00000002
%define VBOOT_MEMORY_TYPE_NVS       0x00000003
%define VBOOT_MEMORY_TYPE_AVAILABLE 0x00000004
%define VBOOT_MEMORY_TYPE_RECLAIM   0x00000005

; Temporary type used during booting which will get
; marked _AVAILABLE automatically just before jumping to
; the OS
%define VBOOT_MEMORY_TYPE_BOOT 0x80000000

struc VBootMemoryEntry
    .Type:         resd 1
    .PhysicalBase: resq 1
    .VirtualBase:  resq 1
    .Length:       resq 1
    .Attributes:   resq 1
endstruc

struc VBoot
    .Magic:        resd 1
    .Version:      resd 1
    .FirmwareType: resd 1
    .ConfigCount:  resd 1
    .ConfigTable:  resq 1

    .MemoryCount:   resd 1
    .MemoryEntries: resq 1

    .VideoFramebuffer:   resq 1
    .VideoWidth:         resd 1
    .VideoHeight:        resd 1
    .VideoPitch:         resd 1
    .VideoBitsPerPixel:  resd 1
    .VideoRedPosition:   resd 1
    .VideoRedMask:       resd 1
    .VideoGreenPosition: resd 1
    .VideoGreenMask:     resd 1
    .VideoBluePosition:  resd 1
    .VideoBlueMask:      resd 1
    .VideoAlphaPosition: resd 1
    .VideoAlphaMask:     resd 1

    .RamdiskBase:   resq 1
    .RamdiskLength: resd 1

    .KernelBase:   resq 1
    .KernelEntry:  resq 1
    .KernelLength: resd 1

    .StackBase:    resq 1
    .StackLength:  resd 1

    .PhoenixBase:   resq 1
    .PhoenixEntry:  resq 1
    .PhoenixLength: resd 1
endstruc

; ********************************
; BiosCallWrapper
; @brief Invokes a function call in 16 bit mode. It will reuse the
; the current stack, which means the stack must reside in memory that is
; accessible to the 16 bit mode.
;
; @param Function [In] The function address to call
; @param Arg0...N [In] The arguments to pass to the function
; @return <none>
; ********************************
ALIGN 32
BITS  32
BiosCallWrapper:
    ; When we enter here we want to the stack to point at the arguments
    ; and not the return address, but we still need to keep the return address
    ; to actually return to the correct caller. So we pop it and store it seperately
    pop eax                         ; get return address
    mov dword [dCurrentStack], esp  ; save the stack
    mov dword [dReturnAddress], eax ; save the return address

	; Load 16-bit protected mode descriptor
	mov eax, DATA16_DESC
	mov ds, eax
	mov es, eax
	mov fs, eax
	mov gs, eax
	mov ss, eax

	; Jump to protected real mode, set CS!
	jmp	CODE16_DESC:BiosCallWrapper_Part2

; We are now in 16 bit, protected mode.
ALIGN 32
BITS  16
BiosCallWrapper_Part2:
	call LoadIdt16

	; Disable Protected mode
	mov	eax, cr0
	and	eax, 0xFFFFFFFE
	mov	cr0, eax

	; Far jump to real mode unprotected
	jmp 0:BiosCallWrapper_Part3

; We are now entering 16 bit, unprotected mode
BiosCallWrapper_Part3:
	; Setup segments, leave 16 bit protected mode
    xor ax, ax
	mov	ds, ax
	mov	es, ax
	mov	fs, ax
	mov	gs, ax

	; Setup stack
	mov	ss, ax
	mov	ax, word [dCurrentStack]
	mov	sp, ax
	sti

    ; Call the function, function address is a 32 bit
    ; address however we are in 16 bit, so we expect
    ; it to point to something in the low 16 bits
    ; TODO split into segment and offset
    pop eax
    call ax ; CODE16_DESC:eax ? 

    ; Reenable the protected mode
	mov	eax, cr0
	or  eax, 1
	mov	cr0, eax
	jmp CODE_DESC:BiosCallWrapper_Part4

ALIGN 32
BITS  32
BiosCallWrapper_Part4:
    cli
    
    ; Restore segments and stack
	mov ax, DATA_DESC
	mov ds, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	mov es, ax
	mov esp, dword [dCurrentStack]
    push dword [dReturnAddress]
    ret

; Set bits to non-32 again
BITS 16
; ********************************
; SystemsFail
; Only call if fatal error
; ********************************
SystemsFail:
    int 0x16 ; await keypress
    int 0x19 ; warm boot computer
    cli
    hlt

; ****************************
; Variables
; ****************************
dCurrentStack  dd 0
dReturnAddress dd 0

; ********************************
; VBoot Structure
; ********************************
align 32
BootHeader:
istruc VBoot
    at VBoot.Magic,              dd VBOOT_MAGIC
    at VBoot.Version,            dd VBOOT_VERSION
    at VBoot.FirmwareType,       dd VBOOT_FIRMWARE_BIOS
    at VBoot.ConfigCount,        dd 0
    at VBoot.ConfigTable,        dq 0

    at VBoot.MemoryCount,        dd 0
    at VBoot.MemoryEntries,      dq 0

    at VBoot.VideoFramebuffer,   dq 0
    at VBoot.VideoWidth,         dd 0
    at VBoot.VideoHeight,        dd 0
    at VBoot.VideoPitch,         dd 0
    at VBoot.VideoBitsPerPixel,  dd 0
    at VBoot.VideoRedPosition,   dd 0
    at VBoot.VideoRedMask,       dd 0
    at VBoot.VideoGreenPosition, dd 0
    at VBoot.VideoGreenMask,     dd 0
    at VBoot.VideoBluePosition,  dd 0
    at VBoot.VideoBlueMask,      dd 0
    at VBoot.VideoAlphaPosition, dd 0
    at VBoot.VideoAlphaMask,     dd 0

    at VBoot.RamdiskBase,        dq 0
    at VBoot.RamdiskLength,      dd 0

    at VBoot.KernelBase,         dq 0
    at VBoot.KernelEntry,        dq 0
    at VBoot.KernelLength,       dd 0

    at VBoot.StackBase,          dq 0
    at VBoot.StackLength,        dd 0

    at VBoot.PhoenixBase,        dq 0
    at VBoot.PhoenixEntry,       dq 0
    at VBoot.PhoenixLength,      dd 0
iend

%endif
