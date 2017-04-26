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
; MollenOS x86-32 Math Round To Int

bits 32
segment .text

;Functions in this asm
global _lrint
global _lrintf
global _lrintl

; Rounds a double to long
_lrint:
	; Stack Frame
	push	ebp
	mov		ebp, esp
	sub     esp, 4

	; Load real from stack
	fld		qword [ebp + 8]
	fistp	dword [ebp - 4]
	pop		eax

	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret

; Rounds a float to long
_lrintf:
	; Stack Frame
	push	ebp
	mov		ebp, esp
	sub     esp, 4

	; Load real from stack
	fld		dword [ebp + 8]
	fistp	dword [ebp - 4]
	pop		eax

	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret

; Rounds a long double to long
_lrintl:
	; Stack Frame
	push	ebp
	mov		ebp, esp
	sub     esp, 4

	; Load real from stack
	fld		tword [ebp + 8]
	fistp	dword [ebp - 4]
	pop		eax

	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret