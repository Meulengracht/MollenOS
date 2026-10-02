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

// x0 names the interrupted user context. Preserve every SIMD register while
// C handles the signal; SVC #2 restores all integer registers through ERET.
.text
.p2align 2
.globl __signalentry
__signalentry:
    sub sp, sp, #544
    str x0, [sp, #528]
    stp q0, q1, [sp, #0]
    stp q2, q3, [sp, #32]
    stp q4, q5, [sp, #64]
    stp q6, q7, [sp, #96]
    stp q8, q9, [sp, #128]
    stp q10, q11, [sp, #160]
    stp q12, q13, [sp, #192]
    stp q14, q15, [sp, #224]
    stp q16, q17, [sp, #256]
    stp q18, q19, [sp, #288]
    stp q20, q21, [sp, #320]
    stp q22, q23, [sp, #352]
    stp q24, q25, [sp, #384]
    stp q26, q27, [sp, #416]
    stp q28, q29, [sp, #448]
    stp q30, q31, [sp, #480]
    
    mrs x9, fpcr
    mrs x10, fpsr
    
    str x9, [sp, #512]
    str x10, [sp, #520]
    
    bl StdInvokeSignal
    
    ldp q0, q1, [sp, #0]
    ldp q2, q3, [sp, #32]
    ldp q4, q5, [sp, #64]
    ldp q6, q7, [sp, #96]
    ldp q8, q9, [sp, #128]
    ldp q10, q11, [sp, #160]
    ldp q12, q13, [sp, #192]
    ldp q14, q15, [sp, #224]
    ldp q16, q17, [sp, #256]
    ldp q18, q19, [sp, #288]
    ldp q20, q21, [sp, #320]
    ldp q22, q23, [sp, #352]
    ldp q24, q25, [sp, #384]
    ldp q26, q27, [sp, #416]
    ldp q28, q29, [sp, #448]
    ldp q30, q31, [sp, #480]
    ldr x9, [sp, #512]
    ldr x10, [sp, #520]
    
    msr fpcr, x9
    msr fpsr, x10
    
    ldr x0, [sp, #528]
    add sp, sp, #544
    svc #2
    brk #0
