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
; MollenOS x86-32 Math floating point hyperbolic tangent

bits 32
segment .text

;Functions in this asm
global  _tanh
global  __CItanh

; double __cdecl tanh(double x)
; floating point hyperbolic tangent
_tanh:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Load real from stack
	fld     qword [ebp+8]
	
	; Duplicate stack top
    fld     st0

	; Compute 2 * x
    fadd

	; Load log base 2(e)
    fldl2e

	; Multiply x * log base 2(e)
    fmulp   st1, st0

	; Push result
    fst     st1

	; Round to integer
    frndint

	; Subtract
    fsub    st1, st0

	; Exchange st, st(1)
    fxch

	; Compute 2 to the (x - 1)
    f2xm1

	; Load real number 1
    fld1

	; 2 to the x
    fadd

	; Scale by power of 2
    fscale

	; Set new stack top and pop
    fstp    st1

	; Load constant 1
    fld1

	; Compute exp(2*x)+1
    fadd

	; Load the constant 1
    fld1

	; Load the constant 1
    fld1

	; Set divisor to 2
    fadd

	; Compute 2/(exp(2*x)+1)
    fdivr

	; Load constant 1
    fld1

	; Compute the hyperbolic tangent
    fsubr
	
	; Unwind & return
	pop     ebp
	ret

__CItanh:
	; Allocate stack space for x
    sub     esp, 8

	; Copy x onto stack
    fstp    qword [esp]

	; Call tanh
    call    _tanh

	; Remove x from stack
    add     esp, 8
    ret
