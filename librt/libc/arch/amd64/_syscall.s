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
; MollenOS x86-64 Syscall Assembly Routine

bits 64
segment .text

;Functions in this asm
global _syscall

; int _syscall(int Function, int Arg0, int Arg1, int Arg2, int Arg3, int Arg4)
_syscall:
	; Stack Frame
	push rbp
	mov rbp, rsp

	; Save
	push rbx
	push rcx
	push rdx
	push rsi
	push rdi

	; Get params
	mov rax, rcx
    mov rbx, rdx
    mov rcx, r8
    mov rdx, r9
    mov rsi, [rbp + 0x30]
    mov rdi, [rbp + 0x38]

	; Syscall
	int	80h

	; Restore
	pop rdi
	pop rsi
	pop rdx
	pop rcx
	pop rbx
	leave
	ret 