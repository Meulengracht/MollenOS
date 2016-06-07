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
; MollenOS x86-32 Math floating point arc cosine

bits 32
segment .text

;Functions in this asm
global  _acos
global  __CIacos

; double __cdecl acos(double x)
; floating point arc cosine
_acos:
    ; Stack Frame
	push    ebp
	mov     ebp, esp

	; Load real from stack
    fld     qword [ebp + 8]

	; Load x twice
    fld     st0
    fld     st0

	; Multiply (x squared)
    fmul

	; Load 1
    fld1

	; 1 - (x squared)
    fsubr

	; Square root of (1 - x squared)
    fsqrt

	; Exchange st, st(1)
    fxch

	; This gives the arc cosine !
    fpatan
	
	; Unwind & return
	pop     ebp
	ret

__CIacos:
	; Load x twice
    fld     st0
    fld     st0
    
	; Multiply (x squared)
	fmul

	; Load 1
    fld1

	; 1 - (x squared)
    fsubr

	; Square root of (1 - x squared)
    fsqrt

	; Exchange st, st(1)
    fxch

	; This gives the arc cosine !
    fpatan

	; Done!
    ret