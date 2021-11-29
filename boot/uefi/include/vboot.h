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

#define VBOOT_MAGIC   0xAEB007AE
#define VBOOT_VERSION 0x00010000

enum VBootFirmware {
    VBootFirmware_BIOS,
    VBootFirmware_UEFI
};

enum VBootMemoryType {
    VBootMemoryType_Reserved,
    VBootMemoryType_Firmware,
    VBootMemoryType_ACPI,
    VBootMemoryType_NVS,
    VBootMemoryType_Available
};

struct VBootMemoryEntry {
    enum VBootMemoryType Type;
    unsigned long long   PhysicalBase;
    unsigned long long   VirtualBase;
    unsigned long long   Length;
    unsigned long long   Attributes;
};

struct VBootMemory {
    unsigned int             NumberOfEntries;
    struct VBootMemoryEntry* Entries;
};

struct VBootVideo {
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
};

struct VBootRamdisk {
    unsigned int Length;
    void*        Data;
};

struct VBoot {
    unsigned int        Magic;
    unsigned int        Version;
    enum VBootFirmware  Firmware;
    void*               ConfigurationTable;
    unsigned int        ConfigurationTableCount;

    struct VBootMemory  Memory;
    struct VBootVideo   Video;
    struct VBootRamdisk Ramdisk;
};

#endif //!__VBOOT_H__
