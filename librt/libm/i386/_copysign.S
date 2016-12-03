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
; MollenOS x86-32 Math CopySign

bits 32
segment .text

;Functions in this asm
global _copysign
global _copysignf
global _copysignl

; Copysign, has two params
_copysign:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Do some tests
	; High part of x
	mov		edx, dword [ebp + 20]
	and		edx, 0x80000000
	mov		eax, dword [ebp + 12]
	and		eax, 0x7FFFFFFF
	or		eax, edx
	mov		dword [ebp + 12], eax

	; Load real from stack
	fld		qword [ebp + 8]

	; Unwind & return
	pop     ebp
	ret

; Copysign float, has two params
_copysignf:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Do some tests
	; High part of x
	mov		edx, dword [ebp + 12]
	and		edx, 0x80000000
	mov		eax, dword [ebp + 8]
	and		eax, 0x7FFFFFFF
	or		eax, edx
	mov		dword [ebp + 8], eax

	; Load real from stack
	fld		dword [ebp + 8]

	; Unwind & return
	pop     ebp
	ret

; Copysign long, has two params
_copysignl:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Do some tests
	; High part of x
	mov		edx, dword [ebp + 28]
	and		edx, 0x00008000
	mov		eax, dword [ebp + 16]
	and		eax, 0x00007FFF
	or		eax, edx
	mov		dword [ebp + 16], eax

	; Load real from stack
	fld		tword [ebp + 8]

	; Unwind & return
	pop     ebp
	ret