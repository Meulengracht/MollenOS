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
; MollenOS x86-32 Math Floating Power POW

bits 32
segment .text

;Functions in this asm
global  _pow
global  __CIpow

;errno 
extern __errno

; Floating point zero
fzero:          dq      0.0e0

; double __cdecl pow(double x, double y)
; floating point power
_pow:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Allocate temporary space
    sub     esp, 12

	; Save register edi/eax
    push    edi 
    push    eax

	; Set negation flag to zero
    mov     dword [ebp - 12], 0

	; Load reals from stack
    fld     qword [ebp + 16]          
    fld     qword [ebp + 8]

	; Point to real zero
    mov     edi, fzero

	; Compare x with zero
    fcom    qword [edi]

	; Get the FPU status word
    fstsw   ax

	; Move condition flags to AL
    mov     al, ah
	
	; Load Flags into AH
    lahf

	; Isolate  C0, C2 and C3
    and     al, 01000101B

	; Turn off CF, PF and ZF
    and     ah, ~01000101B
	
	; Set new  CF, PF and ZF 
    or      ah, al

	; Store AH into Flags
    sahf

	; Re-direct if x < 0
    jb      __fpow1

	; Re-direct if x > 0
    ja      __fpow3

	; Swap st, st(1)
    fxch

	; Compare y with zero
    fcom    qword [edi]

	; Restore x as top of stack
    fxch

	; Get the FPU status word
    fstsw   ax

	; Move condition flags to AL
    mov     al, ah             
	
	; Load Flags into AH      
    lahf

	; Isolate  C0, C2 and C3
    and     al, 01000101B

	; Turn off CF, PF and ZF
    and     ah, ~01000101B

	; Set new  CF, PF and ZF
    or      ah, al

	; Store AH into Flags
    sahf

	; Re-direct if y > 0
    ja      __fpow3

	; Set new stack top and pop
    fstp    st1

	; Set domain error (EDOM)
    mov     eax, 1

	; End of case
    jmp     __fpow5

__fpow1:        
	; Put y on top of stack
	fxch

	; Duplicate y as st1
    fld     st0
	
	; Round to integer              
    frndint

	; Put y on top of stack
    fxch

	; y = int(y) ?
    fcomp

	; Get the FPU status word
    fstsw   ax

	; Move condition flags to AL
    mov     al, ah

	; Load Flags into AH
    lahf

	; Isolate  C0, C2 and C3
    and     al, 01000101B

	; Turn off CF, PF and ZF
    and     ah, ~01000101B

	; Set new  CF, PF and ZF
    or      ah, al

	; Store AH into Flags
    sahf

	; Proceed if y = int(y)
    je      __fpow2

	; Set new stack top and pop
    fstp    st1

	; Set result to zero
    fldz

	; Set new stack top and pop
    fstp    st1

	; Set domain error (EDOM)
    mov     eax, 33

	; End of case
    jmp     __fpow5

__fpow2:
	; Store y as integer
	fist    dword [ebp - 12]
    
	; Set bit if y is odd
	and     dword [ebp - 12], 1
    
	; Put x on top of stack
	fxch
    
	; x = |x|
	fabs

__fpow3:        
	; Load log base e of 2
	fldln2

	; Exchange st0, st1
    fxch    st1

	; Compute the natural log(x)
    fyl2x

	; Compute y * ln(x)
    fmul

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

	; Exchange st0, st1
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

	; Negation required ?
    test    dword [ebp - 12], 1

	; No, re-direct
    jz      __fpow4
	
	; Negate the result        
    fchs

__fpow4:        
	; Save (double)pow(x, y)
	fstp    qword [ebp - 8]

	; Load (double)pow(x, y)
    fld     qword [ebp - 8]

	; Examine st
    fxam

	; Get the FPU status word
    fstsw   ax

	; Infinity ?
    cmp     ah, 5

	; No, end of case
    jne     __fpow6

	; Set range error (ERANGE)
    mov     eax, 34

__fpow5:
	; Breakpt
	int     3
	
	; Set errno
	mov     dword [__errno], eax

__fpow6:
	; Restore registers eax & edi
	pop     eax
	pop     edi
	
	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret

__CIpow:
	; Allocate stack space for args
    sub     esp, 16

	; Copy X & Y onto stack
    fstp    qword [esp + 8]
    fstp    qword [esp]
    call    _pow

	; Remove args from stack
    add     esp, 16
    ret