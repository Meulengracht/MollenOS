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
; MollenOS x86-64 Math SQRT

bits 64
segment .text

;Functions in this asm
global sqrt
global sqrtf
global sqrtl

; Computes the square-root
sqrt:
	sqrtsd xmm0, xmm0
	ret

; Computes the square-root
sqrtf:
	sqrtss xmm0, xmm0
	ret

; Computes the square-root
sqrtl:
%ifdef _MICROSOFT_LIBM
	fld		tword [rdx]
	fsqrt
	mov 	rax, rcx
	mov 	qword [rcx + 8], 0
	fstp	tword [rcx]
%else
	fld 	tword [rsp + 8]
	fsqrt
%endif
	ret