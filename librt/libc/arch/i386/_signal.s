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
; x86-32 Signal Code
;

bits 32
segment .text

global ___signalentry
extern _StdInvokeSignal

; void __signalentry(context_t* context (rcx), int signal (rdx), void* argument (r8), reg_t flags (r9))
; Fixup stack and call directly into the signal handler
___signalentry:
	; Store the return next stack we should use before calling the signal
	; handler in a non-volatile register.
	xchg bx, bx
	
    ; EAX => context, EBX => signal, ECX => void*, EDX => unsigned int
    push eax
    push ebx
    push ecx
    push edx
	call _StdInvokeSignal ; (context_t*, int, void*, unsigned int)
    add esp, 16
    
    ; Restore initial state and switch the handler stack to next one stored
    popad
    
    ; get the user-esp from the stack, it will be offset 9
    mov esp, [esp + (9 * 4)]
    ret
