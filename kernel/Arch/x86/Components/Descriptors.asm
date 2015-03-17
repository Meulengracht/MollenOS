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
global _GdtInstall
global _TssInstall
global _IdtInstall

extern _Gdtptr
extern _Idtptr

; void GdtInstall()
; Load the given gdt into gdtr
_GdtInstall:
	; Save EAX
	push eax

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
	; Restore EAX
	pop eax
	ret 

; void TssInstall(uint32_t gdt_index)
; Load the given TSS descriptor
; index
_TssInstall:
	; Create a stack frame
	push ebp
	mov ebp, esp

	; Save registers
	push eax
	push ecx

	; Calculate index
	mov eax, [ebp + 8]
	mov ecx, 0x8
	mul ecx
	or eax, 0x3

	; Load task register
	ltr ax

	; Restore
	pop ecx
	pop eax

	; Leave frame
	pop ebp
	ret

; void IdtInstall()
; Load the given TSS descriptor
; index
_IdtInstall:
	; Install IDT
	lidt [_idt_ptr]

	; Return
	ret 