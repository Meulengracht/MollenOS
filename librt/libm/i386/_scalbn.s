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
; MollenOS x86-32 Math SCALBN

bits 32
segment .text

;Functions in this asm
global _scalbn
global _scalbnf
global _scalbnl
global _ldexpl

; The math scale
_scalbn:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Load real from stack
	fild    dword [ebp + 16]
	fld     qword [ebp + 8]
	fscale
	
	; Store result + restore stack
	fstp	st1
	
	; Unwind & return
	pop     ebp
	ret

; The math scale float
_scalbnf:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Load real from stack
	fild    dword [ebp + 12]
	fld     dword [ebp + 8]
	fscale
	
	; Store result
	fstp	st1
	
	; Unwind & return
	pop     ebp
	ret

; The math scale long double
_scalbnl:
	push    ebp
	mov     ebp, esp

	; Load real from stack
	fild    dword [ebp + 20]
	fld     tword [ebp + 8]
	fscale
	
	; Store result
	fstp	st1
	pop     ebp
	ret