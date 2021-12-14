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
global _memory_get_cr3
global _memory_load_cr3
global _memory_invalidate_addr
global _memory_paging_init

; void memory_set_paging([esp + 4] int enable)
; If enable is non-zero it will enable paging for the core that is calling. Otherwise
; paging will be disabled.
_memory_set_paging:
	; Get [enable]
	mov	eax, dword [esp + 4]
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
		ret

; void memory_reload_cr3(void)
; Reads and writes back the CR3 register to initiate a MMU reload
_memory_reload_cr3:
	mov eax, cr3
	mov cr3, eax
    ret

; paddr_t memory_get_cr3(void)
; Reads the contents of the CR3 register and returns it
_memory_get_cr3:
	mov eax, cr3
	ret

; void memory_load_cr3([esp + 4] paddr_t pda)
; Loads the cr3 register with the provided physical address to a PD structure
_memory_load_cr3:
	mov	eax, dword [esp + 4]
	mov cr3, eax
	ret 

; void memory_invalidate_addr([esp + 4] vaddr_t address)
; Invalidates the provided memory address (virtual)
_memory_invalidate_addr:
    mov eax, [esp + 4]
	invlpg [eax]
	ret

; void memory_paging_init([esp + 4] paddr_t pda, [esp + 8] paddr_t stackPhysicalBase, [esp + 12] vaddr_t stackVirtualBase)
; Switches to the new paging table and readjusts stack from it's physical address to virtual
; This needs to be called once we setup the new paging mappings.
_memory_paging_init:
    ; load the cr3 first
    mov eax, dword [esp + 4]
    mov cr3, eax

    ; fixup the stack
    sub esp, dword [esp + 8]
    add esp, dword [esp + 12]

    ; enable paging
    mov eax, cr0
    or eax, 0x80000000		; Set bit 31
    mov	cr0, eax
    ret
