/**
 * Copyright 2022, Philip Meulengracht
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
 */

//#define __TRACE
#include <ddk/utils.h>
#include "private.h"
#include <module.h>

PACKED_TYPESTRUCT(RuntimeRelocationHeader, {
    uint32_t Magic0;
    uint32_t Magic1;
    uint32_t Version;
});

PACKED_TYPESTRUCT(RuntimeRelocationEntryV1, {
    uint32_t Value;
    uint32_t RVA;
});

PACKED_TYPESTRUCT(RuntimeRelocationEntryV2, {
    uint32_t SymbolRVA;
    uint32_t OffsetRVA;
    uint32_t Flags;
});

#define RP_VERSION_1 0
#define RP_VERSION_2 1

static oserr_t
__HandleRelocationsV1(
        _In_ struct ModuleMapping* moduleMapping,
        _In_ void*                 data,
        _In_ size_t                dataSize)
{
    RuntimeRelocationEntryV1_t* entries = data;
    size_t                      count   = dataSize / sizeof(RuntimeRelocationEntryV1_t);
    TRACE("__HandleRelocationsV1()");

    for (size_t i = 0; i < count; i++) {
        uintptr_t* address = SectionMappingFromRVA(
                moduleMapping->Mappings,
                moduleMapping->MappingCount,
                entries[i].RVA
        );
        if (address == NULL) {
            return OS_ENOENT;
        }
        *address += entries[i].Value;
    }
    return OS_EOK;
}

static oserr_t
__HandleRelocationsV2(
        _In_ struct ModuleMapping* moduleMapping,
        _In_ void*                 data,
        _In_ size_t                dataSize)
{
    RuntimeRelocationEntryV2_t* entries = data;
    size_t                      count   = dataSize / sizeof(RuntimeRelocationEntryV2_t);
    TRACE("__HandleRelocationsV2()");

    for (size_t i = 0; i < count; i++) {
        void*    symbolSectionAddress;
        void*    targetSectionAddress;
        intptr_t symbolValue;
        uint8_t  relocSize;
        intptr_t relocData;

        symbolSectionAddress = SectionMappingFromRVA(
                moduleMapping->Mappings,
                moduleMapping->MappingCount,
                entries[i].SymbolRVA
        );
        if (symbolSectionAddress == NULL) {
            return OS_ENOENT;
        }

        targetSectionAddress = SectionMappingFromRVA(
                moduleMapping->Mappings,
                moduleMapping->MappingCount,
                entries[i].OffsetRVA
        );
        if (targetSectionAddress == NULL) {
            return OS_ENOENT;
        }

        symbolValue = *((intptr_t*)symbolSectionAddress);
        relocSize = (uint8_t)(entries[i].Flags & 0xFF);
        switch (relocSize) {
            case 8: {
                relocData = (intptr_t)*((uint8_t*)targetSectionAddress);
                if (relocData & 0x80) {
                    relocData |= ~((intptr_t) 0xFF);
                }
            } break;
            case 16: {
                relocData = (intptr_t)*((uint16_t*)targetSectionAddress);
                if (relocData & 0x8000) {
                    relocData |= ~((intptr_t) 0xFFFF);
                }
            } break;
            case 32: {
                relocData = (intptr_t)*((uint32_t*)targetSectionAddress);
#if defined(__amd64__)
                if (relocData & 0x80000000) {
                    relocData |= ~((intptr_t) 0xFFFFFFFF);
                }
#endif
            } break;
#if defined(__amd64__)
            case 64: {
                relocData = (intptr_t)*((uint64_t*)targetSectionAddress);
            } break;
#endif
            default: {
                ERROR("__HandleRelocationsV2 invalid relocation size %u", relocSize);
                return OS_EUNKNOWN;
            }
        }

        TRACE("__HandleRelocationsV2 relocData=0x%" PRIxIN ", mappedBase=0x%" PRIxIN,
              relocData, moduleMapping->MappingBase);
        relocData -= ((intptr_t)moduleMapping->MappingBase + entries[i].SymbolRVA);
        relocData += symbolValue;
        TRACE("__HandleRelocationsV2 relocData=0x%" PRIxIN, relocData);

        switch (relocSize) {
            case 8: *((uint8_t*)targetSectionAddress) = (uint8_t)((uintptr_t)relocData & 0xFF); break;
            case 16: *((uint16_t*)targetSectionAddress) = (uint16_t)((uintptr_t)relocData & 0xFFFF); break;
            case 32: *((uint32_t*)targetSectionAddress) = (uint32_t)((uintptr_t)relocData & 0xFFFFFFFF); break;
#if defined(__amd64__)
            case 64: *((uint64_t*)targetSectionAddress) = (uint64_t)relocData; break;
#endif
            default: break;
        }
    }
    return OS_EOK;
}

oserr_t
PERuntimeRelocationsProcess(
        _In_ struct ModuleMapping* moduleMapping)
{
    RuntimeRelocationHeader_t* header;
    PeDataDirectory_t* directories = ModuleDataDirectories(moduleMapping->Module);
    uint32_t rva  = directories[PE_SECTION_GLOBAL_PTR].AddressRVA;
    uint32_t size = directories[PE_SECTION_GLOBAL_PTR].Size;
    TRACE("PERuntimeRelocationsProcess()");

    if (rva == 0 || size == 0) {
        // no runtime relocations
        return OS_EOK;
    }

    // the directory must be large enough to have the correct header
    if (size < 8) {
        return OS_EUNKNOWN;
    }

    header = SectionMappingFromRVA(
            moduleMapping->Mappings,
            moduleMapping->MappingCount,
            rva
    );
    if (header == NULL) {
        return OS_EUNKNOWN;
    }

    // Determine if the relocation header is actually present, otherwise
    // we are looking at version 1 of relocation entries.
    if (size >= sizeof(RuntimeRelocationHeader_t) && header->Magic0 == 0 && header->Magic1 == 0) {
        // header is there, adjust for entries
        void* entries = ((uint8_t*)header + sizeof(RuntimeRelocationHeader_t));
        size -= sizeof(RuntimeRelocationHeader_t);

        if (header->Version == RP_VERSION_1) {
            return __HandleRelocationsV1(
                    moduleMapping,
                    entries,
                    size
            );
        } else if (header->Version == RP_VERSION_2) {
            return __HandleRelocationsV2(
                    moduleMapping,
                    entries,
                    size
            );
        } else {
            ERROR("PERuntimeRelocationsProcess unsupported RT reloc version %u", header->Version);
            return OS_ENOTSUPPORTED;
        }
    }
    return __HandleRelocationsV1(
            moduleMapping,
            header,
            size
    );
}
