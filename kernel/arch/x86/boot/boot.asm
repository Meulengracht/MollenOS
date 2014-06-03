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
; MollenOS x86-32 Boot Code
;
bits 32
segment .text

; Extern main function in C-code
extern _init

; Publics in this file
global _kentry

; No matter what, this is booted by multiboot, and thus
; We can assume the state when this point is reached.
; EAX - Multiboot Magic
; EBX - Contains address of the multiboot structure, but
;		it should be located in stack aswell.
; EDX - Should contain size of the kernel file

_kentry:
	;We disable interrupts, we have no IDT installed
	cli

	;If any important information has been passed to 
	;us through the stack, we need to save it now.

	;Setup a new stack to an unused
	;place in memory. This will be temporary.
	mov eax, 0x10
	mov ss, ax					
	mov esp, 0x90000
	mov ebp, esp

	;Now, we place multiboot structure and kernel
	;size information on the stack.
	push edx
	push ebx

	;Now call the init function
	call _init

	;When we return from here, we just
	;enter into an idle loop.
	mov eax, 0x0000DEAD
	
	.idle:
		hlt
		jmp .idle