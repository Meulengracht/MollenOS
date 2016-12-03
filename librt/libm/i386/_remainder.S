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
global _remainder
global _remainderf
global _remainderl

; takes the remainder
_remainder:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Load x and y from stack
    fld     qword [ebp + 16]
    fld     qword [ebp + 8]

	.TryAgain:
		; Get fp remainder
		fprem1

		; Get flags
		fstsw	ax

		; Is there more?
		sahf

		; Load real from stack
		jp		.TryAgain

		; Store result on stack
		fstp	st1

	; Unwind & return
	pop     ebp
	ret

; takes the remainder float
_remainderf:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Load x and y from stack
    fld     dword [ebp + 12]
    fld     dword [ebp + 8]

	.TryAgain:
		; Get fp remainder
		fprem1

		; Get flags
		fstsw	ax

		; Is there more?
		sahf

		; Load real from stack
		jp		.TryAgain

		; Store result on stack
		fstp	st1

	; Unwind & return
	pop     ebp
	ret

; takes the remainder long double
_remainderl:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Load x and y from stack
    fld     tword [ebp + 20]
    fld     tword [ebp + 8]

	.TryAgain:
		; Get fp remainder
		fprem1

		; Get flags
		fstsw	ax

		; Is there more?
		sahf

		; Load real from stack
		jp		.TryAgain

		; Store result on stack
		fstp	st1

	; Unwind & return
	pop     ebp
	ret