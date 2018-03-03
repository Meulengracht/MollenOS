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
; MollenOS x86-64 math remainder 

bits 64
segment .text

;Functions in this asm
global remainder
global remainderf
global remainderl

; takes the remainder
remainder:
	movsd	qword [rsp - 8], xmm0
	movsd	qword [rsp - 16], xmm1
	fld 	qword [rsp - 16]
	fld 	qword [rsp - 8]

	.Calculate:
		fprem1
		fstsw	ax
		test	ax, 0x0400
		jne 	.Calculate

	fstp	qword [rsp - 8]
	movsd 	xmm0, qword [rsp - 8]
	fstp	st0
	ret

; takes the remainder float
remainderf:
	movss	dword [rsp - 4], xmm0
	movss	dword [rsp - 8], xmm1
	fld 	dword [rsp - 8]
	fld 	dword [rsp - 4]

	.Calculate:
		fprem1
		fstsw	ax
		test	ax, 0x0400
		jne 	.Calculate

	fstp	dword [rsp - 4]
	movss 	xmm0, dword [rsp - 4]
	fstp	st0
	ret

; takes the remainder long double
remainderl:
%ifdef _MICROSOFT_LIBM
	fld		tword [r8]
	fld 	tword [rdx]
%else
	fld 	tword [rsp + 24]
	fld 	tword [rsp + 8]
%endif

	.Calculate:
		fprem1
		fstsw	ax
		test	ax, 0x0400
		jne 	.Calculate
	
	fstp 	st1
%ifdef _MICROSOFT_LIBM
	mov 	rax, rcx
	mov 	qword [rcx + 8], 0
	fstp	tword [rcx]
%endif
	ret