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
; MollenOS x86-32 Spinlock Code
;
bits 32
segment .text

;Functions in this asm
global _inb
global _inw
global _inl

global _outb
global _outw
global _outl

; void inb(uint16_t port)
; Recieves a byte from a port
_inb:
	; Stack Frame
	push ebp
	mov ebp, esp
	push edx

	; Get byte
	xor eax, eax
	xor edx, edx
	mov dx, [ebp + 8]
	in al, dx

	; Release stack frame
	pop edx
	pop ebp
	ret

; void inw(uint16_t port)
; Recieves a word from a port
_inw:
	; Stack Frame
	push ebp
	mov ebp, esp
	push edx

	; Get byte
	xor eax, eax
	xor edx, edx
	mov dx, [ebp + 8]
	in ax, dx

	; Release stack frame
	pop edx
	pop ebp
	ret

; void inl(uint16_t port)
; Recieves a long from a port
_inl:
	; Stack Frame
	push ebp
	mov ebp, esp
	push edx

	; Get byte
	xor eax, eax
	xor edx, edx
	mov dx, [ebp + 8]
	in eax, dx

	; Release stack frame
	pop edx
	pop ebp
	ret

; void outb(uint16_t port, uint8_t data)
; Sends a byte to a port
_outb:
	; Stack Frame
	push ebp
	mov ebp, esp
	push edx

	; Get data
	xor eax, eax
	xor edx, edx
	mov dx, [ebp + 8]
	mov al, [ebp + 12]
	out dx, al

	; Release stack frame
	xor eax, eax
	pop edx
	pop ebp
	ret

; void outw(uint16_t port, uint16_t data)
; Sends a word to a port
_outw:
	; Stack Frame
	push ebp
	mov ebp, esp
	push edx

	; Get data
	xor eax, eax
	xor edx, edx
	mov dx, [ebp + 8]
	mov ax, [ebp + 12]
	out dx, ax

	; Release stack frame
	xor eax, eax
	pop edx
	pop ebp
	ret

; void outl(uint16_t port, uint32_t data)
; Sends a long to a port
_outl:
	; Stack Frame
	push ebp
	mov ebp, esp
	push edx

	; Get data
	xor edx, edx
	mov dx, [ebp + 8]
	mov eax, [ebp + 12]
	out dx, eax

	; Release stack frame
	xor eax, eax
	pop edx
	pop ebp
	ret