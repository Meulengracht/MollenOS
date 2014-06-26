; MollenOS
;
; Copyright 2011-2014, Philip Meulengracht
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
; MollenOS x86-32 Descriptor Assembly Functions
;
bits 32
segment .text

;Functions in this asm
global _init_fpu
global _save_fpu
global _load_fpu
global _clear_ts
global _set_ts
global __yield

; void _yield(void)
; Yields
__yield:
	; Stack Frame
	push ebp
	mov ebp, esp

	; call int 0x81
	int 0x81

	; Release stack frame
	xor eax, eax
	pop ebp
	ret 

; void save_fpu(addr_t *buffer)
; Save MMX and MMX registers
_save_fpu:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Save FPU to argument 1
	mov eax, [ebp + 8]
	fxsave [eax]

	; Release stack frame
	xor eax, eax
	pop ebp
	ret 

; void load_fpu(addr_t *buffer)
; Load MMX and MMX registers
_load_fpu:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Save FPU to argument 1
	mov eax, [ebp + 8]
	fxrstor [eax]

	; Release stack frame
	xor eax, eax
	pop ebp
	ret 

; void set_ts()
; Sets the Task-Switch register
_set_ts:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Set TS
	mov eax, cr0
	bts eax, 3
	mov cr0, eax

	; Release stack frame
	xor eax, eax
	pop ebp
	ret 

; void clear_ts()
; Clears the Task-Switch register
_clear_ts:
	; Stack Frame
	push ebp
	mov ebp, esp

	; clear
	clts

	; Release stack frame
	xor eax, eax
	pop ebp
	ret 

; void init_fpu()
; Clears the Task-Switch register
_init_fpu:
	; Stack Frame
	push ebp
	mov ebp, esp

	; fpu init
	finit

	; Release stack frame
	xor eax, eax
	pop ebp
	ret 