/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 *
 * MollenOS X86 SMBIOS Driver
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_2.8.0.pdf
 */

#ifndef __SMBIOS_DRIVER__
#define __SMBIOS_DRIVER__

/* Includes
 * - Library */
#include <os/osdefs.h>

#define SMBIOS_SIGNATURE                            0x5F534D5F
#define SMBIOS_ANCHOR_STRING                        "_DMI_"
#define SMBIOS_DEFAULT_MEMORY_LOCATION_START        0xF0000
#define SMBIOS_DEFAULT_MEMORY_LOCATION_END          0xFFFFF

#define SMBIOS_STRUCTURE_TYPE_BIOS_INFORMATION      0
#define SMBIOS_STRUCTURE_TYPE_SYSTEM_INFORMATION    1
#define SMBIOS_STRUCTURE_TYPE_SYSTEM_ENCLOSURE      3
#define SMBIOS_STRUCTURE_TYPE_CPU_INFORMATION       4
#define SMBIOS_STRUCTURE_TYPE_CACHE_INFORMATION     7
#define SMBIOS_STRUCTURE_TYPE_SYSTEM_SLOTS          9
#define SMBIOS_STRUCTURE_TYPE_PHYSICAL_MEMORY_ARRAY 16  // How physical memory is layed out
#define SMBIOS_STRUCTURE_TYPE_MEMORY_DEVICE         17
#define SMBIOS_STRUCTURE_TYPE_PHYSICAL_MEMORY_MAP   19  // How memory regions map to physical arrays
#define SMBIOS_STRUCTURE_TYPE_BOOT_INFORMATION      32

PACKED_TYPESTRUCT(SmBiosTable, {
    uint32_t            Signature;
    uint8_t             Checksum;
    uint8_t             TableLength;

    uint8_t             VersionMajor;
    uint8_t             VersionMinor;
    uint16_t            LargestTableSize;
    uint8_t             Revision;
    uint8_t             FormattedArea[5];   // Determined by revision, but reserved if value is 0

    // Intermediate area
    uint8_t             AnchorString[5];    // _DMI_
    uint8_t             IntermediateChecksum;
    uint16_t            StructureLength;
    uint32_t            StructureAddress;
    uint16_t            NumberOfStructures;
    uint8_t             BcdRevision;
});

PACKED_TYPESTRUCT(SmBiosStructureHeader, {
    uint8_t             Type;
    uint8_t             Length;
    uint16_t            Handle; // Unique 16 bit id.
});

/**
 * Initializes and finds if the smbios table is present on the system. The 
 * function will return OsOK if the table is present and everything is alright.
 */
KERNELAPI OsStatus_t KERNELABI
SmBiosInitialize(void);

#endif // !__SMBIOS_DRIVER__
