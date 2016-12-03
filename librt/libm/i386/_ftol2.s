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
; MollenOS x86-32 Math FTOL2

bits 32
segment .text

;Functions in this asm
extern __ftol
global __ftol2
global __ftol2_sse

; This routine is called by MSVC-generated code to convert from floating point
; to integer representation.
__ftol2:
	push        ebp 
	mov         ebp,esp
	sub         esp,20h

	and         esp,0FFFFFFF0h
	fld         st0
	fst         dword [esp+18h]
	fistp       qword [esp+10h]
	fild        qword [esp+10h]
	mov         edx,dword [esp+18h]
	mov         eax,dword [esp+10h]
	test        eax,eax
	je          integer_QnaN_or_zero

arg_is_not_integer_QnaN:
	fsubp       st1, st0
	test        edx,edx
	jns         positive
	fstp        dword [esp]
	mov         ecx,dword [esp]
	xor         ecx,80000000h
	add         ecx,7FFFFFFFh
	adc         eax,0
	mov         edx,dword [esp+14h]
	adc         edx,0
	jmp         localexit

positive:
	fstp        dword [esp]
	mov         ecx,dword [esp]
	add         ecx,7FFFFFFFh
	sbb         eax,0
	mov         edx,dword [esp+14h]
	sbb         edx,0
	jmp         localexit

integer_QnaN_or_zero:
	mov         edx,dword [esp+14h]
	test        edx,7FFFFFFFh
	jne         arg_is_not_integer_QnaN
	fstp        dword [esp+18h]
	fstp        dword [esp+18h]

localexit:
	leave           
	ret   

__ftol2_sse:
    jmp __ftol2
