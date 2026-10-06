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

#ifndef __VALI_RPI_REGISTERS_H__
#define __VALI_RPI_REGISTERS_H__

// Assembly-only constants: no C types or integer suffixes are required.
// CurrentEL stores the exception level in bits [3:2].
#define RPI_CURRENT_EL1 4
#define RPI_CURRENT_EL2 8

// DAIFSet immediate selects all debug, SError, IRQ and FIQ masks.
#define RPI_DAIF_ALL 0xf

// MPIDR affinity fields exclude MT/U flags; Aff3 is stored separately.
#define RPI_MPIDR_AFF012_MASK 0xffffff
#define RPI_MPIDR_AFF3_SHIFT  32
#define RPI_MPIDR_AFF3_WIDTH  8

// SCTLR fields checked before entering an MMU-off, little-endian kernel.
#define RPI_SCTLR_M_BIT  0
#define RPI_SCTLR_C_BIT  2
#define RPI_SCTLR_EE_BIT 25

// Instruction-cache enable is cleared while preserving other EL2 controls.
#define RPI_SCTLR_I_MASK 0x1000

// Reject VHE register aliases and select AArch64 EL1 without stage 2 or traps.
#define RPI_HCR_E2H_BIT 34
#define RPI_HCR_RW_MASK 0x80000000

// Optional feature fields used to enable memory copy/set instructions below EL2.
#define RPI_ISAR2_MOPS_SHIFT    16
#define RPI_MMFR1_HCX_SHIFT     40
#define RPI_FEATURE_FIELD_WIDTH 4
#define RPI_HCRX_MSCEN_MASK     0x800

// Non-VHE A72/A76 CPTR baseline RES1 fields; FP/trace/access traps remain clear.
#define RPI_CPTR_EL2_RES1 0x33ff

// CNTHCTL permits EL1 physical counter and timer access in the non-VHE regime.
#define RPI_CNTHCTL_EL1_ACCESS 3

// Two move-immediate halves of the supported SCTLR_EL1 baseline, 0x30d00800.
#define RPI_SCTLR_EL1_BASELINE_LOW  0x0800
#define RPI_SCTLR_EL1_BASELINE_HIGH 0x30d0

// Saved PSTATE masks D/A/I/F and selects AArch64 EL1h with SP_EL1.
#define RPI_SPSR_EL1H_MASKED 0x3c5

// Architectural exception table: sixteen 128-byte slots at a 2 KiB boundary.
#define RPI_VECTOR_ALIGNMENT    2048
#define RPI_VECTOR_COUNT        16
#define RPI_VECTOR_SLOT_PADDING 124

// File-backed bootstrap stack has a 64 KiB budget and 4 KiB alignment.
#define RPI_BOOT_STACK_ALIGNMENT 4096
#define RPI_BOOT_STACK_SIZE      65536

#endif