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
; MollenOS x86-64 Descriptor Assembly Functions
;
bits 64
segment .text

;Functions in this asm
global GdtInstall
global TssInstall
global IdtInstall

extern __GdtTableObject
extern __IdtTableObject

; void GdtInstall()
; Load the given gdt into gdtr
GdtInstall:
	; Install GDT
	lgdt [__GdtTableObject]

	; Jump into correct descriptor
	xor rax, rax
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

    sub rsp, 16
    mov qword [rsp + 8], 0x08
    mov rax, done
    mov qword [rsp], rax
    iretq
    done:
	    ret 

; void TssInstall(int gdt_index)
; Load the given TSS descriptor
; index
TssInstall:
	; Calculate index
	mov rax, rcx
	shl rax, 3
	or rax, 0x3

	; Load task register
	ltr ax
	ret

; void IdtInstall()
; Load the given TSS descriptor
; index
IdtInstall:
	lidt [__IdtTableObject]
	ret