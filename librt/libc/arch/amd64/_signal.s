; MollenOS
;
; Copyright 2011, Philip Meulengracht
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
; x86-64 Signal Code
;

bits 64
segment .text

global __signalentry
extern StdInvokeSignal

; void __signalentry(int signal (rcx), void* argument (rdx), reg_t flags (r8))
; Fixup stack and call directly into the signal handler
__signalentry:
	; Check which of these are non-volatile, and skip storing state of these
	; r12-15 are non volatile
	; rdi, rsi, rbp, rsp, rbx are non-volatile
	xchg bx, bx
	push rax
	push rbx
	push r9
	push r10
	push r11
	
    ; Prepare stack alignment
    mov rbx, 0x20 ; this register is non-volatile, so we use this
    mov rax, rbx
    and rax, 0xF
    add rbx, rax
	sub rsp, rbx
	call StdInvokeSignal ; (int, void*, unsigned)
	add rsp, rbx
	
    ; Restore initial state and switch the handler stack to next one stored
	pop r11
	pop r10
	pop r9
	pop rbx
	pop rax
	pop r8
	pop rdx
	pop rcx
	pop rsp
    ret
