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
; MollenOS x86-32 Setjmp/Longjmp routines

bits 32
segment .text

;Defs
%define REGISTER_BP  0
%define REGISTER_BX  4
%define REGISTER_DI  8
%define REGISTER_SI  12
%define REGISTER_SP  16
%define REGISTER_IP  20

;Functions in this asm
global __setjmp
global __setjmp3
global _longjmp

; int _setjmp(jmp_buf env);
__setjmp:
	; We don't use a stack frame here

	; Get params
	mov edx, [esp + 4]

	; Save state
    mov [edx + REGISTER_BP], ebp
    mov [edx + REGISTER_BX], ebx
    mov [edx + REGISTER_DI], edi
    mov [edx + REGISTER_SI], esi
    lea ecx, [esp + 4]
    mov [edx + REGISTER_SP], ecx
    mov ecx, [esp]
    mov [edx + REGISTER_IP], ecx

	; Done
	xor eax, eax
	ret 

; int _setjmp3(jmp_buf env, int nb_args, ...);
__setjmp3:
	; We don't use a stack frame here

	; Get params
	mov edx, [esp + 4]

	; Save state
    mov [edx + REGISTER_BP], ebp
    mov [edx + REGISTER_BX], ebx
    mov [edx + REGISTER_DI], edi
    mov [edx + REGISTER_SI], esi
    lea ecx, [esp + 4]
    mov [edx + REGISTER_SP], ecx
    mov ecx, [esp]
    mov [edx + REGISTER_IP], ecx

	; Done
	xor eax, eax
	ret 

; void longjmp(jmp_buf env, int value);
_longjmp:
	; Don't use a stack frame here

	; Get jump buffer
    mov ecx, [esp + 4]

	; Get value
    mov eax, [esp + 8]

	; Save return address
    mov edx, [ecx + REGISTER_IP]
    
	; Restore State
    mov ebp, [ecx + REGISTER_BP]
    mov ebx, [ecx + REGISTER_BX]
    mov edi, [ecx + REGISTER_DI]
    mov esi, [ecx + REGISTER_SI]
    mov esp, [ecx + REGISTER_SP]
    
	; Return
    jmp edx