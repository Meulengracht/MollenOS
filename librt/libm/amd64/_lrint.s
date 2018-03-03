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
global lrint
global lrintf
global lrintl

; Rounds a double to long
lrint:
%ifdef _MICROSOFT_LIBM
	cvtsd2si rax, xmm0
%else
	cvtsd2si eax, xmm0
%endif
	ret

; Rounds a float to long
lrintf:
%ifdef _MICROSOFT_LIBM
	cvtss2si rax, xmm0
%else
	cvtss2si eax, xmm0
%endif
	ret

; Rounds a long double to long
lrintl:
%ifdef _MICROSOFT_LIBM
	fld 	tword [rcx]
%else
	fld 	tword [rsp + 8]
%endif
	sub     rsp, 8
	fistp	qword [rsp]
	pop		rax
	ret