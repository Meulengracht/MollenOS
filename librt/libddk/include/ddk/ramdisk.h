/**
 * Copyright 2017, Philip Meulengracht
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

#ifndef __DDK_RAMDISK_H__
#define __DDK_RAMDISK_H__

#include <ddk/ddkdefs.h>

/**
 * Definitions for the MollenOS ramdisk
 * This is the magic signature, must be present in the ramdisk image file */
#define RAMDISK_MAGIC               0x3144524D
#define RAMDISK_VERSION_1           0x01

/**
 * Supported architectures, must of course match
 * the architecture the kernel has been compiled with */
#define RAMDISK_ARCH_X86_32         0x08
#define RAMDISK_ARCH_X86_64         0x10

/**
 * This is the different ramdisk-header identifiers
 * Entries can be either generic files, directories or modules (/server) */
#define RAMDISK_FILE                0x1
#define RAMDISK_DIRECTORY           0x2
#define RAMDISK_MODULE              0x4

/**
 * These are the different identifiers for a module
 * since modules can be different types of modules */
#define RAMDISK_MODULE_SHARED       0x1
#define RAMDISK_MODULE_SERVER       0x2

/**
 * The ramdisk header, this is present in the first few bytes of the ramdisk image, members
 * do not vary in length
 */
PACKED_TYPESTRUCT(RamdiskHeader, {
    uint32_t Magic;
    uint32_t Version;
    uint32_t Architecture;
    int32_t  FileCount;
});

/**
 * This is the ramdisk entry, which describes an entry in the ramdisk. The ramdisk entry area
 * contains headers right after each other
 */
PACKED_TYPESTRUCT(RamdiskEntry, {
    uint8_t  Name[64]; // UTF-8 Encoded filename
    uint32_t Type; // Check the ramdisk entry definitions
    uint32_t DataHeaderOffset; // offset in the ramdisk
});

/**
 * This is the module header, and contains basic information
 * about the module data that follow this header.
 */
PACKED_TYPESTRUCT(RamdiskModuleHeader, {
    uint32_t Flags;
    uint32_t LengthOfData; // Excluding this header
    uint32_t Crc32OfData;

    uint32_t VendorId;
    uint32_t DeviceId;
    uint32_t DeviceType;
    uint32_t DeviceSubType;
});

#endif //!__DDK_RAMDISK_H__
