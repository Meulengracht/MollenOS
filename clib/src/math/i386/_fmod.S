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
; MollenOS x86-32 Math floating point remainder of x/y

bits 32
segment .text

;Functions in this asm
global _fmod
global __CIfmod

; double __cdecl fmod(double x, double y)
; floating point remainder of x/y
_fmod:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Load x and y from stack
    fld     qword [ebp + 16]
    fld     qword [ebp + 8]

__fmod1:
	; Get the partial remainder
	fprem

	; Get coprocessor status
    fstsw   ax

	; Complete remainder ?
    sahf

	; No, go get next remainder
    jp		__fmod1

	; Set new top of stack
    fstp    st1

	; Unwind & return
	pop     ebp
	ret

; Msvc version of fmod
__CIfmod:
	; Save eax
	push	eax

	; Swap arguments
	fxch    st1

__CIfmod1:
	; Get the partial remainder
	fprem

	; Get coprocessor status
    fstsw   ax

	; Complete remainder ?
    sahf

	; No, go get next remainder
    jp		__CIfmod1

	; Set new top of stack
    fstp    st1

	; Unwind & return
	pop     eax
	ret