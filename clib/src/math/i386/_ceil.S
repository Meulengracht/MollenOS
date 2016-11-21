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
global _ceilf
global _ceill

; Ceils a value
_ceil:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Allocate temporary space
	sub     esp, 8

	; Store control word 
	fstcw	word [ebp - 4]
	mov		dx, word [ebp - 4]

	; Round towards +oo
	or		dx, 0x0800
	and		dx, 0xFBFF
	mov		word [ebp - 8], dx

	; Load modfied control word
	fldcw	word [ebp - 8]

	; Load real from stack
	fld     qword [ebp + 8]

	; Round
	frndint

	; Restore control word
	fldcw   [ebp - 4]

	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret

; Ceils a value float
_ceilf:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Allocate temporary space
	sub     esp, 8

	; Store control word 
	fstcw	word [ebp - 4]
	mov		dx, word [ebp - 4]

	; Round towards +oo
	or		dx, 0x0800
	and		dx, 0xFBFF
	mov		word [ebp - 8], dx

	; Load modfied control word
	fldcw	word [ebp - 8]

	; Load real from stack
	fld     dword [ebp + 8]

	; Round
	frndint

	; Restore control word
	fldcw   [ebp - 4]

	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret

; Ceils a value long double
_ceill:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Allocate temporary space
	sub     esp, 8

	; Store control word 
	fstcw	word [ebp - 4]
	mov		dx, word [ebp - 4]

	; Round towards +oo
	or		dx, 0x0800
	and		dx, 0xFBFF
	mov		word [ebp - 8], dx

	; Load modfied control word
	fldcw	word [ebp - 8]

	; Load real from stack
	fld     tword [ebp + 8]

	; Round
	frndint

	; Restore control word
	fldcw   [ebp - 4]

	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret