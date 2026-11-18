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
 * MMU-500 register offsets, bitfields, and write-verification masks.
 * References: Arm IHI 0062D.c (SMMUv2 architecture, chapters 9, 11, and 17)
 * and Arm DDI 0517F (CoreLink MMU-500 TRM for ACR, ACTLR, and revisions).
 *
 * Register window layout:
 * - Page 0 (GR0, offset 0): Global controls, ID registers, global fault
 *   reporting, global TLB invalidation, and stream match/routing tables.
 * - Page 1 (GR1, offset 1 << PageShift): Per-bank configuration attributes
 *   and fault Stream ID syndrome registers.
 * - Context banks (ContextBase + index * PageSize): Per-bank translation
 *   controls, page-table root pointer, and bank fault registers.
 */

#ifndef __ARM_SMMU500_REGISTERS_H__
#define __ARM_SMMU500_REGISTERS_H__

#include "../mmio.h"
#include <os/osdefs.h>

// Global Register Space 0 (GR0) offsets, relative to the unit's register base.
#define SMMU500_CR0                 0x000U // Global client configuration (fault, bypass, VMID rules).
#define SMMU500_ACR                 0x010U // MMU-500 auxiliary control (cache bypass and cache lock).
#define SMMU500_IDR0                0x020U // ID0: translation stages, stream matching, slot count, SID bits.
#define SMMU500_IDR1                0x024U // ID1: register page size, bank layout, total and stage-2 bank counts.
#define SMMU500_IDR2                0x028U // ID2: input/output address widths and supported page sizes.
#define SMMU500_IDR7                0x03CU // ID7: MMU-500 architecture and minor revision number.
#define SMMU500_GFAR                0x040U // Global Fault Address Register (64-bit).
#define SMMU500_GFSR                0x048U // Global Fault Status Register (write-1-to-clear).
#define SMMU500_GFSYNR0             0x050U // Global Fault Syndrome 0 (access and fault attributes).
#define SMMU500_GFSYNR1             0x054U // Global Fault Syndrome 1 (Stream ID and Substream ID).
#define SMMU500_GFSYNR2             0x058U // Global Fault Syndrome 2 (additional implementation syndrome).
#define SMMU500_TLBIVMID            0x064U // Invalidate stage-2 TLB entries for one VMID.
#define SMMU500_TLBIALLNSNH         0x068U // Invalidate all Non-secure non-Hyp TLB entries.
#define SMMU500_TLBGSYNC            0x070U // Trigger global TLB synchronization barrier.
#define SMMU500_TLBGSTATUS          0x074U // Global TLB synchronization status.
#define SMMU500_SYNC_ACTIVE         1U     // Bit 0 of TLBGSTATUS: 1 while global sync is in progress.

// Stream matching and routing tables in GR0 (one entry per hardware stream slot).
// SMR matches an incoming device Stream ID. S2CR routes matches to a context
// bank or marks the slot as faulting.
#define SMMU500_SMR(index)          (0x800U + 4U * (index))
#define SMMU500_S2CR(index)         (0xC00U + 4U * (index))

// Global Register Space 1 (GR1) offsets. Add (1 << PageShift) to reach GR1
// before applying these offsets.
#define SMMU500_CBAR(index)         (4U * (index))          // Context bank type (stage 2) and VMID.
#define SMMU500_CBFRSYNRA(index)    (0x400U + 4U * (index)) // Offending Stream ID (low 16 bits) on bank fault.
#define SMMU500_CBA2R(index)        (0x800U + 4U * (index)) // Context bank page-table format (AArch64 vs AArch32).

// Per-context-bank register offsets, relative to __SMMU500ContextBankOffset.
#define SMMU500_CB_SCTLR            0x000U // Bank system control (enable bit and fault behavior).
#define SMMU500_CB_ACTLR            0x004U // Bank auxiliary control (prefetch enable).
#define SMMU500_CB_TTBR0            0x020U // Translation Table Base Register 0 (64-bit root table PA).
#define SMMU500_CB_TCR              0x030U // Translation Control Register (address widths, walk depth, cacheability).
#define SMMU500_CB_FSR              0x058U // Bank Fault Status Register (write-1-to-clear).
#define SMMU500_CB_FAR              0x060U // Bank Fault Address Register (64-bit faulting IOVA).
#define SMMU500_CB_FSYNR0           0x068U // Bank Fault Syndrome 0 (access type, level, attributes).

// Capability bits read from IDR0 and IDR2 during probing.
#define SMMU500_IDR0_STAGE2         (1U << 29) // S2TS: controller supports stage-2 translation.
#define SMMU500_IDR0_MATCHING       (1U << 27) // SMS: controller supports stream-ID matching (SMR/S2CR).
#define SMMU500_IDR0_EXTENDED       ((1U << 15) | (1U << 8)) // EXIDS/EXVMIDS: 16-bit extended IDs (unsupported).
#define SMMU500_IDR0_CTTW           (1U << 14) // Coherent Translation Table Walk tie-off signal.
#define SMMU500_IDR2_GRANULE4K      (1U << 12) // PTF4KB: supports 4 KiB translation granules.

// Global control bits (CR0).
// CLIENTPD forces global bypass when set, so it stays 0. FAULTS enables global
// fault recording. UNMATCHED_FAULT (USFCFG) forces unmatched Stream IDs to fault
// instead of bypassing. PRIVATE_VMID keeps VMIDs isolated per context bank.
// CR0_FIELDS masks the writable control bits checked after every write,
// ensuring extended IDs, forced broadcasts, interrupts, and CLIENTPD stay 0.
#define SMMU500_CR0_CLIENTPD        (1U << 0)
#define SMMU500_CR0_FAULTS          ((1U << 1) | (1U << 4))
#define SMMU500_CR0_UNMATCHED_FAULT (1U << 10)
#define SMMU500_CR0_PRIVATE_VMID    ((1U << 11) | (1U << 12))
#define SMMU500_CR0_FIELDS          (0x8000FC3FU)

// Auxiliary cache and prefetch controls cleared during unit startup.
#define SMMU500_ACR_CACHE_LOCK      (1U << 26) // Locks page-table cache lines (MMU-500 r2p0+).
#define SMMU500_ACR_BYPASS_CACHE    ((1U << 10) | (1U << 9) | (1U << 8)) // Forces cache bypass on TLB/PTW.
#define SMMU500_ACTLR_PREFETCH      (1U << 1)  // Enables speculative table prefetch in a context bank.

// Stream match (SMR) and stream routing (S2CR) bits and verification masks.
// SMR_FIELDS checks the VALID bit (31) and ID/MASK bits while skipping bit 15
// (reserved/EXID). S2CR_FIELDS checks type, transient/cache hints, and CBNDX.
// S2CR_FAULT_FIELDS checks only the 2-bit routing TYPE field [17:16].
#define SMMU500_SMR_VALID           (1U << 31)
#define SMMU500_SMR_FIELDS          0xFFFF7FFFU
#define SMMU500_S2CR_FAULT          (2U << 16)
#define SMMU500_S2CR_FIELDS         0x030300FFU
#define SMMU500_S2CR_FAULT_FIELDS   (3U << 16)

// Context bank configuration bits and verification masks.
#define SMMU500_CBA2R_VA64          1U          // Select AArch64 64-bit translation table format.
#define SMMU500_CBAR_FIELDS         0x000300FFU // Checks TYPE [17:16] (0 = stage 2) and VMID [7:0].
#define SMMU500_CBA2R_FIELDS        1U          // Checks bit 0 (VA64).

// Context bank SCTLR: M (bit 0) enables translation and CFRE (bit 5) records
// context faults in CB_FSR. CFCFG and CFIE stay 0 so faults terminate DMA
// immediately instead of stalling the bus or firing context interrupts.
#define SMMU500_SCTLR_ENABLE        1U
#define SMMU500_SCTLR_FAULTS        (1U << 5)
#define SMMU500_SCTLR_FIELDS        0x1F1U

// Context bank stage-2 VTCR fields: write-back cacheable, outer-shareable
// page-table walks. VTCR_FIELDS masks writable geometry and attribute bits.
#define SMMU500_VTCR_CACHE_WB       ((1U << 8) | (1U << 10))
#define SMMU500_VTCR_OUTER_SHARED   (2U << 12)
#define SMMU500_VTCR_FIELDS         0x8007FFFFU

// Global and context fault status masks.
// Bits in W1C masks are write-1-to-clear. Ordinary client faults (such as USF
// and UUT in GFSR, or translation/access/permission/address-size/UUT in FSR)
// stop new DMA work while still allowing safe cleanup. Any other fault bit
// (such as MULTI overflow, stalled requests, or format/external errors) means
// hardware completion can no longer be trusted.
#define SMMU500_GFSR_ORDINARY       ((1U << 1) | (1U << 8))
#define SMMU500_GFSR_W1C            0x8000017FU
#define SMMU500_FSR_FAULTS          0xC00001FEU
#define SMMU500_FSR_FORMAT          (3U << 9)
#define SMMU500_FSR_ORDINARY        ((1U << 1) | (1U << 2) | (1U << 3) | (1U << 7) | (1U << 8))
#define SMMU500_FSR_W1C             0x800001FEU

// Host tests swap out MMIO reads, writes, and barriers at compile time for an
// in-memory hardware model. Production builds call the shared SMMU helpers in
// ../mmio.h directly without a runtime dispatch branch.
#ifdef SMMU500_TEST
uint32_t SMMU500TestRead32(uintptr_t base, unsigned int offset);
uint64_t SMMU500TestRead64(uintptr_t base, unsigned int offset);
void SMMU500TestWrite32(uintptr_t base, unsigned int offset, uint32_t value);
void SMMU500TestWrite64(uintptr_t base, unsigned int offset, uint64_t value);
void SMMU500TestBarrier(void);
#define __SMMU500Read32 SMMU500TestRead32
#define __SMMU500Read64 SMMU500TestRead64
#define __SMMU500Write32 SMMU500TestWrite32
#define __SMMU500Write64 SMMU500TestWrite64
#define __SMMU500Barrier SMMU500TestBarrier
#else
#define __SMMU500Read32 __ArmSMMURead32
#define __SMMU500Read64 __ArmSMMURead64
#define __SMMU500Write32 __ArmSMMUWrite32
#define __SMMU500Write64 __ArmSMMUWrite64
#define __SMMU500Barrier __ArmSMMUBarrier
#endif

#endif //!__ARM_SMMU500_REGISTERS_H__
