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
; MollenOS x86-64 Thread Assembly Functions
;
bits 64
segment .text

;Functions in this asm
global init_sse
global init_fpu
global save_fpu
global save_fpu_extended
global load_fpu
global load_fpu_extended
global clear_ts
global set_ts
global _rdtsc
global _rdmsr
global _yield
global enter_thread

%macro restore_segments 0
    pop gs
    pop fs
    mov es, qword [rsp]
    mov ds, qword [rsp + 8]
    add rsp, 16
%endmacro

%macro restore_state 0
    ; Restore registers
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
    
    restore_segments
%endmacro

; void _yield(void)
; Yields
_yield:
    int 0x61
    ret 

; void save_fpu(uintptr_t *buffer)
; Save FPU, MMX and SSE registers
save_fpu:
    fxsave [rcx]
    ret

; void save_fpu_extended(uintptr_t *buffer)
; Save FPU, MMX, SSE, AVX extended registers
save_fpu_extended:
    mov rax, 0xFFFFFFFFFFFFFFFF
    mov rdx, 0xFFFFFFFFFFFFFFFF
    xsave [rcx]
    ret

; void load_fpu(uintptr_t *buffer)
; Load FPU, MMX and SSE registers
load_fpu:
    fxrstor [rcx]
    ret

; void load_fpu_extended(uintptr_t *buffer)
; Load FPU, MMX, SSE, AVX extended registers
load_fpu_extended:
    mov rax, 0xFFFFFFFFFFFFFFFF
    mov rdx, 0xFFFFFFFFFFFFFFFF
    xrstor [rcx]
    ret

; void set_ts()
; Sets the Task-Switch register
set_ts:
    push rax
    mov rax, cr0
    bts rax, 3
    mov cr0, rax
    pop rax
    ret 

; void clear_ts()
; Clears the Task-Switch register
clear_ts:
    clts
    ret 

; void init_fpu()
; Initializes the FPU
init_fpu:
    finit
    ret

; void _rdtsc(uint64_t *value)
; Gets the CPU time-stamp counter
_rdtsc:
    rdtsc
    shl rdx, 32
    or rax, rdx
    mov qword [rcx], rax
    ret

; void _rdmsr(size_t Register, uint64_t *value)
; Gets the CPU model specific register
_rdmsr:
    push rdx
    rdmsr
    shl rdx, 32
    or rax, rdx
    pop rdx
    mov qword [rdx], rax
    ret

; void enter_thread(registers_t *stack)
; Switches stack and far jumps to next task
enter_thread:
    mov rsp, rcx
    restore_state

    ; Cleanup irq & error code from stack
    add rsp, 0x10
    iretq
