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
; MollenOS x86-32 Math SINUS

bits 32
segment .text

;Functions in this asm
global _sin
global __CIsin

; computes the sinus value
_sin:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Load real from stack
	fld     qword [ebp + 8]
	fsin

	; Do some validation
	fnstsw	ax
	and		ax, 0x0400

	jnz		.Try1
	jmp		.Leave

	.Try1:
		fldpi
		fadd	st0
		fxch	st1

	.Try2:
		fprem1
		fnstsw	ax
		and		ax, 0x0400
		jnz		.Try2
		fstp	st1
		fsin
	
	.Leave:
		; Unwind & return
		pop     ebp
		ret

; Msvc version of sin
__CIsin:
	; We trash eax
	push	eax

	; Do the fsin
	fsin

	; Do some validation
	fnstsw	ax
	and		ax, 0x0400

	jnz		.Try1
	jmp		.Leave

	.Try1:
		fldpi
		fadd	st0
		fxch	st1

	.Try2:
		fprem1
		fnstsw	ax
		and		ax, 0x0400
		jnz		.Try2
		fstp	st1
		fsin
	
	.Leave:
		; Unwind & return
		pop     eax
		ret