/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "abi.h"

// All vector slots are 128 bytes, and VBAR requires 2 KiB alignment.
.text
.p2align ARM64_VECTOR_ALIGN
.globl Arm64Vectors
Arm64Vectors:
    b .Lfatal
    .space ARM64_VECTOR_PADDING
    b .Lfatal
    .space ARM64_VECTOR_PADDING
    b .Lfatal
    .space ARM64_VECTOR_PADDING
    b .Lfatal
    .space ARM64_VECTOR_PADDING
    b .Lsync
    .space ARM64_VECTOR_PADDING
    b .Lirq
    .space ARM64_VECTOR_PADDING
    b .Lfatal
    .space ARM64_VECTOR_PADDING
    b .Lfatal
    .space ARM64_VECTOR_PADDING
    b .Lsync
    .space ARM64_VECTOR_PADDING
    b .Lirq
    .space ARM64_VECTOR_PADDING
    b .Lfatal
    .space ARM64_VECTOR_PADDING
    b .Lfatal
    .space ARM64_VECTOR_PADDING
    b .Lfatal
    .space ARM64_VECTOR_PADDING
    b .Lfatal
    .space ARM64_VECTOR_PADDING
    b .Lfatal
    .space ARM64_VECTOR_PADDING
    b .Lfatal
    .space ARM64_VECTOR_PADDING

// Each saved general register occupies eight bytes. Capture ELR/SPSR for
// ERET, SP_EL0 for user return, and FAR/ESR for C exception classification.
.macro save_frame
    sub sp, sp, #ARM64_CONTEXT_SIZE
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #ARM64_CONTEXT_LR]
    add x0, sp, #ARM64_CONTEXT_SIZE
    str x0, [sp, #ARM64_CONTEXT_SP]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #ARM64_CONTEXT_PC]
    mrs x0, sp_el0
    str x0, [sp, #ARM64_CONTEXT_USER_SP]
    str xzr, [sp, #ARM64_CONTEXT_RESERVED]
    mrs x0, far_el1
    mrs x1, esr_el1
    stp x0, x1, [sp, #ARM64_CONTEXT_FAULT]
    mov x0, sp
.endm

.Lsync:
    save_frame
    // Blocking syscalls and page faults retain their thread stack. Only an
    // EL1 SVC #1 yields through the scheduler and can retire that stack.
    ldr x1, [x0, #ARM64_CONTEXT_SYNDROME]
    lsr x2, x1, #ARM64_ESR_CLASS_SHIFT
    cmp x2, #ARM64_EC_SVC
    b.ne .Ldispatch_sync
    uxth w1, w1
    cmp w1, #ARM64_SVC_YIELD
    b.ne .Ldispatch_sync
    ldr x1, [x0, #ARM64_CONTEXT_PSTATE]
    and x1, x1, #ARM64_PSTATE_MODE_MASK
    cbz x1, .Ldispatch_sync
    mrs x1, tpidr_el1
    ldr x1, [x1, #ARM64_LOCAL_INTERRUPT_STACK]
    mov sp, x1
.Ldispatch_sync:
    bl Arm64HandleException
    b Arm64RestoreContext
.Lirq:
    save_frame
    // Nested IRQ dispatch is disabled. The saved frame belongs to the
    // interrupted thread, but dispatch must survive destruction of it.
    mrs x1, tpidr_el1
    ldr x1, [x1, #ARM64_LOCAL_INTERRUPT_STACK]
    mov sp, x1
    bl Arm64HandleInterrupt
    b Arm64RestoreContext
.Lfatal:
    msr daifset, #ARM64_DAIF_ALL
    wfe
    b .Lfatal

.globl Arm64RestoreContext
Arm64RestoreContext:
    // Restore the selected thread's return state before loading its registers;
    // x30 holds the frame address until its own saved LR is loaded last.
    mov x30, x0
    ldp x0, x1, [x30, #ARM64_CONTEXT_PC]
    msr elr_el1, x0
    msr spsr_el1, x1
    ldr x0, [x30, #ARM64_CONTEXT_USER_SP]
    msr sp_el0, x0
    ldr x0, [x30, #ARM64_CONTEXT_SP]
    mov sp, x0
    ldp x0, x1, [x30, #0]
    ldp x2, x3, [x30, #16]
    ldp x4, x5, [x30, #32]
    ldp x6, x7, [x30, #48]
    ldp x8, x9, [x30, #64]
    ldp x10, x11, [x30, #80]
    ldp x12, x13, [x30, #96]
    ldp x14, x15, [x30, #112]
    ldp x16, x17, [x30, #128]
    ldp x18, x19, [x30, #144]
    ldp x20, x21, [x30, #160]
    ldp x22, x23, [x30, #176]
    ldp x24, x25, [x30, #192]
    ldp x26, x27, [x30, #208]
    ldp x28, x29, [x30, #224]
    ldr x30, [x30, #ARM64_CONTEXT_LR]
    eret
