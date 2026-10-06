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

#ifndef __VALI_AARCH64_ABI_H__
#define __VALI_AARCH64_ABI_H__

// Shared C/assembly constants must not use C types or integer suffixes.
// Exception frames contain x0-x30 followed by the saved stack/system state.
#define ARM64_CONTEXT_SIZE        304
#define ARM64_CONTEXT_LR          240
#define ARM64_CONTEXT_SP          248
#define ARM64_CONTEXT_PC          256
#define ARM64_CONTEXT_PSTATE      264
#define ARM64_CONTEXT_USER_SP     272
#define ARM64_CONTEXT_RESERVED    280
#define ARM64_CONTEXT_FAULT       288
#define ARM64_CONTEXT_SYNDROME    296

// TPIDR_EL1 addresses Arm64CpuLocal; this field supplies the IRQ dispatch stack.
#define ARM64_LOCAL_INTERRUPT_STACK 152

// FP storage holds 32 128-bit SIMD registers, then 64-bit FPCR/FPSR slots.
#define ARM64_FP_CONTROL          512
#define ARM64_FP_STATUS           520
#define ARM64_FP_STATE_SIZE       528

// The secondary descriptor carries affinity, stack, translation controls, ID,
// and a publication flag. C assertions check the layout consumed at startup.
#define ARM64_SECONDARY_STATE_SIZE 64
#define ARM64_SECONDARY_STACK      8
#define ARM64_SECONDARY_TABLES     16
#define ARM64_SECONDARY_CONTROLS   32
#define ARM64_SECONDARY_ID         48
#define ARM64_SECONDARY_READY      56
#define ARM64_SECONDARY_SLOTS      256

// MPIDR affinity is assembled from Aff0-2 in bits 23:0 and Aff3 in bits 39:32.
#define ARM64_AFFINITY_LOW         0xffffff
#define ARM64_AFFINITY_HIGH        0xff
#define ARM64_AFFINITY_HIGH_SHIFT  32

// VBAR_EL1 needs 2 KiB alignment; each 128-byte vector slot starts with a branch.
#define ARM64_VECTOR_ALIGN         11
#define ARM64_VECTOR_PADDING       124

// DAIFSet immediate 1111 masks debug, SError, IRQ and FIQ; immediate 0010
// changes only IRQ masking. CPACR.FPEN=11 permits FP/SIMD at EL0 and EL1.
#define ARM64_DAIF_ALL             15
#define ARM64_DAIF_IRQ             2
#define ARM64_CPACR_FP_ENABLE      0x300000

// ESR.EC identifies SVC; PSTATE.M selects EL0t (0) or EL1h (5).
#define ARM64_ESR_CLASS_SHIFT      26
#define ARM64_EC_SVC               0x15
#define ARM64_SVC_YIELD            1
#define ARM64_PSTATE_MODE_MASK     0xf
#define ARM64_PSTATE_EL0T          0
#define ARM64_PSTATE_EL1H          5

// CurrentEL encodes the exception level in bits [3:2]. HCR.RW selects AArch64
// EL1; CNTHCTL bits 0/1 grant EL1 physical counter/timer access when E2H=0.
#define ARM64_CURRENT_EL1          4
#define ARM64_CURRENT_EL2          8
#define ARM64_HCR_AARCH64          0x80000000
#define ARM64_CNTHCTL_PHYSICAL     3

// EL2 startup supplies SCTLR's baseline control/RES1 bits, with MMU/caches off,
// and returns to EL1h with all asynchronous exceptions masked in SPSR.
#define ARM64_SCTLR_BASE_LOW       0x0800
#define ARM64_SCTLR_BASE_HIGH      0x30d0
#define ARM64_SPSR_MASKED_EL1H     0x3c5

// These SCTLR bits enable the MMU, data cache and instruction cache at EL1.
#define ARM64_SCTLR_MMU               (1 << 0)
#define ARM64_SCTLR_DATA_CACHE        (1 << 2)
#define ARM64_SCTLR_INSTRUCTION_CACHE (1 << 12)

// ID_AA64ISAR2.MOPS=1 supplies memory operations. ID_AA64MMFR1.HCX describes
// HCRX availability; HCRX.MSCEn permits MOPS below EL2.
#define ARM64_ISAR2_MOPS_SHIFT     16
#define ARM64_ISAR2_MOPS_SUPPORTED 1
#define ARM64_FEATURE_FIELD_BITS   4
#define ARM64_MMFR1_HCX_SHIFT      40
#define ARM64_HCRX_MOPS_ENABLE     0x800

#endif //!__VALI_AARCH64_ABI_H__
