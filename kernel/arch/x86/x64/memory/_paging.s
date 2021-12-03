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
global memory_paging_init

; void memory_reload_cr3(void)
; Reads and writes back the CR3 register to initiate a MMU reload
memory_reload_cr3:
	mov rax, cr3
	mov cr3, rax
	ret 

; paddr_t memory_get_cr3(void)
; Reads the contents of the CR3 register and returns it
memory_get_cr3:
	mov rax, cr3
	ret 

; void memory_load_cr3([rcx] paddr_t pda)
; Loads the cr3 register with the provided physical address to a PML4 structure
memory_load_cr3:
	mov cr3, rcx
	ret 

; void memory_invalidate_addr([rcx] vaddr_t address)
; Invalidates the provided memory address (virtual)
memory_invalidate_addr:
	invlpg [rcx]
	ret

; void memory_paging_init([rcx] paddr_t pda, [rdx] paddr_t stackPhysicalBase, [r8] vaddr_t stackVirtualBase)
; Switches to the new paging table and readjusts stack from it's physical address to virtual
; This needs to be called once we setup the new paging mappings.
memory_paging_init:
    sub rsp, rdx
    add rsp, r8
    jmp memory_load_cr3
