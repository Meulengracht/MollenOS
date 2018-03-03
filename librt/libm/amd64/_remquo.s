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
; MollenOS x86-64 math remquo

bits 64
segment .text

;Functions in this asm
global remquo
global remquof
global remquol

; takes the remainder quotient
remquo:
	; Load x and y from stack
	movsd	qword [rsp - 8], xmm0
	movsd 	qword [rsp - 16], xmm1
    fld     qword [rsp - 16]
    fld     qword [rsp - 8]

    ; Extract the remainder
	.TryAgain:
		fprem1
		fstsw	ax
		bt		ax, 10
		jc		.TryAgain
		fstp	st1

	; Extract the three low-order bits of the quotient from C0, C3, C1
	shr 	eax, 6
	mov 	ecx, eax
	and 	eax, 0x108
	ror 	eax, 7
	or 		ecx, eax
	rol 	eax, 4
	or 		eax, ecx
	and 	eax, 7

	; Negate the quotient bits if x * y < 0. Avoid using an unpreditable branch
	mov 	ecx, dword [rsp - 12]
	xor 	ecx, dword [rsp - 4]
	sar 	ecx, 16
	sar 	ecx, 16
	xor 	eax, ecx
	and 	ecx, 1
	add 	eax, ecx

	; Store result
%ifdef _MICROSOFT_LIBM
	mov 	dword [r8], eax
%else
	mov 	dword [rdi], eax
%endif
	fstp	qword [rsp - 8]
	movsd	xmm0, qword [rsp - 8]
	ret

; takes the remainder quotient
remquof:
	; Load x and y from stack
	movss	dword [rsp - 4], xmm0
	movss 	dword [rsp - 8], xmm1
    fld     dword [rsp - 8]
    fld     dword [rsp - 4]

    ; Extract the remainder
	.TryAgain:
		fprem1
		fstsw	ax
		bt		ax, 10
		jc		.TryAgain
		fstp	st1

	; Extract the three low-order bits of the quotient from C0, C3, C1
	shr 	eax, 6
	mov 	ecx, eax
	and 	eax, 0x108
	ror 	eax, 7
	or 		ecx, eax
	rol 	eax, 4
	or 		eax, ecx
	and 	eax, 7

	; Negate the quotient bits if x * y < 0. Avoid using an unpreditable branch
	mov 	ecx, dword [rsp - 8]
	xor 	ecx, dword [rsp - 4]
	sar 	ecx, 16
	sar 	ecx, 16
	xor 	eax, ecx
	and 	ecx, 1
	add 	eax, ecx

	; Store result
%ifdef _MICROSOFT_LIBM
	mov 	dword [r8], eax
%else
	mov 	dword [rdi], eax
%endif
	fstp	dword [rsp - 4]
	movss	xmm0, dword [rsp - 4]
	ret

; takes the remainder quotient
remquol:
	; Load x and y from stack
%ifdef _MICROSOFT_LIBM
	fld 	tword [r8]
	fld		tword [rdx]
	mov 	r8, rcx
%else
	fld 	tword [rsp + 24]
	fld 	tword [rsp + 8]
%endif

    ; Extract the remainder
	.TryAgain:
		fprem1
		fstsw	ax
		bt		ax, 10
		jc		.TryAgain
		fstp	st1

	; Extract the three low-order bits of the quotient from C0, C3, C1
	shr 	eax, 6
	mov 	ecx, eax
	and 	eax, 0x108
	ror 	eax, 7
	or 		ecx, eax
	rol 	eax, 4
	or 		eax, ecx
	and 	eax, 7

	; Negate the quotient bits if x * y < 0. Avoid using an unpreditable branch
	mov 	ecx, dword [rsp + 32]
	xor 	ecx, dword [rsp + 16]
	movsx 	ecx, cx
	sar 	ecx, 16
	sar 	ecx, 16
	xor 	eax, ecx
	and 	ecx, 1
	add 	eax, ecx

	; Store result
%ifdef _MICROSOFT_LIBM
	mov 	dword [r9], eax
	mov 	rax, r8
	mov 	qword [r8 + 8], 0
	fstp	tword [r8]
%else
	mov 	dword [rdi], eax
%endif
	ret