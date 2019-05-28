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
; x86-64 Signal Code
;

bits 64
segment .text

global __signalentry
extern StdInvokeSignal

; void __signalentry(int signal, void* argument, reg_t r8)
; Fixup stack and call directly into the signal handler
__signalentry:
    ; Prepare stack alignment
    mov rbx, 0x20 ; this register is non-volatile, so we use this
    mov rax, rbx
    and rax, 0xF
    add rbx, rax
	sub rsp, rbx
	call StdInvokeSignal ; (int, void*)
	add rsp, rbx
	
	; Restore RCX/RDX/R8 for next call in case we are now
	; entering another queued up signal handler
	pop r8
	pop rdx
	pop rcx
    ret
