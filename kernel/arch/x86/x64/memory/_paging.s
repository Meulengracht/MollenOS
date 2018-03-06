; MollenOS
;
; Copyright 2011, Philip Meulengracht
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
; MollenOS x86-64 Paging Assembly Functions
;
bits 64
segment .text

;Functions in this asm
global memory_reload_cr3
global memory_get_cr3
global memory_load_cr3
global memory_invalidate_addr

;void memory_reload_cr3(void)
;Reloads the cr3 register
memory_reload_cr3:
	mov rax, cr3
	mov cr3, rax
	ret 

;uint32_t memory_get_cr3(void)
;Returns the cr3 register
memory_get_cr3:
	mov rax, cr3
	ret 

;void _memory_load_cr3(uintptr_t pda)
;Loads the cr3 register
memory_load_cr3:
	mov cr3, rcx
	ret 

;void _memory_invalidate_addr(uintptr_t pda)
;Invalidates a page address
memory_invalidate_addr:
	invlpg [rcx]
	ret