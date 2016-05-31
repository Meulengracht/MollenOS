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
; MollenOS x86-32 Math EXP2

bits 32
segment .text

;Functions in this asm
global _exp2f

; float __cdecl exp2f(float)
; Take the exponential exp 2
_exp2f:
    ; Stack Frame
	push    ebp
	mov     ebp, esp

	fld		qword [ebp + 8]

	; Is NaN or +-Inf?
	fxam
	fstsw	ax
	fwait
	mov		dh, 0x45
	and		dh, ah
	cmp		dh, 0x05

	; Is +-Inf, jump.
	je		t1
	fld		st0

	; int(x)
	frndint

	; fract(x)
	fsubr	st1, st0
	fxch

	; 2^(fract(x)) - 1
	f2xm1
	fld1

	;  2^(fract(x))
	faddp

	; e^x
	fscale
	fstp	st1
	
	; Unwind & return
	pop     ebp
	ret
	
t1:
	; Test sign.
	test	eax, 0x200

	; If positive, jump.
	jz		t2
	fstp	st0

	; Set result to 0.
	fldz
t2:
	; Unwind & return
	pop     ebp
	ret