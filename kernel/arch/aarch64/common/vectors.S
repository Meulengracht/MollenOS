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

// All vector slots are 128 bytes, and VBAR requires 2 KiB alignment.
.text
.p2align 11
.globl Arm64Vectors
Arm64Vectors:
    b .Lfatal
    .space 124
    b .Lfatal
    .space 124
    b .Lfatal
    .space 124
    b .Lfatal
    .space 124
    b .Lsync
    .space 124
    b .Lirq
    .space 124
    b .Lfatal
    .space 124
    b .Lfatal
    .space 124
    b .Lsync
    .space 124
    b .Lirq
    .space 124
    b .Lfatal
    .space 124
    b .Lfatal
    .space 124
    b .Lfatal
    .space 124
    b .Lfatal
    .space 124
    b .Lfatal
    .space 124
    b .Lfatal
    .space 124

.macro save_frame
    sub sp, sp, #304
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
    str x30, [sp, #240]
    add x0, sp, #304
    str x0, [sp, #248]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #256]
    mrs x0, sp_el0
    str x0, [sp, #272]
    str xzr, [sp, #280]
    mrs x0, far_el1
    mrs x1, esr_el1
    stp x0, x1, [sp, #288]
    mov x0, sp
.endm

.Lsync:
    save_frame
    bl Arm64HandleException
    b Arm64RestoreContext
.Lirq:
    save_frame
    bl Arm64HandleInterrupt
    b Arm64RestoreContext
.Lfatal:
    msr daifset, #15
    wfe
    b .Lfatal

.globl Arm64RestoreContext
Arm64RestoreContext:
    mov x30, x0
    ldp x0, x1, [x30, #256]
    msr elr_el1, x0
    msr spsr_el1, x1
    ldr x0, [x30, #272]
    msr sp_el0, x0
    ldr x0, [x30, #248]
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
    ldr x30, [x30, #240]
    eret
