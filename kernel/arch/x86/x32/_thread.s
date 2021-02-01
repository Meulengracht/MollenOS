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
; MollenOS x86-32 Descriptor Assembly Functions
;
bits 32
segment .text

;Functions in this asm
global _init_fpu
global _save_fpu
global _save_fpu_extended
global _load_fpu
global _load_fpu_extended
global _clear_ts
global _set_ts
global __rdtsc
global __rdmsr
global __wrmsr
global __yield

; void _yield(void)
; Yields
__yield:
	int 0x61
	ret 

; void save_fpu(uintptr_t *buffer)
; Save FPU, MMX and SSE registers
_save_fpu:
	mov eax, [esp + 4]
	fxsave [eax]
	ret

; void save_fpu_extended(uintptr_t *buffer)
; Save FPU, MMX, SSE, AVX extended registers
_save_fpu_extended:
	mov eax, 0xFFFFFFFF
	mov edx, 0xFFFFFFFF
	mov ecx, [esp + 4]
	xsave [ecx]
	ret

; void load_fpu(uintptr_t *buffer)
; Load FPU, MMX and SSE registers
_load_fpu:
	mov eax, [esp + 4]
	fxrstor [eax]
	ret

; void load_fpu_extended(uintptr_t *buffer)
; Load MMX and MMX registers
_load_fpu_extended:
	mov eax, 0xFFFFFFFF
	mov edx, 0xFFFFFFFF
	mov ecx, [esp + 4]
	xrstor [ecx]
	ret

; void set_ts()
; Sets the Task-Switch register
_set_ts:
	push eax
	mov eax, cr0
	bts eax, 3
	mov cr0, eax
	pop eax
	ret 

; void clear_ts()
; Clears the Task-Switch register
_clear_ts:
	clts
	ret 

; void init_fpu()
; Initializes the FPU
_init_fpu:
	finit
	ret

; void _rdtsc(uint64_t *value)
; Gets the CPU time-stamp counter
__rdtsc:
	mov ecx, [esp + 4]
	rdtsc
	mov [ecx], eax
	mov [ecx + 4], edx
	ret

; void _rdmsr(size_t Register, uint64_t *value)
; Gets the CPU model specific register
__rdmsr:
    mov ecx, [esp + 4]
	rdmsr
	mov ecx, [esp + 8]
	mov [ecx], eax
	mov [ecx + 4], edx
	ret

; void _wrmsr(size_t Register, uint64_t *value)
; Gets the CPU model specific register
__wrmsr:
    push ebx
    mov ecx, [esp + 4]
    mov ebx, [esp + 8]
	mov eax, [ebx]
	mov edx, [ebx + 4]
	wrmsr
	pop ebx
	ret
