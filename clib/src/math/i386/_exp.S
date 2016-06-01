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

; double __cdecl exp(double x)
; exponential
_exp:
	; Stack Frame
	push    ebp
	mov     ebp, esp

    fld		qword [ebp + 8]
    fxam
    fstsw	ax
    fwait
    sahf
    jnp		.not_inf
    jnc		.not_inf
    test	ah, 2
    jz		.done
    fstp	st0
    fldz
    jmp		.done
.not_inf:
    fldl2e
    fmul	st0, st1
    fst		st1
    frndint
    fxch	st1
    fsub	st0, st1
    f2xm1
    fld1
    faddp	st1, st0
    fscale
    fstp	st1
.done:
	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret

