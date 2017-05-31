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
; MollenOS x86-32 Math floating point tangent

bits 32
segment .text

;Functions in this asm
global _tan
global _tanf
global __CItan

; double __cdecl tan(double x)
; floating point tangent
_tan:
	; Stack Frame
	push    ebp
	mov     ebp, esp
    
	; Load real from stack
	fld     qword [ebp+8]

	; Take the tangent
	fptan

	; Use ax for temporary storage
	fnstsw	ax
	and		ax, 0x400
	jnz		.NotZero
	
	fstp	st0
	jmp		.Leave
	
	.NotZero:
		fldpi
		fadd	st0
		fxch	st1

	.Repeat:
		fprem1
		fnstsw	ax
		and		ax, 0x400
		jnz		.Repeat
		fstp	st1
		fptan
		fstp	st0

	; Unwind & return
	.Leave:
		pop     ebp
		ret

; float __cdecl tan(float x)
; floating point tangent
_tanf:
	; Stack Frame
	push    ebp
	mov     ebp, esp
    
	; Load real from stack
	fld     dword [ebp+8]

	; Take the tangent
	fptan

	; Use ax for temporary storage
	fnstsw	ax
	and		ax, 0x400
	jnz		.NotZero
	
	fstp	st0
	jmp		.Leave
	
	.NotZero:
		fldpi
		fadd	st0
		fxch	st1

	.Repeat:
		fprem1
		fnstsw	ax
		and		ax, 0x400
		jnz		.Repeat
		fstp	st1
		fptan
		fstp	st0

	; Unwind & return
	.Leave:
		pop     ebp
		ret

; Msvc version of tan
__CItan:
	; Store eax, we trash it
	push	eax

	; Take the tangent
	fptan

	; Use ax for temporary storage
	fnstsw	ax
	and		ax, 0x400
	jnz		.NotZero
	fstp	st0
	
	; Restore and return
	pop		eax
	ret
	
	.NotZero:
		fldpi
		fadd	st0
		fxch	st1

	.Repeat:
		fprem1
		fnstsw	ax
		and		ax, 0x400
		jnz		.Repeat
		fstp	st1
		fptan
		fstp	st0

	; Restore and return
	pop		eax
	ret