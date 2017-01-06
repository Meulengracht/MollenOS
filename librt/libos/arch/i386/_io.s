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
	; Setup frame
	push ebp
	mov ebp, esp

	; Save stuff
	push edx

	; Get byte
	xor al, al
	xor edx, edx
	mov dx, [ebp + 8]
	in al, dx

	; Restore
	pop edx

	; Leave frame
	pop ebp
	ret

; uint16_t __readword(uint16_t port)
; Recieves a word from a port
___readword:
	; Setup frame
	push ebp
	mov ebp, esp

	; Save stuff
	push edx

	; Get word
	xor ax, ax
	xor edx, edx
	mov dx, [ebp + 8]
	in ax, dx

	; Restore
	pop edx

	; Leave
	pop ebp
	ret

; uint32_t __readlong(uint16_t port)
; Recieves a long from a port
___readlong:
	; Setup frame
	push ebp
	mov ebp, esp

	; Save stuff
	push edx

	; Get dword
	xor eax, eax
	xor edx, edx
	mov dx, [ebp + 8]
	in eax, dx

	; Restore
	pop edx

	; Leave
	pop ebp
	ret

; void __writebyte(uint16_t port, uint8_t data)
; Sends a byte to a port
___writebyte:
	; Setup frame
	push ebp
	mov ebp, esp

	; Save stuff
	push eax
	push edx

	; Get data
	xor eax, eax
	xor edx, edx
	mov dx, [ebp + 8]
	mov al, [ebp + 12]
	out dx, al

	; Restore
	pop edx
	pop eax

	; Leave
	pop ebp
	ret

; void __writeword(uint16_t port, uint16_t data)
; Sends a word to a port
___writeword:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Save stuff
	push eax
	push edx

	; Get data
	xor eax, eax
	xor edx, edx
	mov dx, [ebp + 8]
	mov ax, [ebp + 12]
	out dx, ax

	; Restore
	pop edx
	pop eax

	; Release stack frame
	pop ebp
	ret

; void __writelong(uint16_t port, uint32_t data)
; Sends a long to a port
___writelong:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Save stuff
	push eax
	push edx

	; Get data
	xor edx, edx
	mov dx, [ebp + 8]
	mov eax, [ebp + 12]
	out dx, eax

	; Restore
	pop edx
	pop eax

	; Release stack frame
	pop ebp
	ret
