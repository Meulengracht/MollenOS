; MollenOS
; Copyright 2019, Philip Meulengracht
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
; x86-32 get current context

bits 32
segment .text

%define REGISTER_EDI      0
%define REGISTER_ESI      4
%define REGISTER_EBP      8
%define REGISTER_ESP      12
%define REGISTER_EBX      16
%define REGISTER_EDX      20
%define REGISTER_ECX      24
%define REGISTER_EAX      28

%define REGISTER_GS       32 ; Not saved
%define REGISTER_FS       36
%define REGISTER_ES       40 ; Not saved
%define REGISTER_DS       44 ; Not saved

%define REGISTER_IRQ      48 ; Not saved
%define REGISTER_ERR      52 ; Not saved
%define REGISTER_EIP      56
%define REGISTER_CS       60 ; Not saved
%define REGISTER_EFLAGS   64 ; Not saved
%define REGISTER_USERESP  68 ; Not saved
%define REGISTER_USERSS   72 ; Not saved

;Functions in this asm
global _GetContext

; int GetContext(Context_t* Context);
_GetContext:
	mov eax, [esp + 4]

    mov [eax + REGISTER_EDI], edi
    mov [eax + REGISTER_ESI], esi
    mov [eax + REGISTER_EBP], ebp
    mov [eax + REGISTER_EBX], ebx
    mov [eax + REGISTER_EDX], edx
    mov [eax + REGISTER_ECX], ecx
    mov dword [eax + REGISTER_EAX], 0
    
    ; store fs
    xor edx, edx
    mov dx, fs
    mov [eax + REGISTER_FS], edx
    
    ; store eip/esp/eflags
    lea ecx, [esp + 4]
    mov [eax + REGISTER_ESP], ecx
    mov ecx, [esp]
    mov [eax + REGISTER_EIP], ecx
    pushfd
    pop ecx
    mov [eax + REGISTER_EFLAGS], ecx

    ; zero values not stored
    mov dword [eax + REGISTER_IRQ], 0
    mov dword [eax + REGISTER_ERR], 0
    mov dword [eax + REGISTER_CS], 0
    mov dword [eax + REGISTER_USERESP], 0
    mov dword [eax + REGISTER_USERSS], 0

	xor eax, eax
	ret 
