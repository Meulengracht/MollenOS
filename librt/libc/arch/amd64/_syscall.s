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
    ; reorder arguments
    pop r10 ;arg3
    pop r11 ;arg4

    push r12
    mov r12, rcx 
    mov rcx, rdx
    mov rdx, r8
    mov r8, r9
    mov r9, r10
    mov r10, r11
    mov r11, r12
    pop r12
	int	80h
	ret