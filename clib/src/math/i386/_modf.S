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
; MollenOS x86-32 Math MODF

bits 32
segment .text

;Functions in this asm
global _modf

; double __cdecl modf(double x, double *y)
_modf:
	; Stack Frame
    push    ebp
	mov     ebp, esp

	; Save register edi
    push    edi

	; Load real from stack
    fld     qword [ebp+8]

	; Put integer address in edi
    mov     edi,dword [ebp+16]

	; Duplicate st
    fld     st0

	; Round to integer
    frndint

	; Compare with orignal value
    fcom    st1

	; Get the FPU status word
    fstsw   ax

	; Test if number is negative
    test    byte [ebp+15], 0x080

	; Re-direct if positive
    jz      __fmodf1

	; Store AH to flags
    sahf

	; Re-direct if greater or equal
    jnb     __fmodf2

	; Load the constant 1
    fld1

	; Increment integer part
    fadd

	; End of case
    jmp     __fmodf2

__fmodf1:
	; Store AH to flags     
	sahf

	; Re-direct if less or equal
    jna     __fmodf2

	; Load constant 1
    fld1

	; Decrement integer part
    fsub

__fmodf2:
	; Store integer part
	fst     qword [edi]       
	
	; Subtract to get fraction      
    fsub

	; Restore register edi
    pop     edi

	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret