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
; MollenOS x86-32 math remquo

bits 32
segment .text

;Functions in this asm
global _remquo
global _remquof
global _remquol

; takes the remainder quotient
_remquo:
	push    ebp
	mov     ebp, esp

	; Load x and y from stack
    fld     qword [ebp + 16]
    fld     qword [ebp + 8]

    ; Extract the remainder
	.TryAgain:
		fprem1
		fstsw	ax
		sahf
		jp		.TryAgain
		fstp	st1

	; Extract the three low-order bits of the quotient from C0, C3, C1
	shr 	eax, 6
	mov 	ecx, eax
	and 	eax, 0x108
	ror 	eax, 7
	or 		ecx, eax
	rol 	eax, 4
	or 		eax, ecx
	and 	eax, 7

	; Negate the quotient bits if x * y < 0. Avoid using an unpreditable branch
	mov 	ecx, dword [ebp + 20]
	xor 	ecx, dword [ebp + 12]
	sar 	ecx, 16
	sar 	ecx, 16
	xor 	eax, ecx
	and 	ecx, 1
	add 	eax, ecx

	; Store result
	mov 	ecx, dword [ebp + 24]
	mov 	dword [ecx], eax
	pop     ebp
	ret

; takes the remainder quotient
_remquof:
	push    ebp
	mov     ebp, esp

	; Load x and y from stack
    fld     dword [ebp + 12]
    fld     dword [ebp + 8]

    ; Extract the remainder
	.TryAgain:
		fprem1
		fstsw	ax
		sahf
		jp		.TryAgain
		fstp	st1

	; Extract the three low-order bits of the quotient from C0, C3, C1
	shr 	eax, 6
	mov 	ecx, eax
	and 	eax, 0x108
	ror 	eax, 7
	or 		ecx, eax
	rol 	eax, 4
	or 		eax, ecx
	and 	eax, 7

	; Negate the quotient bits if x * y < 0. Avoid using an unpreditable branch
	mov 	ecx, dword [ebp + 12]
	xor 	ecx, dword [ebp + 8]
	sar 	ecx, 16
	sar 	ecx, 16
	xor 	eax, ecx
	and 	ecx, 1
	add 	eax, ecx

	; Store result
	mov 	ecx, dword [ebp + 16]
	mov 	dword [ecx], eax
	pop     ebp
	ret

; takes the remainder quotient
_remquol:
	push    ebp
	mov     ebp, esp

	; Load x and y from stack
    fld     tword [ebp + 20]
    fld     tword [ebp + 8]

    ; Extract the remainder
	.TryAgain:
		fprem1
		fstsw	ax
		sahf
		jp		.TryAgain
		fstp	st1

	; Extract the three low-order bits of the quotient from C0, C3, C1
	shr 	eax, 6
	mov 	ecx, eax
	and 	eax, 0x108
	ror 	eax, 7
	or 		ecx, eax
	rol 	eax, 4
	or 		eax, ecx
	and 	eax, 7

	; Negate the quotient bits if x * y < 0. Avoid using an unpreditable branch
	mov 	ecx, dword [ebp + 28]
	xor 	ecx, dword [ebp + 16]
	sar 	ecx, 16
	sar 	ecx, 16
	xor 	eax, ecx
	and 	ecx, 1
	add 	eax, ecx

	; Store result
	mov 	ecx, dword [ebp + 32]
	mov 	dword [ecx], eax
	pop     ebp
	ret