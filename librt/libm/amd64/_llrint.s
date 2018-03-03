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
; MollenOS x86-64 Math Round To Int

bits 64
segment .text

;Functions in this asm
global llrint
global llrintf
global llrintl

; Rounds double to long long
llrint:
	cvtsd2si rax, xmm0
	ret

; Rounds float to long long
llrintf:
	cvtss2si rax, xmm0
	ret

; Rounds long double to long long
llrintl:
%ifdef _MICROSOFT_LIBM
	fld 	tword [rcx]
%else
	fld 	tword [rsp + 8]
%endif
	sub     rsp, 8
	fistp	qword [rsp]
	pop		rax
	ret