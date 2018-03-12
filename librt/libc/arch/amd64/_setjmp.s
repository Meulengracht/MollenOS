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
; MollenOS x86-64 Setjmp/Longjmp routines

bits 64
segment .text

;Defs
%define REGISTER_FRAME		0
%define REGISTER_BX			8
%define REGISTER_SP			16
%define REGISTER_BP			24
%define REGISTER_SI			32
%define REGISTER_DI			40
%define REGISTER_R12		48
%define REGISTER_R13		56
%define REGISTER_R14		64
%define REGISTER_R15		72
%define REGISTER_IP			80
%define REGISTER_SPARE		88
%define REGISTER_XMM6		96
%define REGISTER_XMM7		112
%define REGISTER_XMM8		128
%define REGISTER_XMM9		144
%define REGISTER_XMM10		160
%define REGISTER_XMM11		176
%define REGISTER_XMM12		192
%define REGISTER_XMM13		208
%define REGISTER_XMM14		224
%define REGISTER_XMM15		240

;Functions in this asm
global _setjmp
global _setjmp3
global longjmp

; int _setjmp(jmp_buf env);
_setjmp:
	; We don't use a stack frame here

	; Get SP
	lea rax, [rsp + 8]

	; Load IP
	mov r8, [rsp]

	; Save state
    mov qword [rcx + REGISTER_FRAME], 0
    mov [rcx + REGISTER_BX], rbx
    mov [rcx + REGISTER_BP], rbp
    mov [rcx + REGISTER_SI], rsi
    mov [rcx + REGISTER_DI], rdi
    mov [rcx + REGISTER_R12], r12
    mov [rcx + REGISTER_R13], r13
    mov [rcx + REGISTER_R14], r14
    mov [rcx + REGISTER_R15], r15
    mov [rcx + REGISTER_SP], rax
    mov [rcx + REGISTER_IP], r8

	; Save SSE Registers
    movdqa [rcx + REGISTER_XMM6], xmm6
    movdqa [rcx + REGISTER_XMM7], xmm7
    movdqa [rcx + REGISTER_XMM8], xmm8
    movdqa [rcx + REGISTER_XMM9], xmm9
    movdqa [rcx + REGISTER_XMM10], xmm10
    movdqa [rcx + REGISTER_XMM11], xmm11
    movdqa [rcx + REGISTER_XMM12], xmm12
    movdqa [rcx + REGISTER_XMM13], xmm13
    movdqa [rcx + REGISTER_XMM14], xmm14
    movdqa [rcx + REGISTER_XMM15], xmm15

	; Done
	xor rax, rax
	ret 

; int _setjmpex(jmp_buf _Buf,void *_Ctx);
_setjmp3:
	; We don't use a stack frame here

	; Get SP
	lea rax, [rsp + 8]

	; Load IP
	mov r8, [rsp]

	; Save state
    mov qword [rcx + REGISTER_FRAME], 0
    mov [rcx + REGISTER_BX], rbx
    mov [rcx + REGISTER_BP], rbp
    mov [rcx + REGISTER_SI], rsi
    mov [rcx + REGISTER_DI], rdi
    mov [rcx + REGISTER_R12], r12
    mov [rcx + REGISTER_R13], r13
    mov [rcx + REGISTER_R14], r14
    mov [rcx + REGISTER_R15], r15
    mov [rcx + REGISTER_SP], rax
    mov [rcx + REGISTER_IP], r8

	; Save SSE Registers
    movdqa [rcx + REGISTER_XMM6], xmm6
    movdqa [rcx + REGISTER_XMM7], xmm7
    movdqa [rcx + REGISTER_XMM8], xmm8
    movdqa [rcx + REGISTER_XMM9], xmm9
    movdqa [rcx + REGISTER_XMM10], xmm10
    movdqa [rcx + REGISTER_XMM11], xmm11
    movdqa [rcx + REGISTER_XMM12], xmm12
    movdqa [rcx + REGISTER_XMM13], xmm13
    movdqa [rcx + REGISTER_XMM14], xmm14
    movdqa [rcx + REGISTER_XMM15], xmm15

	; Done
	xor rax, rax
	ret 

; void longjmp(jmp_buf env, int value);
longjmp:
	; Don't use a stack frame here

	; Restore registers
	mov rbx, [rcx + REGISTER_BX]
    mov rbp, [rcx + REGISTER_BP]
    mov rsi, [rcx + REGISTER_SI]
    mov rdi, [rcx + REGISTER_DI]
    mov r12, [rcx + REGISTER_R12]
    mov r13, [rcx + REGISTER_R13]
    mov r14, [rcx + REGISTER_R14]
    mov r15, [rcx + REGISTER_R15]
    mov rsp, [rcx + REGISTER_SP]
    mov r8, [rcx + REGISTER_IP]

	; Restore SSE
    movdqa xmm6, [rcx + REGISTER_XMM6]
    movdqa xmm7, [rcx + REGISTER_XMM7]
    movdqa xmm8, [rcx + REGISTER_XMM8]
    movdqa xmm9, [rcx + REGISTER_XMM9]
    movdqa xmm10, [rcx + REGISTER_XMM10]
    movdqa xmm11, [rcx + REGISTER_XMM11]
    movdqa xmm12, [rcx + REGISTER_XMM12]
    movdqa xmm13, [rcx + REGISTER_XMM13]
    movdqa xmm14, [rcx + REGISTER_XMM14]
    movdqa xmm15, [rcx + REGISTER_XMM15]

    ; Which retval should be return
    mov rax, rdx
    test rax, rax

	; Return either 1 or the given value
    jnz l2
    inc rax
l2: jmp r8