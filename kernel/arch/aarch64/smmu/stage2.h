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
 * ARM SMMU Stage-2 Page-Table Descriptors
 * Bit definitions for the 4 KiB stage-2 translation table format shared by
 * SMMUv3 and MMU-500.
 *
 * On ARM, stage-2 tables translate a device's DMA address directly into a
 * physical RAM address. A 48-bit address is walked across up to four 4 KiB
 * levels (levels 0 through 3), where each 4 KiB table holds 512 64-bit entries
 * indexed by 9 bits of the address. Individual controllers may cap the address
 * width below 48 bits during hardware probing. Register layouts, table walks,
 * and TLB invalidations live in each backend driver.
 */

#ifndef __ARM_SMMU_STAGE2_H__
#define __ARM_SMMU_STAGE2_H__

// Page geometry and level bit shifts for 4 KiB translation granules.
#define ARM_SMMU_S2_PAGE_SHIFT        12U
#define ARM_SMMU_S2_PAGE_SIZE         4096U
#define ARM_SMMU_S2_TABLE_ENTRIES     512U
#define ARM_SMMU_S2_LEVEL_BITS        9U
#define ARM_SMMU_S2_LEVEL_COUNT       4U
#define ARM_SMMU_S2_LEVEL0_SHIFT      39U
#define ARM_SMMU_S2_LEVEL1_SHIFT      30U
#define ARM_SMMU_S2_LEVEL2_SHIFT      21U
#define ARM_SMMU_S2_LEVEL3_SHIFT      12U
#define ARM_SMMU_S2_ADDRESS_LIMIT     0x0000FFFFFFFFFFFFULL
#define ARM_SMMU_S2_ADDRESS_MASK      0x0000FFFFFFFFF000ULL

// Bits [1:0] tell hardware whether a 64-bit entry is empty (0), points to a
// next-level table (3 at levels 0..2), or maps a final 4 KiB page (3 at level 3).
// Tables and pages share the value 3 and are told apart by table depth.
// Larger block mappings (2 MiB or 1 GiB entries) are left out here so every
// mapping uses uniform 4 KiB pages.
#define ARM_SMMU_S2_TYPE_MASK         3ULL
#define ARM_SMMU_S2_INVALID           0ULL
#define ARM_SMMU_S2_TABLE             3ULL
#define ARM_SMMU_S2_PAGE              3ULL

// Stage-2 access permissions (bits [7:6]) enable device reads and writes
// independently, so a write-only buffer sets WRITE alone. Stage-2 also uses a
// separate Access Flag (bit 10) that must be set on valid pages to prevent a
// hardware fault on first access when software manages access tracking.
#define ARM_SMMU_S2_PERMISSION_MASK   (3ULL << 6)
#define ARM_SMMU_S2_READ              (1ULL << 6)
#define ARM_SMMU_S2_WRITE             (1ULL << 7)
#define ARM_SMMU_S2_ACCESS_FLAG       (1ULL << 10)

// Shareability (bits [9:8]) and memory type (bits [5:2]) describe how DMA
// transfers interact with CPU caches. Outer-shareable write-back assumes the
// controller's forced write-back feature (S2FWB) is turned off. These bits
// describe the target DMA buffer, separately from how the CPU maps table RAM.
#define ARM_SMMU_S2_SHAREABILITY_MASK (3ULL << 8)
#define ARM_SMMU_S2_OUTER_SHAREABLE   (2ULL << 8)
#define ARM_SMMU_S2_INNER_SHAREABLE   (3ULL << 8)
#define ARM_SMMU_S2_MEMORY_MASK       (15ULL << 2)
#define ARM_SMMU_S2_NORMAL_UNCACHED   (5ULL << 2)
#define ARM_SMMU_S2_NORMAL_WRITE_BACK (15ULL << 2)

// Bit 54 blocks instruction fetches from device-mapped pages. On hardware with
// extended execute-never support, bits [54:53] act as a two-bit field where
// binary 10 denies execution at all privilege levels (setting bit 53 as well
// would allow privileged execution). Hardware dirty tracking (DBM) and
// contiguous-page hints stay disabled on these entries.
#define ARM_SMMU_S2_EXECUTE_NEVER     (1ULL << 54)

#endif //!__ARM_SMMU_STAGE2_H__
