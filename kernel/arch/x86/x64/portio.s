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
; MollenOS x86 Direct Port IO Code
;
bits 64
segment .text

;Functions in this asm
global inb
global inw
global inl

global outb
global outw
global outl

; uint8_t inb(uint16_t port)
; Recieves a byte from a port
inb:
	xor rax, rax
	xor rdx, rdx
	mov dx, cx
	in al, dx
	ret

; uint16_t inw(uint16_t port)
; Recieves a word from a port
inw:
	xor rax, rax
	xor rdx, rdx
	mov dx, cx
	in ax, dx
	ret

; uint32_t inl(uint16_t port)
; Recieves a long from a port
inl:
	xor rax, rax
	xor rdx, rdx
	mov dx, cx
	in eax, dx
	ret

; void outb(uint16_t port, uint8_t data)
; Sends a byte to a port
outb:
	; Get data
    push rbx
    mov rbx, rdx
	xor rdx, rdx
	xor rax, rax
	mov dx, cx
	mov al, bl
	out dx, al
    pop rbx
	ret

; void outw(uint16_t port, uint16_t data)
; Sends a word to a port
outw:
	; Get data
    push rbx
    mov rbx, rdx
	xor rdx, rdx
	xor rax, rax
	mov dx, cx
	mov ax, bx
	out dx, ax
    pop rbx
	ret

; void outl(uint16_t port, uint32_t data)
; Sends a long to a port
outl:
	; Get data
    push rbx
    mov rbx, rdx
	xor rdx, rdx
	xor rax, rax
	mov dx, cx
	mov eax, ebx
	out dx, eax
    pop rbx
	ret