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
; MollenOS x86-32 Math FMODF

bits 32
segment .text

;Functions in this asm
global _fmodf

; float fmodf(float x,float y)
_fmodf:
	; Stack Frame
	push    ebp
	mov     ebp, esp

    fld dword [esp + 4]
    fld dword [esp + 8]
    fxch st1
l1:
    fprem
    fstsw ax
    sahf
    jp l1
    fstp st1

	; Unwind & return
	pop     ebp
	ret
