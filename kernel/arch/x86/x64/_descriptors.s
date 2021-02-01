; MollenOS
;
; Copyright 2014, Philip Meulengracht
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
; x86-64 Descriptor Assembly Functions
;
bits 64
segment .text

;Functions in this asm
global GdtInstall
global TssInstall
global IdtInstall

extern g_gdtTable
extern g_idtTable

; void GdtInstall()
; Load the given gdt into gdtr
GdtInstall:
	; Install GDT
	lgdt [g_gdtTable]

	; Setup null segments
	xor rax, rax
	mov ds, ax
	mov es, ax
	mov fs, ax

	; Setup gs, we set a limit on 1 page
	mov ax, 0x50
	mov gs, ax

	; Setup stack segment
	mov ax, 0x20
	mov ss, ax

    ; Invoke a faux interrupt return to switch CS
    mov rax, rsp
    sub rsp, 16
    mov qword [rsp + 8], 0x20
    mov qword [rsp], rax
    pushfq
    sub rsp, 16
    mov qword [rsp + 8], 0x10
    mov rax, done
    mov qword [rsp], rax
    iretq
    done:
	    ret 

; void TssInstall(int gdt_index)
; Load the given TSS descriptor
; index
TssInstall:
	; Calculate index (gdt_index * 16)
	mov rax, rcx
	shl rax, 4
	or rax, 0x3
	ltr ax
	ret

; void IdtInstall()
; Load the given TSS descriptor
; index
IdtInstall:
	lidt [g_idtTable]
	ret