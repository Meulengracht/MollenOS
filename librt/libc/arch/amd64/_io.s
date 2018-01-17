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
	push rbp
	mov rbp, rsp
	push rdx

	; Get byte
	xor rax, rax
	xor rdx, rdx
	mov dx, word [rbp + 8]
	in al, dx

	; Restore
	pop rdx
	pop rbp
	ret

; uint16_t __readword(uint16_t port)
; Recieves a word from a port
___readword:
	; Setup frame
	push rbp
	mov rbp, rsp
	push rdx

	; Get word
	xor rax, rax
	xor rdx, rdx
	mov dx, word [rbp + 8]
	in ax, dx

	; Restore
	pop rdx
	pop rbp
	ret

; uint32_t __readlong(uint16_t port)
; Recieves a long from a port
___readlong:
	; Setup frame
	push rbp
	mov rbp, rsp
	push rdx

	; Get dword
	xor rax, rax
	xor rdx, rdx
	mov dx, word [rbp + 8]
	in eax, dx

	; Restore
	pop rdx
	pop rbp
	ret

; void __writebyte(uint16_t port, uint8_t data)
; Sends a byte to a port
___writebyte:
	; Setup frame
	push ebp
	mov ebp, esp
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
	pop ebp
	ret

; void __writeword(uint16_t port, uint16_t data)
; Sends a word to a port
___writeword:
	; Stack Frame
	push rbp
	mov rbp, rsp
	push rax
	push rdx

	; Get data
	xor rax, rax
	xor rdx, rdx
	mov dx, [rbp + 8]
	mov ax, [rbp + 16]
	out dx, ax

	; Restore
	pop rdx
	pop rax
	pop rbp
	ret

; void __writelong(uint16_t port, uint32_t data)
; Sends a long to a port
___writelong:
	; Stack Frame
	push rbp
	mov rbp, rsp
	push rax
	push rdx

	; Get data
	xor rdx, rdx
	mov dx, word [rbp + 8]
	mov eax, dword [rbp + 16]
	out dx, eax

	; Restore
	pop rdx
	pop rax
	pop rbp
	ret
