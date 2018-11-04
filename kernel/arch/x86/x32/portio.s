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
global _inb
global _inw
global _inl

global _outb
global _outw
global _outl

; uint8_t inb(uint16_t port)
; Recieves a byte from a port
_inb:
    xor eax, eax
    xor edx, edx
    mov dx, word [esp + 4]
    in al, dx
    ret

; uint16_t inw(uint16_t port)
; Recieves a word from a port
_inw:
    xor eax, eax
    xor edx, edx
    mov dx, word [esp + 4]
    in ax, dx
    ret

; uint32_t inl(uint16_t port)
; Recieves a long from a port
_inl:
    xor eax, eax
    xor edx, edx
    mov dx, word [esp + 4]
    in eax, dx
    ret

; void outb(uint16_t port, uint8_t data)
; Sends a byte to a port
_outb:
    xor eax, eax
    xor edx, edx
    mov dx, word [esp + 4]
    mov al, byte [esp + 8]
    out dx, al
    ret

; void outw(uint16_t port, uint16_t data)
; Sends a word to a port
_outw:
    xor eax, eax
    xor edx, edx
    mov dx, word [esp + 4]
    mov ax, word [esp + 8]
    out dx, ax
    ret

; void outl(uint16_t port, uint32_t data)
; Sends a long to a port
_outl:
    xor eax, eax
    xor edx, edx
    mov dx, word [esp + 4]
    mov eax, dword [esp + 8]
    out dx, eax
    ret