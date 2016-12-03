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
; MollenOS x86-32 Math LOG2

bits 32
segment .text

;Functions in this asm
global _log2

; double __cdecl log2(double)
; Take the log value 2
_log2:
    ; Stack Frame
	push    ebp
	mov     ebp, esp

	; Push 1 to stack 
	fld1
	fld		qword [ebp + 8]
	fxam
	fnstsw	ax

	;  x : x : 1
	fld		st0
	sahf

	; in case x is NaN or ±Inf
	jc		t3
t4:	
	;  x-1 : x : 1
	fsub	st0, st2

	; x-1 : x-1 : x : 1
	fld		st0

	; |x-1| : x-1 : x : 1
	fabs

	; x-1 : x : 1
	fcomp

	; x-1 : x : 1
	fnstsw	ax
	and		ah, 0x45
	jz		t2

	; x-1 : 1
	fstp	st1

	; log(x)
	fyl2xp1
	
	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret

t2:	
	; x : 1
	fstp	st0

	; log(x)
	fyl2x
	
	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret

t3:	
	; in case x is ±Inf
	jp		t4
	fstp	st1
	fstp	st1
	
	; Unwind & return
	mov     esp, ebp
	pop     ebp
	ret