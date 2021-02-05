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
; x86-64 get current context

bits 64
segment .text

%define REGISTER_RDI      0
%define REGISTER_RSI      8
%define REGISTER_RBP      16
%define REGISTER_RSP      24
%define REGISTER_RBX      32
%define REGISTER_RDX      40
%define REGISTER_RCX      48
%define REGISTER_RAX      56

%define REGISTER_R8       64
%define REGISTER_R9       72
%define REGISTER_R10      80
%define REGISTER_R11      88
%define REGISTER_R12      96
%define REGISTER_R13      104
%define REGISTER_R14      112
%define REGISTER_R15      120

%define REGISTER_IRQ      128 ; Not saved
%define REGISTER_ERR      136 ; Not saved
%define REGISTER_RIP      144
%define REGISTER_CS       152 ; Not saved
%define REGISTER_EFLAGS   160 ; Not saved
%define REGISTER_USERESP  168 ; Not saved

;Functions in this asm
global GetContext

; int GetContext(Context_t* Context);
GetContext:
    mov [rcx + REGISTER_RDI], rdi
    mov [rcx + REGISTER_RSI], rsi
    mov [rcx + REGISTER_RBP], rbp
    mov [rcx + REGISTER_RBX], rbx
    mov [rcx + REGISTER_RDX], rdx
    mov qword [rcx + REGISTER_RCX], 0
    mov [rcx + REGISTER_RAX], rax
    
    mov [rcx + REGISTER_R8], r8
    mov [rcx + REGISTER_R9], r9
    mov [rcx + REGISTER_R10], r10
    mov [rcx + REGISTER_R11], r11
    mov [rcx + REGISTER_R12], r12
    mov [rcx + REGISTER_R13], r13
    mov [rcx + REGISTER_R14], r14
    mov [rcx + REGISTER_R15], r15

    ; store eip/esp
    lea rax, [rsp + 8]
    mov [rcx + REGISTER_RSP], rax
    mov rax, [rsp]
    mov [rcx + REGISTER_RIP], rax
    
    ; store fpregs TODO

	xor rax, rax
	ret 
