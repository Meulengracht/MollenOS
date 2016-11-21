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
; MollenOS x86-32 Math floating point arc sine

bits 32
segment .text

;Functions in this asm
global  _asin
global  __CIasin

; double __cdecl asin(double x)
; floating point arc sine
_asin:
    push    ebp
    mov     ebp,esp
    fld     qword [ebp+8]           ; Load real from stack
    fld     st0                     ; Load x
    fld     st0                     ; Load x
    fmul                            ; Multiply (x squared)
    fld1                            ; Load 1
    fsubr                           ; 1 - (x squared)
    fsqrt                           ; Square root of (1 - x squared)
    fpatan                          ; This gives the arc sine !
    pop     ebp
    ret

; Msvc version of asin
__CIasin:
	fld     st0                     ; Load x
    fld     st0                     ; Load x
    fmul                            ; Multiply (x squared)
    fld1                            ; Load 1
    fsubr                           ; 1 - (x squared)
    fsqrt                           ; Square root of (1 - x squared)
    fpatan                          ; This gives the arc sine !
    ret