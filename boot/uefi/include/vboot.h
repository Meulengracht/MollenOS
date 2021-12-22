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

// Use this to pack structures and avoid any issues with padding
// from compilers
#if (defined (__clang__))
#define VBOOT_PACKED(name, body) struct __attribute__((packed)) name body
#elif (defined (__GNUC__))
#define VBOOT_PACKED(name, body) struct name body __attribute__((packed))
#elif (defined (__arm__))
#define VBOOT_PACKED(name, body) __packed struct name body
#elif (defined (_MSC_VER))
#define VBOOT_PACKED(name, body) __pragma(pack(push, 1)) struct name body __pragma(pack(pop))
#else
#error Please define packed struct for the used compiler
#endif

#define VBOOT_MAGIC   0xAEB007AE
#define VBOOT_VERSION 0x00010000 // V1.0

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
#define VBOOT_MEMORY_RUNTIME          0x8000000000000000ULL // The memory region is a runtime allocated region.

#define VBOOT_CACHE_ATTRIBUTE_MASK  (VBOOT_MEMORY_UC | VBOOT_MEMORY_WC | VBOOT_MEMORY_WT | VBOOT_MEMORY_WB | VBOOT_MEMORY_UCE | VBOOT_MEMORY_WP)
#define VBOOT_MEMORY_ACCESS_MASK    (VBOOT_MEMORY_RP | VBOOT_MEMORY_XP | VBOOT_MEMORY_RO)
#define VBOOT_MEMORY_ATTRIBUTE_MASK (VBOOT_MEMORY_ACCESS_MASK | VBOOT_MEMORY_SP | VBOOT_MEMORY_CPU_CRYPTO)

enum VBootFirmware {
    VBootFirmware_BIOS,
    VBootFirmware_UEFI
};

enum VBootMemoryType {
    VBootMemoryType_Reserved,
    VBootMemoryType_Firmware,
    VBootMemoryType_ACPI,
    VBootMemoryType_NVS,
    VBootMemoryType_Available,
    VBootMemoryType_Reclaim
};

VBOOT_PACKED(VBootMemoryEntry, {
    enum VBootMemoryType Type;
    unsigned long long   PhysicalBase;
    unsigned long long   VirtualBase;
    unsigned long long   Length;
    unsigned long long   Attributes;
});

VBOOT_PACKED(VBootMemory, {
    unsigned int       NumberOfEntries;
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

VBOOT_PACKED(VBootKernel, {
    unsigned long long Data;
    unsigned int       Length;
});

VBOOT_PACKED(VBootStack, {
    unsigned long long Base;
    unsigned int       Length;
});

VBOOT_PACKED(VBoot, {
    unsigned int        Magic;
    unsigned int        Version;
    enum VBootFirmware  Firmware;
    unsigned int        ConfigurationTableCount;
    unsigned long long  ConfigurationTable;

    struct VBootMemory  Memory;
    struct VBootVideo   Video;
    struct VBootRamdisk Ramdisk;
    struct VBootKernel  Kernel;
    struct VBootStack   Stack;
});

#endif //!__VBOOT_H__
