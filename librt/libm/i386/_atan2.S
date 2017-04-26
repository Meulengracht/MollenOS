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
; MollenOS x86-32 Math ATAN2

bits 32
segment .text

;Functions in this asm
global _atan2
global __CIatan2

; double __cdecl atan2(double y, double x)
_atan2:
	; Stack Frame
	push    ebp
	mov     ebp, esp

    fld		qword [ebp + 8]
    fld		qword [ebp + 16]
    fpatan

	; Unwind & return
	pop     ebp
	ret

; Msvc version of atan2
__CIatan2:
	fpatan
	ret