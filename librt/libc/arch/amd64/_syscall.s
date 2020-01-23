; MollenOS
; Copyright 2016, Philip Meulengracht
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
; x86-64 Syscall Assembly Routine

bits 64
segment .text

;Functions in this asm
global _syscall

; int _syscall(int Function, int Arg0, int Arg1, int Arg2, int Arg3, int Arg4)
_syscall:
    push rbx
    push rsi
    push rdi
    
    ; index is rcx
    xchg rcx, rax

    ; args is rdx, r8, r9, stack
    mov rbx, rdx
    mov rcx, r8
    mov rdx, r9
    mov r8, qword [rsp + 64] ; arg0 is 32 + 8 + (3 * 8) (reserved + return address + stack space)
    mov r9, qword [rsp + 72] ; arg1 is 32 + 8 + 8 + (3 * 8) (reserved + return address + arg0 + stack space)
	int	60h
	
	pop rdi
	pop rsi
	pop rbx
	ret
