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
; MollenOS x86-32 Math LOG 10

bits 32
segment .text

;Functions in this asm
global _log10f

; takes the logorithmic value in base 10
_log10f:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Do the magic
	fld     qword [ebp+8]       ; Load real from stack
	fldlg2                          ; Load log base 10 of 2
	
	; Exchange st, st(1)
	fxch

	; Compute the log base 10(x)
	fyl2x
	
	; Unwind & return
	pop     ebp
	ret