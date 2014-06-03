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
global _gdt_install
global _tss_install
global _idt_install

extern _gdt_ptr
extern _idt_ptr

; void _gdt_install()
; Load the given gdt into gdtr
_gdt_install:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Install GDT
	lgdt [_gdt_ptr]

	; Jump into correct descriptor
	xor eax, eax
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

	jmp 0x08:done

	done:
	; Release stack frame
	xor eax, eax
	pop ebp
	ret 

; void _tss_install(uint32_t gdt_index)
; Load the given TSS descriptor
; index
_tss_install:
	; Stack Frame
	push ebp
	mov ebp, esp
	push ecx

	; Calculate index
	mov eax, dword [ebp + 8]
	mov ecx, 0x8
	mul ecx
	or eax, 0x3

	; Load task register
	ltr ax

	; Release stack frame
	pop ecx
	xor eax, eax
	pop ebp
	ret

; void _tss_install()
; Load the given TSS descriptor
; index
_idt_install:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Install IDT
	lidt [_idt_ptr]

	; Release stack frame
	xor eax, eax
	pop ebp
	ret 