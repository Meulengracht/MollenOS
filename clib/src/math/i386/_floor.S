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
; MollenOS x86-32 Math FLOOR 

bits 32
segment .text

;Functions in this asm
global _floor

; Floors a value
_floor:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Do the magic
	sub     esp,4                   ; Allocate temporary space
	fld     qword [ebp+8]			; Load real from stack
	fstcw   [ebp-2]                 ; Save control word
	fclex                           ; Clear exceptions
	mov     word [ebp-4], 0763h		; Rounding control word
	fldcw   [ebp-4]                 ; Set new rounding control
	frndint                         ; Round to integer
	fclex                           ; Clear exceptions
	fldcw   [ebp-2]                 ; Restore control word
				
	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret