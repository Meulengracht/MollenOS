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
global _log10
global _log10f
global _log10l
global __CIlog10

; takes the logorithmic value in base 10
_log10:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Do the magic
	fldlg2                      ; Load log base 10 of 2
	fld     qword [ebp + 8]       ; Load real from stack
	
	; Compute the log base 10(x)
	fyl2x
	
	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret

; takes the logorithmic value in base 10 float
_log10f:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Do the magic
	fldlg2                      ; Load log base 10 of 2
	fld     dword [ebp + 8]       ; Load real from stack
	
	; Compute the log base 10(x)
	fyl2x
	
	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret

; takes the logorithmic value in base 10 long double
_log10l:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Load log base 10 of 2
	fldlg2
	
	; Load real from stack     
	fld     tword [ebp + 8]
	
	; Compute the log base 10(x)
	fyl2x
	
	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret

; Msvc version of log10
__CIlog10:
	fldlg2
	fxch
	fyl2x
	ret