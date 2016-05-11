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
; MollenOS x86-32 Math CEILING

bits 32
segment .text

;Functions in this asm
global _ceil

; Ceils a value
_ceil:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Allocate temporary space
	sub     esp, 4

	; Load real from stack
	fld     qword [ebp + 8]

	; Save control word & clear errx
	fstcw	[ebp - 2]
	fclex

	; Rounding control word
	mov		word [ebp - 4], 0x0B63
	fldcw	[ebp - 4]
	
	; Round to integer & clear ex again
	frndint
	fclex

	; Restore control word + free space
	fldcw   [ebp - 2]
	mov     esp, ebp
	
	; Unwind & return
	pop     ebp
	ret
