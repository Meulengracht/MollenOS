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
; MollenOS x86-64 Direct Port IO Code
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

; uint8_t __readbyte(uint16_t port <rcx>)
; Recieves a byte from a port
___readbyte:
	xor rax, rax
	xor rdx, rdx
	mov dx, cx
	in al, dx
	ret

; uint16_t __readword(uint16_t port <rcx>)
; Recieves a word from a port
___readword:
	xor rax, rax
	xor rdx, rdx
	mov dx, cx
	in ax, dx
	ret

; uint32_t __readlong(uint16_t port <rcx>)
; Recieves a long from a port
___readlong:
	xor rax, rax
	xor rdx, rdx
	mov dx, cx
	in eax, dx
	ret

; void __writebyte(uint16_t port <rcx> , uint8_t data <rdx>)
; Sends a byte to a port
___writebyte:
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

; void __writeword(uint16_t port <rcx>, uint16_t data <rdx>)
; Sends a word to a port
___writeword:
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

; void __writelong(uint16_t port <rdx>, uint32_t data <rdx>)
; Sends a long to a port
___writelong:
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
