; MollenOS
; Copyright 2011-2016, Philip Meulengracht
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
; MollenOS x86-32 Math floating point to integer conversion

bits 32
segment .text

;Functions in this asm
global __ftol

; This routine is called by MSVC-generated code to convert from floating point
; to integer representation
__ftol:
	fnstcw  word [esp-2]
    mov     ax, word [esp-2]
    or      ax, 0C00h
    mov     word [esp-4], ax
    fldcw   word [esp-4]
    fistp   qword [esp-12]
    fldcw   word [esp-2]
    mov     eax, dword [esp-12]
    mov     edx, dword [esp-8]
    ret
