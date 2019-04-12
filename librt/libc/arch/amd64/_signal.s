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
    push qword [%1 + 3*8]
    push 0
    push 0
    
    ; segments
    push 0
    push 0
    push 0
    push 0

	; registers
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push qword [%1]

    push rax
    push qword [%1 + 1*8]
    push qword [%1 + 2*8]
    push rbx
    push rsp
    push rbp
    push rsi
    push rdi
%endmacro

%macro restore_state 0
    ; registers
    pop rdi
    pop rsi
    pop rbp
    add rsp, 8 ; skip rsp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
	
    ; segments (4*8), irq (5*8), user (2*8), fillers (5*8)
    add rsp, 16*8
%endmacro

; void __signalentry(int signal, int unused, uintptr_t stack_ptr)
; Fixup stack and call directly into the signal handler
__signalentry:
    ; switch to safe stack if one is provided
    mov  rdx, rsp
    test r8, r8
    jz .Invoke
    xchg r8, rsp
    mov  rdx, r8
    
    .Invoke:
        ; Prepare the context_t*
        save_state rdx
        mov rdx, rsp
        
        ; Prepare stack alignment
        mov rbx, 0x20 ; this register is non-volatile, so we use this
        mov rax, rbx
        and rax, 0xF
        add rbx, rax
    	sub rsp, rbx
    	call StdInvokeSignal ; (int, context_t*)
    	add rsp, rbx
    	restore_state
    	
    	; R8, RCX and RDX is pushed to stack, remove them again as
    	; they have been restored correctly by restore_state
    	add rsp, 24
        ret
