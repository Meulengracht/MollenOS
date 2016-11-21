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
; MollenOS x86-32 Math EXP

bits 32
segment .text

;Functions in this asm
global _exp
global __CIexp

; double __cdecl exp(double x)
; e^x = 2^(x * log2(e))
_exp:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; If x is +-Inf, then the subtraction would give Inf-Inf = NaN.
	; Avoid this.  Also avoid it if x is NaN for convenience.
	mov		eax, dword [ebp + 12]
	and		eax, 0x7FFFFFFF
	cmp		eax, 0x7FF00000
	jae		.IsInf

	; Load 64 bit 
    fld		qword [ebp + 8]
    
	; Extended precision is needed to reduce the maximum error from
	; hundreds of ulps to less than 1 ulp.  Switch to it if necessary.
	; We may as well set the rounding mode to to-nearest and mask traps
	; if we switch.
	fstcw	word [ebp + 8]
	mov		eax, dword [ebp + 8]
	and		eax, 0x00000300
	cmp		eax, 0x00000300
	je		.Start
	mov		dword [ebp + 12], 0x0000137F
	fldcw	word [ebp + 12]

	.Start:
		fldl2e
		fmulp		st1, st0 ; x * log2(e)
		fst			st1
		frndint				 ; int(x * log2(e))
		fsub		st1, st0 
		fxch
		f2xm1				 ; 2^(fract(x * log2(e))) - 1 
		fld1
		fadd
		fscale				 ; e^x 
		fstp		st1
		je		.Leave
		fldcw	word [ebp + 8]
		jmp		.Leave

	; Return 0 if x is -Inf.  Otherwise just return x; when x is Inf
	; this gives Inf, and when x is a NaN this gives the same result
	; as (x + x) (x quieted).
	.IsInf:
		cmp		dword [ebp + 12], 0xFFF00000
		jne		.PlusInf
		cmp		dword [ebp + 8], 0
		jne		.PlusInf
		fldz
		jmp		.Leave

	.PlusInf:
		fld		qword [ebp + 8]

	.Leave:
		; Unwind & return
		pop     ebp
		ret

; Msvc version of exp
__CIexp:
	; Stack Frame
	push    ebp
	mov     ebp, esp
	sub		esp, 8

	fstp	qword [esp]
	call	_exp

	; Restore and leave
	mov		esp, ebp
	pop		ebp
	ret