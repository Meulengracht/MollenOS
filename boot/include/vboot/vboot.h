/**
 * Copyright 2021, Philip Meulengracht
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

#ifndef __VBOOT_H__
#define __VBOOT_H__

#include "def.h"

#define VBOOT_MAGIC   0xAEB007AE
// Version 1.1 defines native physical entry and the DeviceTree tail. All
// producers, including the BIOS assembly mirror, must use the same layout.
#define VBOOT_VERSION 0x00010001

#define VBOOT_FEATURE_PHYSICAL          1ULL
#define VBOOT_FEATURE_MMU_OFF           2ULL
#define VBOOT_FEATURE_RESERVED_FIRMWARE 4ULL
#define VBOOT_FEATURE_GOP               8ULL

#define VBOOT_PSCI_HVC 1
#define VBOOT_PSCI_SMC 2

#define VBOOT_CONSOLE_PL011 1

// Memory cacheability attributes
#define VBOOT_MEMORY_UC               0x0000000000000001ULL // Uncached
#define VBOOT_MEMORY_WC               0x0000000000000002ULL // Write-Combined
#define VBOOT_MEMORY_WT               0x0000000000000004ULL // Write-Through
#define VBOOT_MEMORY_WB               0x0000000000000008ULL // Write-Back
#define VBOOT_MEMORY_UCE              0x0000000000000010ULL // Uncached, Extended

// Physical memory protection attributes
#define VBOOT_MEMORY_WP               0x0000000000001000ULL // Cacheability Protection
#define VBOOT_MEMORY_RP               0x0000000000002000ULL // Read-Protected
#define VBOOT_MEMORY_XP               0x0000000000004000ULL // Execute-Protected
#define VBOOT_MEMORY_NV               0x0000000000008000ULL // The memory region supports byte-addressable non-volatility.
#define VBOOT_MEMORY_MORE_RELIABLE    0x0000000000010000ULL // The memory region is more reliable than the default.
#define VBOOT_MEMORY_RO               0x0000000000020000ULL // The memory region is read-only.
#define VBOOT_MEMORY_SP               0x0000000000040000ULL // The memory region is earmarked for drivers or apps that require special access.
#define VBOOT_MEMORY_CPU_CRYPTO       0x0000000000080000ULL // The memory region is capable of being protected with the CPU's memory cryptographic capabilities.
#define VBOOT_MEMORY_NO_MAP           0x0000000000100000ULL // The memory region must not be mapped into the kernel direct map.
#define VBOOT_MEMORY_RUNTIME          0x8000000000000000ULL // The memory region is a runtime allocated region.

#define VBOOT_CACHE_ATTRIBUTE_MASK  (VBOOT_MEMORY_UC | VBOOT_MEMORY_WC | VBOOT_MEMORY_WT | VBOOT_MEMORY_WB | VBOOT_MEMORY_UCE | VBOOT_MEMORY_WP)
#define VBOOT_MEMORY_ACCESS_MASK    (VBOOT_MEMORY_RP | VBOOT_MEMORY_XP | VBOOT_MEMORY_RO)
#define VBOOT_MEMORY_ATTRIBUTE_MASK (VBOOT_MEMORY_ACCESS_MASK | VBOOT_MEMORY_SP | VBOOT_MEMORY_CPU_CRYPTO)

enum VBootFirmware {
    VBootFirmware_BIOS,
    VBootFirmware_UEFI,
    VBootFirmware_Native
};

enum VBootMemoryType {
    VBootMemoryType_Reserved,
    VBootMemoryType_Firmware,
    VBootMemoryType_ACPI,
    VBootMemoryType_NVS,
    VBootMemoryType_Available,
    VBootMemoryType_Reclaim
};

/**
 * The original firmware DTB remains reserved and readable through full parsing.
 * PhysicalBase stays a physical address; memory initialization must retain an
 * identity mapping or the consumer must map it before dereferencing it.
 */
VBOOT_PACKED(VBootDeviceTree, {
    unsigned long long PhysicalBase;
    unsigned int       Length;
});

VBOOT_PACKED(VBootMemoryEntry, {
    enum VBootMemoryType Type;
    unsigned long long   PhysicalBase;
    unsigned long long   VirtualBase;
    unsigned long long   Length;
    unsigned long long   Attributes;
});

VBOOT_PACKED(VBootMemory, {
    unsigned int       NumberOfEntries;
    unsigned int       EntrySize;
    unsigned long long Entries;         // struct VBootMemoryEntry*
});

VBOOT_PACKED(VBootVideo, {
    unsigned long long FrameBuffer;
    unsigned int       Width;
    unsigned int       Height;
    unsigned int       Pitch;
    unsigned int       BitsPerPixel;
    unsigned int       RedPosition;
    unsigned int       RedMask;
    unsigned int       GreenPosition;
    unsigned int       GreenMask;
    unsigned int       BluePosition;
    unsigned int       BlueMask;
    unsigned int       ReservedPosition;
    unsigned int       ReservedMask;
});

VBOOT_PACKED(VBootRamdisk, {
    unsigned long long Data;
    unsigned int       Length;
});

/**
 * VBoot provides modules, which are really just loaded PE images currently. These images are already
 * loaded and relocated, and ready to be executed.
 */
VBOOT_PACKED(VBootModule, {
    /**
     * Base is the physical address of the loaded image. It may differ from the
     * preferred ImageBase in the PE header.
     */
    unsigned long long Base;

    /**
     * EntryPoint is the absolute execution address, not necessarily a physical
     * address. The native kernel executes at Base+EntryPointRVA with MMU off.
     * Phoenix executes at its user virtual base+EntryPointRVA after mapping;
     * its physical staging address must not be used as its relocation base.
     */
    unsigned long long EntryPoint;

    /**
     * Length is the length of the loaded image. This will cover the entire image of
     * the loaded (and relocated) image in memory.
     */
    unsigned int Length;
});

VBOOT_PACKED(VBootStack, {
    unsigned long long Base;
    unsigned int       Length;
});

/**
 * Native AArch64 entry uses EL1h with physical pointers, MMU/data/instruction
 * caches off, DAIF masked, and no EFI configuration table. x0 points here;
 * x1-x3, x18 and x29 are zero. SP is the aligned exclusive top of Stack.
 * FP/SIMD is disabled; vectors and a non-returning LR target remain reserved
 * in the loader until the kernel establishes its own exception environment.
 * The kernel descriptor identifies a loaded/relocated physical image. Stack
 * describes reserved storage from its bottom, not the current stack pointer.
 * Zero optional descriptors mean absent resources, including during bring-up.
 * Copying this structure does not transfer its pointed-to storage: the producer
 * retains reservations for the map, DTB, stack and payloads until the kernel
 * has established their mappings and completed their use or explicit copying.
 */
VBOOT_PACKED(VBoot, {
    unsigned int        Magic;
    unsigned int        Version;
    enum VBootFirmware  Firmware;
    unsigned int        ConfigurationTableCount;
    unsigned int        ConfigurationEntrySize;
    unsigned long long  ConfigurationTable;

    struct VBootMemory     Memory;
    struct VBootVideo      Video;
    struct VBootRamdisk    Ramdisk;
    struct VBootModule     Kernel;
    struct VBootStack      Stack;
    struct VBootModule     Phoenix;
    struct VBootDeviceTree DeviceTree;
});

#endif //!__VBOOT_H__
