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

#ifndef __VBOOT_UEFI_H__
#define __VBOOT_UEFI_H__

#include "def.h"

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} __EFI_GUID;

typedef struct {
    __EFI_GUID VendorGuid;
    void*      VendorTable;
} __EFI_CONFIGURATION_TABLE;

static inline int __CompareGuid(__EFI_GUID lh, __EFI_GUID rh)
{
    return (
            lh.Data1    == rh.Data1 &&
            lh.Data2    == rh.Data2 &&
            lh.Data3    == rh.Data3 &&
            lh.Data4[0] == rh.Data4[0] &&
            lh.Data4[1] == rh.Data4[1] &&
            lh.Data4[2] == rh.Data4[2] &&
            lh.Data4[3] == rh.Data4[3] &&
            lh.Data4[4] == rh.Data4[4] &&
            lh.Data4[5] == rh.Data4[5] &&
            lh.Data4[6] == rh.Data4[6] &&
            lh.Data4[7] == rh.Data4[7]);
}

static void* __LocateGuidInConfigurationTable(
        __EFI_CONFIGURATION_TABLE* configurationTable,
        unsigned int               numberOfEntries,
        __EFI_GUID                 guid)
{
    for (unsigned int i = 0; i < numberOfEntries; i++){
        if (__CompareGuid(configurationTable[i].VendorGuid, guid)) {
            return configurationTable[i].VendorTable;
        }
    }
    return NULL;
}


// first 8 per UEFI Specification 2.5
#define EFI_ACPI_20_TABLE_GUID  \
   {0x8868e871, 0xe4f1, 0x11d3, {0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}}
#define ACPI_TABLE_GUID \
   {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}}
#define SAL_SYSTEM_TABLE_GUID \
   {0xeb9d2d32, 0x2d88, 0x11d3, {0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}}
#define SMBIOS_TABLE_GUID \
   {0xeb9d2d31, 0x2d88, 0x11d3, {0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}}
#define SMBIOS3_TABLE_GUID \
   {0xf2fd1544, 0x9794, 0x4a2c, {0x99,0x2e,0xe5,0xbb,0xcf,0x20,0xe3,0x94}}
#define MPS_TABLE_GUID \
   {0xeb9d2d2f, 0x2d88, 0x11d3, {0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}}
#define EFI_PROPERTIES_TABLE_GUID \
   {0x880aaca3, 0x4adc, 0x4a04, {0x90,0x79,0xb7,0x47,0x34,0x8,0x25,0xe5}}
#define EFI_SYSTEM_RESOURCES_TABLE_GUID \
   {0xb122a263, 0x3661, 0x4f68, {0x99,0x29,0x78,0xf8,0xb0,0xd6,0x21,0x80}}
#define EFI_SECTION_TIANO_COMPRESS_GUID \
   {0xa31280ad, 0x481e, 0x41b6, {0x95,0xe8,0x12,0x7f,0x4c,0x98,0x47,0x79}}
#define EFI_SECTION_LZMA_COMPRESS_GUID  \
   {0xee4e5898, 0x3914, 0x4259, {0x9d,0x6e,0xdc,0x7b,0xd7,0x94,0x03,0xcf}}
#define EFI_DXE_SERVICES_TABLE_GUID \
   {0x5ad34ba, 0x6f02, 0x4214, {0x95,0x2e,0x4d,0xa0,0x39,0x8e,0x2b,0xb9}}
#define EFI_HOB_LIST_GUID \
   {0x7739f24c, 0x93d7, 0x11d4, {0x9a,0x3a,0x00,0x90,0x27,0x3f,0xc1,0x4d}}
#define MEMORY_TYPE_INFORMATION_GUID \
   {0x4c19049f, 0x4137, 0x4dd3, {0x9c,0x10,0x8b,0x97,0xa8,0x3f,0xfd,0xfa}}

#endif //!__UTILS_UEFI_H__
