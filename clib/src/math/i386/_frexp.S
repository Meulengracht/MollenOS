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
; MollenOS x86-32 Math FREXP

bits 32
segment .text

;Functions in this asm
global _frexp

; double __cdecl frexp(double x, int *y)
; get normalized fraction and exponent
_frexp:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Save register edi
    push    edi

	; Load real from stack
    fld     qword [ebp + 8]

	; Put exponent address in edi
    mov     edi, dword [ebp + 16]

	; Test st for zero
    ftst

	; Put test result in ax
    fstsw   ax

	; Set flags based on test
    sahf

	; Re-direct if not zero
    jnz     __frexp1

	; Set exponent to zero
    fld     st0

	; End of case
    jmp     __frexp2
__frexp1:       
	; Get exponent and significand
	fxtract

	; Load constant 1
	fld1

	; Load constant 1
    fld1

	; Constant 2
    fadd

	; Significand / 2
    fdiv

	; Swap st, st(1)
    fxch

	; Load constant 1
    fld1

	; Increment exponent
    fadd

	; Store result exponent and pop
    fistp   dword [edi]

__frexp2:       
	; Restore register edi
	pop     edi

	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret

