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
; MollenOS x86-64 Math SCALBN

bits 64
segment .text

;Functions in this asm
global scalbn
global scalbnf
global scalbnl

; The math scale
scalbn:
	movsd 	qword [rsp - 8], xmm0
%ifdef _MICROSOFT_LIBM
	mov 	dword [rsp - 12], edx
%else
	mov 	dword [rsp - 12], edi
%endif
	fild	dword [rsp - 12]
	fld 	qword [rsp - 8]
	fscale
	fstp	st1
	fstp	qword [rsp - 8]
	movsd 	xmm0, qword [rsp - 8]
	ret

; The math scale float
scalbnf:
	movss 	dword [rsp - 8], xmm0
%ifdef _MICROSOFT_LIBM
	mov 	dword [rsp - 4], edx
%else
	mov 	dword [rsp - 4], edi
%endif
	fild	dword [rsp - 4]
	fld 	dword [rsp - 8]
	fscale
	fstp	st1
	fstp	dword [rsp - 8]
	movss 	xmm0, dword [rsp - 8]
	ret

; The math scale long double
scalbnl:
%ifdef _MICROSOFT_LIBM
	mov 	rax, r8
	mov 	dword [rsp - 4], eax
	fild 	qword [rsp - 4]
	fld 	tword [rdx]
%else
	mov 	dword [rsp - 4], edi
	fild	qword [rsp - 4]
	fld 	tword [rsp + 8]
%endif
	fscale
	fstp	st1
%ifdef _MICROSOFT_LIBM
	mov 	rax, rcx
	mov 	qword [rcx + 8], 0
	fstp	tword [rcx]
%endif
	ret