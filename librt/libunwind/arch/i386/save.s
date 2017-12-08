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
; MollenOS x86-32 save context routine

bits 32
segment .text

;Functions in this asm
global _unw_getcontext

_unw_getcontext:
	push    eax
	mov	    eax, DWORD [8+esp]
	mov	    DWORD [4+eax],  ebx
	mov	    DWORD [8+eax],  ecx
	mov	    DWORD [12+eax], edx
	mov	    DWORD [16+eax], edi
	mov	    DWORD [20+eax], esi
	mov	    DWORD [24+eax], ebp
	mov	    edx, esp
	add	    edx, 8
	mov	    DWORD [28+eax], edx

	mov	    edx, DWORD [4+esp]
	mov	    DWORD [40+eax], edx

	mov	    edx, DWORD [esp]
	mov	    DWORD [eax], edx
	pop	    eax
	xor	    eax, eax ; UNW_ESUCCESS
    ret