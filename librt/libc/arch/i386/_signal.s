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

%macro save_state 1
    ; fillers
    push 0
    push 0
    push 0
    push 0
    push 0

    ; user-state
    push %1
    push 0
    
    ; irq state
    push 0
    push 0
    push dword [%1 + 4*4]
    push 0
    push 0
    
    ; segments
    push 0
    push 0
    push 0
    push 0

	; registers
    push dword [%1 + 3*4] ;eax
    push dword [%1 + 1*4] ;ecx
    push dword [%1]       ;edx
    push dword [%1 + 2*4] ;ebx
    push esp
    push ebp
    push esi
    push edi
%endmacro

%macro restore_state 0
    ; registers
    pop edi
    pop esi
    pop ebp
    add esp, 4 ; skip esp
    pop ebx
    pop edx
    pop ecx
    pop eax

    ; segments (4*4), irq (5*4), user (2*4), fillers (5*4)
    add esp, 16*4
%endmacro

; void __signalentry(int signal (eax), int unused (ebx), uintptr_t stack_ptr (ecx))
; Fixup stack and call directly into the signal handler
___signalentry:
    ; switch to safe stack if one is provided
    mov  ebx, esp
    test ecx, ecx
    jz .Invoke
    xchg ecx, esp
    mov  ebx, ecx
    
    .Invoke:
        ; Prepare the context_t*
        save_state ebx
        mov ebx, esp
        
        ; EAX => signal, EBX => context_t
        push ebx
        push eax
    	call _StdInvokeSignal ; (int, context_t*)
    	add esp, 8
    	restore_state
    	
    	; EAX, EBX, ECX and EDX is pushed to stack, remove them again as
    	; they have been restored correctly by restore_state
    	add esp, 16
        ret
