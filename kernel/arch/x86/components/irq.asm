; MollenOS
;
; Copyright 2011-2014, Philip Meulengracht
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation?, either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <http://www.gnu.org/licenses/>.
;
;
; MollenOS x86-32 Descriptor Assembly Functions
;
bits 32
segment .text

;Functions in this asm
global _exception_common
global _irq_common
global ___cli
global ___sti
global ___getflags

;Externs, common entry points
extern _exception_entry
extern _interrupt_entry

; void __cli(void)
; Disables interrupts
___cli:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Disable interrupts
	cli

	; Release stack frame
	xor eax, eax
	pop ebp
	ret 

; void __sti(void)
; Enables interrupts
___sti:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Enable interrupts
	sti

	; Release stack frame
	xor eax, eax
	pop ebp
	ret 

; uint32_t __getflags(void)
; Gets Eflags
___getflags:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Get flags
	pushfd
	pop eax

	; Release stack frame
	pop ebp
	ret 

;Common entry point for exceptions
_exception_common:
	
	; Save Segments
	xchg bx, bx
	push ds
	push es
	push fs
	push gs

	; Save Registers
	pushad

	; Switch to kernel segment
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax

	; Push registers as pointer to struct
	push esp

	; Call C-code exception handler
	call _exception_entry

	; Cleanup
	add esp, 0x4

	; _IF_ we return, restore state
	popad

	; Restore segments
	pop gs
	pop fs
	pop es
	pop ds

	; Cleanup IrqNum & IrqErrorCode from stack
	add esp, 0x8

	; Return
	iret

;Common entry point for interrupts
_irq_common:
	
	; Save Segments
	push ds
	push es
	push fs
	push gs

	; Save Registers
	pushad

	; Switch to kernel segment
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax

	; Push registers as pointer to struct
	push esp

	; Call C-code exception handler
	call _interrupt_entry

	; Cleanup
	add esp, 0x4

	; When we return, restore state
	popad

	; Restore segments
	pop gs
	pop fs
	pop es
	pop ds

	; Cleanup IrqNum & IrqErrorCode from stack
	add esp, 0x8

	; Return
	iret

; Macros

; Exception with no error code
%macro irq_no_error 1
	global _irq_handler%1
	_irq_handler%1:
		push 0
		push %1
		jmp _exception_common
%endmacro

%macro irq_error 1
	global _irq_handler%1
	_irq_handler%1:
		push %1
		jmp _exception_common
%endmacro

%macro irq_normal 2
	global _irq_handler%1
	_irq_handler%1:
		push 0
		push %2
		jmp _irq_common
%endmacro

;Define excetions!
irq_no_error 0
irq_no_error 1
irq_no_error 2
irq_no_error 3
irq_no_error 4
irq_no_error 5
irq_no_error 6
irq_no_error 7
irq_error    8
irq_no_error 9
irq_error    10
irq_error    11
irq_error    12
irq_error    13
irq_error    14
irq_no_error 15
irq_no_error 16
irq_error    17
irq_no_error 18
irq_no_error 19
irq_no_error 20
irq_no_error 21
irq_no_error 22
irq_no_error 23
irq_no_error 24
irq_no_error 25
irq_no_error 26
irq_no_error 27
irq_no_error 28
irq_no_error 29
irq_no_error 30
irq_no_error 31

;Base IRQs 0 - 15
irq_normal 32, 0
irq_normal 33, 1
irq_normal 34, 2
irq_normal 35, 3
irq_normal 36, 4
irq_normal 37, 5
irq_normal 38, 6
irq_normal 39, 7
irq_normal 40, 8
irq_normal 41, 9
irq_normal 42, 10
irq_normal 43, 11
irq_normal 44, 12
irq_normal 45, 13
irq_normal 46, 14
irq_normal 47, 15