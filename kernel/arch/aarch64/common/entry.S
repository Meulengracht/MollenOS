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

// The native loader has established EL1h, DAIF masking and a reserved stack.
// Install vectors before C, then create Normal-memory mappings before atomics.
.text
.p2align 2
.globl kentry
kentry:
    // Keep all asynchronous exceptions masked until vectors and per-core state exist.
    msr daifset, #ARM64_DAIF_ALL
    adrp x1, Arm64Vectors
    add x1, x1, :lo12:Arm64Vectors
    msr vbar_el1, x1
    // FPEN grants FP/SIMD access; TPIDR_EL0 starts without a userspace TLS anchor.
    mov x1, #ARM64_CPACR_FP_ENABLE
    msr cpacr_el1, x1
    msr tpidr_el0, xzr
    isb

    mov x19, x0
    bl Arm64BootMemoryInitialize
    
    cbnz w0, .Lboot_stop
    mov w0, wzr
    bl Arm64InitializeLocal
    
    mov x0, x19
    bl InitializeMachine

.Lboot_stop:
    msr daifset, #ARM64_DAIF_ALL
    wfe
    b .Lboot_stop
