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
bits 32
segment .text

;Functions in this asm
global ___readbyte
global ___readword
global ___readlong

global ___writebyte
global ___writeword
global ___writelong

; uint8_t __readbyte(uint16_t port)
; Recieves a byte from a port
___readbyte:
	xor eax, eax
	xor edx, edx
	mov dx, word [esp + 4]
	in al, dx
	ret

; uint16_t __readword(uint16_t port)
; Recieves a word from a port
___readword:
	xor eax, eax
	xor edx, edx
	mov dx, word [esp + 4]
	in ax, dx
	ret

; uint32_t __readlong(uint16_t port)
; Recieves a long from a port
___readlong:
	xor eax, eax
	xor edx, edx
	mov dx, word [esp + 4]
	in eax, dx
	ret

; void __writebyte(uint16_t port, uint8_t data)
; Sends a byte to a port
___writebyte:
	xor eax, eax
	xor edx, edx
	mov dx, word [esp + 4]
	mov al, byte [esp + 8]
	out dx, al
	ret

; void __writeword(uint16_t port, uint16_t data)
; Sends a word to a port
___writeword:
	xor eax, eax
	xor edx, edx
	mov dx, word [esp + 4]
	mov ax, word [esp + 8]
	out dx, ax
	ret

; void __writelong(uint16_t port, uint32_t data)
; Sends a long to a port
___writelong:
	xor eax, eax
	xor edx, edx
	mov dx, word [esp + 4]
	mov eax, dword [esp + 8]
	out dx, eax
	ret
