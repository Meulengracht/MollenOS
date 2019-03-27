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
global __readbyte
global __readword
global __readlong

global __writebyte
global __writeword
global __writelong

; uint8_t __readbyte(uint16_t port <rcx>)
; Recieves a byte from a port
__readbyte:
	xor rax, rax
	xor rdx, rdx
	mov dx, cx
	in al, dx
	ret

; uint16_t __readword(uint16_t port <rcx>)
; Recieves a word from a port
__readword:
	xor rax, rax
	xor rdx, rdx
	mov dx, cx
	in ax, dx
	ret

; uint32_t __readlong(uint16_t port <rcx>)
; Recieves a long from a port
__readlong:
	xor rax, rax
	xor rdx, rdx
	mov dx, cx
	in eax, dx
	ret

; void __writebyte(uint16_t port <rcx> , uint8_t data <rdx>)
; Sends a byte to a port
__writebyte:
    xchg rcx, rdx
    mov al, cl
    out dx, al
	ret

; void __writeword(uint16_t port <rcx>, uint16_t data <rdx>)
; Sends a word to a port
__writeword:
    xchg rcx, rdx
    mov ax, cx
    out dx, ax
	ret

; void __writelong(uint16_t port <rcx>, uint32_t data <rdx>)
; Sends a long to a port
__writelong:
    xchg rcx, rdx
    mov eax, ecx
    out dx, eax
	ret
