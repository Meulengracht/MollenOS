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
; rcx => arg0
; rdx => arg1
; r8  => arg2
; r9  => arg3
; r10 => arg4
; r11 => index
_syscall:
    ; index is rcx
    xchg rcx, r11

    ; args is rdx, r8, r9, stack
    mov rcx, rdx
    mov rdx, r8
    mov r8, r9
    mov r9, qword [rsp + 40]  ; arg0 is 32 + 8 (reserved + return address)
    mov r10, qword [rsp + 48] ; arg1 is 32 + 8 + 8 (reserved + return address + arg0)
	int	80h
	ret