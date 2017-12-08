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
; MollenOS x86-32 restore context routine

bits 32
segment .text

;Functions in this asm
global ?jumpto@Registers_x86@libunwind@@QAEXXZ

?jumpto@Registers_x86@libunwind@@QAEXXZ:
	mov	eax, ecx ; mov   eax, DWORD [4+esp]
	mov	edx, DWORD [28+eax]
	sub	edx, 8
	mov	DWORD [28+eax], edx
	mov	ebx, DWORD [eax]
	mov	DWORD [edx], ebx
	mov	ebx, DWORD [40+eax]
	mov	DWORD [4+edx], ebx

	mov	ebx, DWORD [4+eax]
	mov	ecx, DWORD [8+eax]
	mov	edx, DWORD [12+eax]
	mov	edi, DWORD [16+eax]
	mov	esi, DWORD [20+eax]
	mov	ebp, DWORD [24+eax]
	mov	esp, DWORD [28+eax]

	pop	eax
    ret
