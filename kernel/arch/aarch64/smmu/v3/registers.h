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
 * SMMUv3 hardware registers, table entries, and command encodings.
 * Follows the ARM SMMUv3 architecture specification (Arm IHI 0070 G.a,
 * chapters 4 through 6).
 *
 * Register layout:
 * Only the standard two-page Non-secure register layout (128 KiB total) is
 * supported. Page 0 starts at offset 0x0000 and holds capabilities, global
 * controls, stream-table base, and command-queue pointers. Page 1 starts at
 * offset 0x10000 and holds the architectural event-queue producer and consumer
 * pointers.
 */

#ifndef __ARM_SMMUV3_REGISTERS_H__
#define __ARM_SMMUV3_REGISTERS_H__

#include "../mmio.h"
#include <os/osdefs.h>

// Total MMIO register window size across Page 0 and Page 1 (128 KiB).
#define SMMUV3_REGISTER_SIZE       0x20000U

// Read-only hardware capability and identification registers.
#define SMMUV3_IDR0                0x0000U  // Translation stages, coherency, ATS, VMID size, table format.
#define SMMUV3_IDR1                0x0004U  // Fixed-base flags, queue sizes, Stream ID width.
#define SMMUV3_IDR5                0x0014U  // Supported page sizes (granules) and physical output width.
#define SMMUV3_IIDR                0x0018U  // Implementer and product identification.
#define SMMUV3_AIDR                0x001CU  // SMMUv3 architecture revision (0 = v3.0 .. 4 = v3.4).

// Global control, status, and error registers.
// CR0 changes take effect asynchronously: software writes CR0 and polls CR0ACK
// until hardware echoes the matching value. IRQ_CTRL and IRQ_CTRLACK work the
// same way. GERROR flips bits when global hardware errors happen, and software
// acknowledges them by matching those bit values in GERRORN.
#define SMMUV3_CR0                 0x0020U  // Global enable bits (SMMU, event queue, command queue, ATS check).
#define SMMUV3_CR0ACK              0x0024U  // Hardware acknowledgement copy of CR0.
#define SMMUV3_CR1                 0x0028U  // Cacheability and shareability for table and queue memory accesses.
#define SMMUV3_CR2                 0x002CU  // Global behavior flags (such as recording bad Stream IDs).
#define SMMUV3_GBPA                0x0044U  // Global Bypass Attribute (applied when translation is disabled).
#define SMMUV3_IRQ_CTRL            0x0050U  // Interrupt enable controls for GERROR, PRI, and event queues.
#define SMMUV3_IRQ_CTRLACK         0x0054U  // Hardware acknowledgement copy of IRQ_CTRL.
#define SMMUV3_GERROR              0x0060U  // Global error toggle flags set by hardware.
#define SMMUV3_GERRORN             0x0064U  // Global error acknowledgement flags written by software.

// Stream table, command queue, and event queue base/index registers.
// Page 1 offsets for EVTQ_PROD and EVTQ_CONS are architectural. Page 0
// aliases are optional on hardware and are never used.
#define SMMUV3_STRTAB_BASE         0x0080U  // Physical address and attributes of the stream table root.
#define SMMUV3_STRTAB_CFG          0x0088U  // Stream table format (2-level), L2 split, and Stream ID width.
#define SMMUV3_CMDQ_BASE           0x0090U  // Physical address, attributes, and log2 size of the command ring.
#define SMMUV3_CMDQ_PROD           0x0098U  // Command ring producer index (written by software).
#define SMMUV3_CMDQ_CONS           0x009CU  // Command ring consumer index and error code (updated by hardware).
#define SMMUV3_EVTQ_BASE           0x00A0U  // Physical address, attributes, and log2 size of the event ring.
#define SMMUV3_EVTQ_PROD           0x100A8U // Event ring producer index and overflow toggle (updated by hardware).
#define SMMUV3_EVTQ_CONS           0x100ACU // Event ring consumer index and overflow ack (written by software).

// IDR0 capability bits: supported translation features and formats.
#define SMMUV3_IDR0_STAGE2         (1U << 0)   // Stage-2 (device physical address) translation is supported.
#define SMMUV3_IDR0_AARCH64        (2U << 2)   // AArch64 (VMSAv8-64) page-table format is supported.
#define SMMUV3_IDR0_COHERENT       (1U << 4)   // Hardware table/queue walks are cache-coherent with CPUs.
#define SMMUV3_IDR0_ATS            (1U << 10)  // PCIe Address Translation Services (ATS) is supported.
#define SMMUV3_IDR0_VMID16         (1U << 18)  // 16-bit VMIDs are supported (otherwise 8-bit VMIDs).
#define SMMUV3_IDR0_ENDIAN_SHIFT   21U         // Table-walk endianness field (0 = mixed, 2 = little-endian).
#define SMMUV3_IDR0_STALL_SHIFT    24U         // Stall model field (0 = no stall, 1 = optional, 2 = forced stall).
#define SMMUV3_IDR0_TABLE_SHIFT    27U         // Stream-table format field (1 = two-level supported).

// IDR1 capability bits: preset-base flags, queue log2 sizes, and SID width.
#define SMMUV3_IDR1_PRESET         ((1U << 30) | (1U << 29) | (1U << 28)) // Hardware fixes table or queue bases.
#define SMMUV3_IDR1_CMD_SHIFT      21U         // Bit offset of max command queue log2 size (5 bits).
#define SMMUV3_IDR1_EVENT_SHIFT    16U         // Bit offset of max event queue log2 size (5 bits).
#define SMMUV3_IDR1_SID_MASK       63U         // Mask for max supported Stream ID width in bits.

// IDR5 capability bits: granule support and physical output address size (OAS).
#define SMMUV3_IDR5_GRANULE4K      (1U << 4)   // 4 KiB translation granule is supported.
#define SMMUV3_IDR5_OAS_MASK       7U          // Mask for Output Address Size encoding (0..6 = 32..52 bits).
#define SMMUV3_FIELD5_MASK         31U         // Mask for 5-bit queue size fields in IDR1.

// CR0 control bits.
#define SMMUV3_CR0_ENABLE          (1U << 0)   // SMMUEN: turn on stream lookup and translation.
#define SMMUV3_CR0_EVENTS          (1U << 2)   // EVENTQEN: allow hardware to write fault events to the event ring.
#define SMMUV3_CR0_COMMANDS        (1U << 3)   // CMDQEN: allow hardware to fetch commands from the command ring.
#define SMMUV3_CR0_ATS_CHECK       (1U << 4)   // ATSCHK: reject pre-translated PCIe ATS requests unless ATS is enabled for the device.

// GBPA control bits. Hardware clears UPDATE once the new attribute takes effect.
#define SMMUV3_GBPA_ABORT          (1U << 20)  // Abort all incoming DMA while translation is disabled.
#define SMMUV3_GBPA_UPDATE         (1U << 31)  // Trigger hardware to latch the written GBPA settings.

// CR1 and CR2 settings.
// Each CR1 cacheability field uses 01 for write-back caching and 10 for outer-shareable.
#define SMMUV3_CR1_OUTER_SHARED    ((2U << 10) | (2U << 4))
#define SMMUV3_CR1_WRITE_BACK      ((1U << 8) | (1U << 6) | (1U << 2) | 1U)
#define SMMUV3_CR2_RECORD_BAD_SID  (1U << 1)   // RECINVSID: log an event when a device uses an unmapped Stream ID.

// Queue base attributes and status bits.
#define SMMUV3_QUEUE_CACHEABLE     (1ULL << 62) // RAWAWB: mark queue or stream-table base memory write-back cacheable (read/write allocate).
#define SMMUV3_QUEUE_ERROR_MASK    (0x7FU << 24) // ERR field in CMDQ_CONS (valid only while GERROR.CMDQ_ERR is active).
#define SMMUV3_EVENT_OVERFLOW      (1U << 31)   // OVFLG bit in EVTQ_PROD/CONS, toggled when events are dropped.

// Event record layout (32 bytes = four 64-bit words per ring entry).
// Word 0 holds the event code in bits [7:0] and the device Stream ID (SID) in
// bits [63:32]. Bit 31 of word 1 marks a stalled transaction that is waiting
// for software to resume or terminate it. Translation faults (codes 0x10..0x13)
// store the faulting device input address in word 2.
#define SMMUV3_EVENT_WORDS         4U
#define SMMUV3_EVENT_TYPE_MASK     0xFFU
#define SMMUV3_EVENT_SID_SHIFT     32U
#define SMMUV3_EVENT_STALL         (1ULL << 31)
#define SMMUV3_EVENT_ADDRESS_FIRST 0x10U
#define SMMUV3_EVENT_ADDRESS_LAST  0x13U

// Two-level stream table layout and Stream Table Entry (STE) encodings.
// Each Level-2 table is one 4 KiB page holding 64 STEs of 64 bytes (8 words)
// each, so the low 6 bits of a Stream ID select the entry inside an L2 page.
// The Level-1 descriptor span encodes 1 + log2(entries per L2 page), so 7 for 64.
#define SMMUV3_STREAM_SPLIT        6U
#define SMMUV3_STREAM_ENTRIES      64U
#define SMMUV3_STREAM_WORDS        8U
#define SMMUV3_STRTAB_TWO_LEVEL    (1U << 16)
#define SMMUV3_STRTAB_SPLIT_SHIFT  6U
#define SMMUV3_L1_SPAN             (SMMUV3_STREAM_SPLIT + 1U)
#define SMMUV3_STE_ABORT           1ULL              // V=1, Config=0b000 (abort all traffic for this SID).
#define SMMUV3_STE_STAGE2          (1ULL | (6ULL << 1)) // V=1, Config=0b110 (translate via stage-2 page table).
#define SMMUV3_STE_AARCH64         (1ULL << 51)      // S2AA64: use AArch64 64-bit stage-2 descriptors.
#define SMMUV3_STE_RECORD_FAULT    (1ULL << 58)      // S2R: write stage-2 faults to the event ring.
#define SMMUV3_STE_VTCR_SHIFT      32U               // Bit offset of S2VTCR fields inside STE word 2.

// Stage-2 translation control (S2VTCR) fields embedded in each STE.
#define SMMUV3_VTCR_SL0_SHIFT      6U                // Starting walk level (2 - RootLevel: 2 for L0, 1 for L1).
#define SMMUV3_VTCR_CACHE_WB       ((1U << 8) | (1U << 10)) // Inner and outer write-back cacheability for table walks.
#define SMMUV3_VTCR_OUTER_SHARED   (2U << 12)        // Outer-shareable memory attribute for table walks.
#define SMMUV3_VTCR_PS_SHIFT       16U               // Physical output address size encoding (matches IDR5.OAS).

// Command queue opcodes (16 bytes = two 64-bit words per command).
// All reserved bits and unused command words must be written as zero.
#define SMMUV3_CMD_CFGI_STE        0x03ULL // Invalidate cached stream config for one Stream ID.
#define SMMUV3_CMD_CFGI_ALL        0x04ULL // Invalidate all cached stream and L1 table config.
#define SMMUV3_CMD_TLBI_VMID       0x28ULL // Flush cached stage-2 TLB entries for one VMID.
#define SMMUV3_CMD_TLBI_ALL        0x30ULL // Flush all cached stage-2 TLB entries on the unit.
#define SMMUV3_CMD_SYNC            0x46ULL // Wait until all earlier commands finish taking effect.
#define SMMUV3_CMD_ID_SHIFT        32U     // Bit offset of SID or VMID in command word 0.
#define SMMUV3_CMD_CFGI_LEAF       1ULL    // Word 1 flag: invalidate only the L2 STE, keep L1 cached.
#define SMMUV3_CMD_CFGI_ALL_RANGE  31ULL   // Word 1 range for CFGI_ALL (31 covers all SID bits).
#define SMMUV3_CMD_OPCODE_MASK     0xFFULL

// Host unit tests compile with SMMUV3_TEST to redirect MMIO reads, writes, and
// memory barriers to an in-memory software register model. Production kernel
// builds have no runtime dispatch and call the shared MMIO helpers in ../mmio.h.
#ifdef SMMUV3_TEST
uint32_t SMMUv3TestRead32(uintptr_t base, unsigned int offset);
void SMMUv3TestWrite32(uintptr_t base, unsigned int offset, uint32_t value);
void SMMUv3TestWrite64(uintptr_t base, unsigned int offset, uint64_t value);
void SMMUv3TestBarrier(void);
#define __SMMUv3Read32 SMMUv3TestRead32
#define __SMMUv3Write32 SMMUv3TestWrite32
#define __SMMUv3Write64 SMMUv3TestWrite64
#define __SMMUv3Barrier SMMUv3TestBarrier
#else
#define __SMMUv3Read32 __ArmSMMURead32
#define __SMMUv3Write32 __ArmSMMUWrite32
#define __SMMUv3Write64 __ArmSMMUWrite64
#define __SMMUv3Barrier __ArmSMMUBarrier
#endif

#endif //!__ARM_SMMUV3_REGISTERS_H__
