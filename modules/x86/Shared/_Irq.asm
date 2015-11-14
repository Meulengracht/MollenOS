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
; MollenOS x86-32 Irq Assembly Functions
;
bits 32
segment .text

;Functions in this asm
global ___cli
global ___sti
global ___hlt
global ___getflags
global ___getcr2

; void __cli(void)
; Disables interrupts
___cli:
	; Disable interrupts
	cli

	; Return
	ret 

; void __sti(void)
; Enables interrupts
___sti:
	; Enable interrupts
	sti

	; Return
	ret 

; void __hlt(void)
; Enables interrupts
___hlt:
	; Idle
	hlt

	; Return
	ret 

; uint32_t __getflags(void)
; Gets Eflags
___getflags:
	; Get flags
	pushfd
	pop eax

	; Return
	ret 

; uint32_t __getcr2(void)
; Gets CR2 register
___getcr2:
	; Get cr2
	mov eax, cr2

	; Return
	ret 
