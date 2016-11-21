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
; MollenOS x86-32 Math LOG 

bits 32
segment .text

;Functions in this asm
global _log
global _logf
global _logl
global __CIlog

; takes the logorithmic value in base 2
_log:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Load log base e of 2
	fldln2

	; Load real from stack
	fld     qword [ebp + 8]

	; Compute the natural log(x)
	fyl2x

	; Unwind & return
	pop     ebp
	ret

; takes the logorithmic value in base 2 float
_logf:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Load log base e of 2
	fldln2

	; Load real from stack
	fld     dword [ebp + 8]

	; Compute the natural log(x)
	fyl2x

	; Unwind & return
	pop     ebp
	ret

; takes the logorithmic value in base 2 float
_logl:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Load log base e of 2
	fldln2

	; Load real from stack
	fld     tword [ebp + 8]

	; Compute the natural log(x)
	fyl2x

	; Unwind & return
	pop     ebp
	ret

; Msvc version of log
__CIlog:
	fldln2
	fxch
	fyl2x

	; Done
	ret