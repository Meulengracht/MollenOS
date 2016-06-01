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
; MollenOS x86-32 Math floating point hyperbolic sine

bits 32
segment .text

;Functions in this asm
global _sinh
global  __CIsinh

; double __cdecl sinh(double x)
; floating point hyperbolic sine
_sinh:
	; Stack Frame
	push    ebp
	mov     ebp, esp

    sub     esp,8                   ; Allocate temporary space
    fld     qword [ebp+8]           ; Load real from stack
    fchs                            ; Set x = -x
    fldl2e                          ; Load log base 2(e)
    fmulp   st1,st0                 ; Multiply x * log base 2(e)
    fst     st1                     ; Push result
    frndint                         ; Round to integer
    fsub    st1,st0                 ; Subtract
    fxch                            ; Exchange st, st(1)
    f2xm1                           ; Compute 2 to the (x - 1)
    fld1                            ; Load real number 1
    fadd                            ; 2 to the x
    fscale                          ; Scale by power of 2
    fstp    st1                     ; Set new stack top and pop
    fstp    qword [ebp-8]           ; Save exp(-x)
    fld     qword [ebp+8]           ; Load real from stack
    fldl2e                          ; Load log base 2(e)
    fmulp   st1,st0                 ; Multiply x * log base 2(e)
    fst     st1                     ; Push result
    frndint                         ; Round to integer
    fsub    st1,st0                 ; Subtract
    fxch                            ; Exchange st, st(1)
    f2xm1                           ; Compute 2 to the (x - 1)
    fld1                            ; Load real number 1
    fadd                            ; 2 to the x
    fscale                          ; Compute exp(-x)
    fstp    st1                     ; Set new stack top and pop
    fld     qword [ebp-8]           ; Get exp(x)
    fsub                            ; Compute exp(x) - exp(-x)
    fld1                            ; Load the constant 1
    fld1                            ; Load the constant 1
    fadd                            ; Set divisor to 2
    fdiv                            ; Compute the hyperbolic sine
	
	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret

__CIsinh:
	; Allocate stack space for x
    sub     esp, 8

	; Copy x onto stack
    fstp    qword [esp]    
	
	; Call sinh         
    call    _sinh

	; Remove x from stack
    add     esp, 8
    ret
