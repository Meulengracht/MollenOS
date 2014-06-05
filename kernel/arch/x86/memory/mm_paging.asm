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
; MollenOS x86-32 Paging Assembly Functions
;
bits 32
segment .text

;Functions in this asm
global _memory_set_paging
global _memory_reload_cr3
global _memory_load_cr3
global _memory_invalidate_addr

;void memory_set_paging(int enable)
;Either enables or disables paging
_memory_set_paging:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Get [enable]
	mov	eax, dword [ebp + 8]
	cmp eax, 0
	je	.disable

	; Enable
	mov eax, cr0
	or eax, 0x80000000		; Set bit 31
	mov	cr0, eax
	jmp .done
	
	.disable:
		mov eax, cr0
		and eax, 0x7FFFFFFF		; Clear bit 31
		mov	cr0, eax

	.done:
		; Release stack frame
		xor eax, eax
		pop ebp
		ret 


;void memory_reload_cr3(void)
;Reloads the cr3 register
_memory_reload_cr3:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Reload
	mov eax, cr3
	mov cr3, eax

	; Release stack frame
	xor eax, eax
	pop ebp
	ret 

;void _memory_load_cr3(addr_t pda)
;Loads the cr3 register
_memory_load_cr3:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Reload
	mov	eax, dword [ebp + 8]
	mov cr3, eax

	; Release stack frame
	xor eax, eax
	pop ebp
	ret 

;void _memory_invalidate_addr(addr_t pda)
;Invalidates a page address
_memory_invalidate_addr:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Reload
	invlpg [ebp + 8]

	; Release stack frame
	xor eax, eax
	pop ebp
	ret 