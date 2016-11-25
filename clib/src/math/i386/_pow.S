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
; MollenOS x86-32 X-Power-To-Y

bits 32
segment .text

; Functions in this asm
global  _pow
global  __CIpow

; Externs
extern ___errno

; Floating point zero
fzero:          dq      0.0e0
fone:			dq		1.0e0

; double pow(double x, double y)
; X-Power-To-Y
_pow:
	; Entry, Setup Frame
    push    ebp
    mov     ebp, esp

	; Allocate temporary space
    sub     esp, 24
	
	; Save modified registers       
    push    edi
	push	ecx
    push    eax

	; Set negation flag to zero
	mov     dword [ebp - 12], 0

	; The type of y, if int
	mov		dword [ebp - 24], 0

	; Store high-parts (yh at -20, xh at -16)
	mov		eax, dword [ebp + 20]
	and		eax, 0x7FFFFFFF
	mov		dword [ebp - 20], eax

	mov		eax, dword [ebp + 12]
	and		eax, 0x7FFFFFFF
	mov		dword [ebp - 16], eax

	; Special case 1: (anything) ** 0 is 1
	.Case1:
		cmp		dword [ebp - 20], 0
		jne		.Case2
		cmp		dword [ebp + 16], 0
		je		.SetOne

	; Special case 2: x==1: 1**y = 1, even if y is NaN
	.Case2:
		cmp		dword [ebp + 12], 0x3FFFFFFF
		jne		.Case3_1
		mov		eax, dword [ebp + 8]
		je		.SetOne
	
	; Special case 3: y!=zero: result is NaN if either arg is NaN	
	.Case3_1:
		cmp		dword [ebp - 16], 0x7FF00000
		ja		.SetNaN
		jne		.Case3_2
		cmp		dword [ebp + 8], 0
		je		.SetNaN

	.Case3_2:
		cmp		dword [ebp - 20], 0x7FF00000
		ja		.SetNaN
		jne		.DetermineY
		cmp		dword [ebp + 16], 0
		je		.SetNaN


	; Determine type of y
	.DetermineY:
		cmp		dword [ebp + 12], 0
		jnb		.Case4

		; Test if y is an even integer
		cmp		dword [ebp - 20], 0x43400000
		jb		.DetermineY_1
		mov		dword [ebp - 24], 2
		jmp		.Case4

		.DetermineY_1:
			cmp		dword [ebp - 20], 0x3FF00000
			jb		.Case4

			; Get exponent
			mov		eax, dword [ebp - 20]
			shr		eax, 20
			sub		eax, 0x3FF
			cmp		eax, 20
			jbe		.DetermineY_2

			; eax = lower_y >> ecx(52 - eax)
			mov		ecx, 52
			sub		ecx, eax
			mov		eax, dword [ebp + 16]
			shr		eax, cl
			push	eax

			; if((eax<<ecx) == lower_y)
			shl		eax, cl
			cmp		dword [ebp + 16], eax
			pop		eax
			jne		.Case4

			; ytype = 2-(eax&1)
			and		eax, 0x1
			mov		dword [ebp - 24], 2
			sub		dword [ebp - 24], eax
			jmp		.Case4

		.DetermineY_2:
			cmp		dword [ebp + 16], 0
			jne		.Case4

			; eax = mod_hy >> ecx(20 - eax)
			mov		ecx, 20
			sub		ecx, eax
			mov		eax, dword [ebp - 20]
			shr		eax, cl
			push	eax

			; if((eax << ecx) == mod_hy)
			shl		eax, cl
			cmp		dword [ebp - 20], eax
			pop		eax
			jne		.Case4

			; ytype = 2 - (eax&1)
			and		eax, 0x1
			mov		dword [ebp - 24], 2
			sub		dword [ebp - 24], eax

	; Special case 4: Special value of y
	.Case4:
		cmp		dword [ebp + 16], 0
		jne		.Case5

		; Case 4.1: y is +-inf
		.Case4_1:
			cmp		dword [ebp - 20], 0x7FF00000
			jne		.Case4_2
			mov		eax, dword [ebp - 16]
			sub		eax, 0x3FF00000
			or		eax, dword [ebp + 8]
			cmp		eax, 0
			je		.SetOne
			cmp		dword [ebp - 16], 0x3FFFFFFF
			jb		.Case4_1_1
			cmp		dword [ebp + 20], 0
			jb		.SetZero

			; Return y
			fld     qword [ebp + 16]
			jmp		.Leave

			; Case 4.1.1: (|x|<1)**-,+inf = inf,0
			.Case4_1_1:
				; Determine to ret zero or -y
				cmp		dword [ebp + 20], 0
				jae		.SetZero

				; Return y negated
				fld     qword [ebp + 16]
				fchs
				jmp		.Leave

		; Case 4.2: y is  +-1
		.Case4_2:
			cmp		dword [ebp - 20], 0x3FFFFFFF
			jne		.Case4_3
			cmp		dword [ebp + 20], 0
			jb		.SetOne

			; Return x
			fld     qword [ebp + 8]
			jmp		.Leave

		; Case 4.3: y is 2
		.Case4_3:
			cmp		dword [ebp + 20], 0x40000000
			jne		.Case4_4

			; Load x twice, multiply
			fld     qword [ebp + 8]
			fld     qword [ebp + 8]
			fmulp	st1, st0
			jmp		.Leave

		; Case 4.4: y is 3
		.Case4_4:
			cmp		dword [ebp + 20], 0x40080000
			jne		.Case4_5

			; Load x thrice, multiply
			fld     qword [ebp + 8]
			fld     qword [ebp + 8]
			fmulp	st1, st0
			fld     qword [ebp + 8]
			fmulp	st1, st0
			jmp		.Leave

		; Case 4.5: y is 4
		.Case4_5:
			cmp		dword [ebp + 20], 0x40100000
			jne		.Case4_6

			; Load x twice, multiply
			fld     qword [ebp + 8]
			fld     qword [ebp + 8]
			fmulp	st1, st0			; St0 == x*x
			fld		st0					; Copy st0
			fmulp	st1, st0			; x*x * x*x
			jmp		.Leave

		; Case 4.6: y is 0.5
		.Case4_6:
			cmp		dword [ebp + 20], 0x3FE00000
			jne		.Case5
			cmp		dword [ebp + 12], 0
			jb		.Case5

			; Just return squareroot
			; of x
			fld     qword [ebp + 8]
			fsqrt
			jmp		.Leave
	
	; Special case 5: Special value of x
	.Case5:
		cmp		dword [ebp + 8], 0
		jne		.Case6
		cmp		dword [ebp - 16], 0x7FF00000
		je		.Case5_1
		cmp		dword [ebp - 16], 0
		je		.Case5_1
		cmp		dword [ebp - 16], 0x3FF00000
		jne		.Case6

		; If we reach here, x has a special value case
		; and we return z
		.Case5_1:
			fld     qword [ebp + 8]
			fabs
			cmp		dword [ebp + 20], 0
			jae		.Case5_2

			; st0 = (1/|x|)
			fld1
			fxch
			fdivp	st1, st0

		.Case5_2:
			cmp		dword [ebp + 12], 0
			jnb		.Leave

			; xh - 0x3FF00000 | ytype
			mov		eax, dword [ebp - 16]
			sub		eax, 0x3FF00000
			or		eax, dword [ebp - 24]
			cmp		eax, 0
			jne		.Case5_3

			; (-1)**non-int is NaN
			; st0-st0/st0-st0
			fld		st0
			fsubp	st1, st0
			fld		st0
			fdivp	st1, st0
			jmp		.Leave

		.Case5_3:
			cmp		dword [ebp - 24], 1
			jne		.Leave

			; Negate st0
			fchs
			jmp		.Leave

	.Case6:
		

	.PowNatural:
		; Load y from stack
		fld     qword [ebp + 16]

		; Load x from stack
		fld     qword [ebp + 8]
		mov     edi, fzero              ; Point to real zero
		fcom    qword [edi]             ; Compare x with zero
		fstsw   ax                      ; Get the FPU status word
		mov     al,ah                   ; Move condition flags to AL
		lahf                            ; Load Flags into AH
		and     al, 01000101B           ; Isolate  C0, C2 and C3
		and     ah,~01000101B           ; Turn off CF, PF and ZF
		or      ah,al                   ; Set new  CF, PF and ZF
		sahf                            ; Store AH into Flags
		jb      .XLessThan0             ; Re-direct if x < 0
		ja      .PositiveXY             ; Re-direct if x > 0
		fxch                            ; Swap st, st(1)
		fcom    qword [edi]             ; Compare y with zero
		fxch                            ; Restore x as top of stack
		fstsw   ax                      ; Get the FPU status word
		mov     al,ah                   ; Move condition flags to AL
		lahf                            ; Load Flags into AH
		and     al, 01000101B           ; Isolate  C0, C2 and C3
		and     ah,~01000101B           ; Turn off CF, PF and ZF
		or      ah,al                   ; Set new  CF, PF and ZF
		sahf                            ; Store AH into Flags
		ja      .PositiveXY             ; Re-direct if y > 0
		fstp    st1                     ; Set new stack top and pop
		mov     eax,1                   ; Set domain error (EDOM)
		jmp     .SetErrno               ; End of case

		.XLessThan0:
			fxch                            ; Put y on top of stack
			fld     st0                     ; Duplicate y as st1
			frndint                         ; Round to integer
			fxch                            ; Put y on top of stack
			fcomp                           ; y = int(y) ?
			fstsw   ax                      ; Get the FPU status word
			mov     al,ah                   ; Move condition flags to AL
			lahf                            ; Load Flags into AH
			and     al, 01000101B           ; Isolate  C0, C2 and C3
			and     ah,~01000101B           ; Turn off CF, PF and ZF
			or      ah,al                   ; Set new  CF, PF and ZF
			sahf                            ; Store AH into Flags
			je      .IntegerY                 ; Proceed if y = int(y)
			fstp    st1                     ; Set new stack top and pop
			fldz                            ; Set result to zero
			fstp    st1                     ; Set new stack top and pop
			mov     eax,1                   ; Set domain error (EDOM)
			jmp     .SetErrno                ; End of case

		.IntegerY:
			fist    dword [ebp-12]          ; Store y as integer
            and     dword [ebp-12],1        ; Set bit if y is odd
            fxch                            ; Put x on top of stack
            fabs                            ; x = |x|

		.PositiveXY:
			fldln2                          ; Load log base e of 2
            fxch    st1                     ; Exchange st0, st1
            fyl2x                           ; Compute the natural log(x)
            fmul                            ; Compute y * ln(x)
            fldl2e                          ; Load log base 2(e)
            fmulp   st1,st0                 ; Multiply x * log base 2(e)
            fst     st1                     ; Push result
            frndint                         ; Round to integer
            fsub    st1,st0                 ; Subtract
            fxch                            ; Exchange st0, st1
            f2xm1                           ; Compute 2 to the (x - 1)
            fld1                            ; Load real number 1
            fadd                            ; 2 to the x
            fscale                          ; Scale by power of 2
            fstp    st1                     ; Set new stack top and pop
            test    dword [ebp-12],1        ; Negation required ?
            jz      .NoNegate                 ; No, re-direct
            fchs                            ; Negate the result

		; No Negation
		.NoNegate:
			fstp    qword [ebp-8]           ; Save (double)pow(x, y)
            fld     qword [ebp-8]           ; Load (double)pow(x, y)
            fxam
            
			; Get the FPU status word
			fstsw   ax
            
			; Infinity set? 
			cmp     ah, 5
            jne     .Leave
            
			; Set range error (ERANGE)
			mov     eax, 2
	
	; Set error before we leave
	.SetErrno:
		push	eax						; Save eax
		call	___errno				; get errno-pointer
        mov     edi, eax                ; Store C errno variable pointer
		pop		eax						; Restore errno
        mov     dword [edi], eax        ; Set errno
		jmp		.Leave

	; Return NaN
	.SetNaN:
		fld     qword [ebp + 16]
		fldz
		faddp	st1, st0
		fld     qword [ebp + 8]
		fldz
		faddp	st1, st0
		faddp	st1, st0
		jmp		.Leave

	; Return one
	.SetOne:
		fld1
		jmp		.Leave

	; Return zero
	.SetZero:
		fldz

	; Restore registers and leave
	.Leave:
		pop     eax                     ; Restore register eax
		pop		ecx						; Restore register ecx
        pop     edi                     ; Restore register edi
        
		; Deallocate temporary space & leave
		mov     esp, ebp
        pop     ebp
        ret

; Msvc version of pow
__CIpow:
    sub     esp,16                  ; Allocate stack space for args
    fstp    qword [esp+8]           ; Copy y onto stack
    fstp    qword [esp]             ; Copy x onto stack
    call    _pow                    ; Call pow
    add     esp,16                  ; Remove args from stack
    ret
