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
; MollenOS x86-32 Math LOGB

bits 32
segment .text

;Functions in this asm
global _logb
global _logbf
global _logbl

; takes the logorithmic value
_logb:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Do the magic
	fld     qword [ebp + 8]       ; Load real from stack
	fxtract

	; Store
	fstp	st0

	; Unwind & return
	pop     ebp
	ret

; takes the logorithmic value
_logbf:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Do the magic
	fld     dword [ebp + 8]       ; Load single from stack
	fxtract

	; Store
	fstp	st0

	; Unwind & return
	pop     ebp
	ret

; takes the logorithmic value
_logbl:
	; Stack Frame
	push    ebp
	mov     ebp, esp

	; Do the magic
	fld     tword [ebp + 8]       ; Load 80bit from stack
	fxtract

	; Store
	fstp	st0

	; Unwind & return
	pop     ebp
	ret