/**
 * MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 * Process Manager - Bootstrapper
 * Provides system bootstrap functionality, parses ramdisk for additional system
 * services and boots them.
 */

#define __TRACE

#include <ddk/crc32.h>
#include <ddk/ramdisk.h>
#include <ddk/utils.h>
#include <ds/mstring.h>
#include <ds/list.h>
#include <stdlib.h>
#include "pe.h"
#include "process.h"

struct RamdiskFile {
    element_t   ListHeader;
    MString_t*  Name;
    const void* Data;
    size_t      DataLength;
    int         IsService;
};

static list_t g_ramdiskFiles = LIST_INIT;

static OsStatus_t
__ParseRamdiskFile(
        _In_ const uint8_t*         name,
        _In_ RamdiskModuleHeader_t* moduleHeader)
{
    struct RamdiskFile* ramdiskFile;
    uint8_t*            moduleData;
    uint32_t            crcOfData;

    // calculate CRC32 of data
    moduleData = (uint8_t*)((uintptr_t)moduleHeader + sizeof(RamdiskModuleHeader_t));
    crcOfData  = Crc32Calculate(-1, moduleData, moduleHeader->LengthOfData);
    if (crcOfData != moduleHeader->Crc32OfData) {
        ERROR("__ParseRamdiskModule crc validation failed!");
        return OsError;
    }

    // we found a file
    ramdiskFile = malloc(sizeof(struct RamdiskFile));
    if (!ramdiskFile) {
        return OsOutOfMemory;
    }

    ELEMENT_INIT(&ramdiskFile->ListHeader, 0, ramdiskFile);

    ramdiskFile->Name = MStringCreate("rd:/", StrUTF8);
    MStringAppendCharacters(ramdiskFile->Name, (const char*)name, StrUTF8);

    ramdiskFile->Data       = moduleData;
    ramdiskFile->DataLength = moduleHeader->LengthOfData;
    ramdiskFile->IsService  = (moduleHeader->Flags & RAMDISK_MODULE_SERVER) != 0 ? 1 : 0;
    list_append(&g_ramdiskFiles, &ramdiskFile->ListHeader);
    return OsSuccess;
}

static OsStatus_t
__ParseRamdisk(
        _In_ void*  ramdiskBuffer,
        _In_ size_t ramdiskSize)
{
    RamdiskHeader_t* header;
    RamdiskEntry_t*  entry;
    OsStatus_t       osStatus;
    int              i;

    TRACE("__ParseRamdisk(buffer=0x%" PRIxIN ", size=0x%" PRIxIN,
          ramdiskBuffer, ramdiskSize);

    header = ramdiskBuffer;
    if (header->Magic != RAMDISK_MAGIC) {
        ERROR("__ParseRamdisk invalid header magic");
        return OsError;
    }

    if (header->Version != RAMDISK_VERSION_1) {
        ERROR("__ParseRamdisk invalid header version");
        return OsError;
    }

    entry = (RamdiskEntry_t*)((uintptr_t)ramdiskBuffer + sizeof(RamdiskHeader_t));
    for (i = 0; i < header->FileCount; i++, entry++) {
        if (entry->Type == RAMDISK_MODULE || entry->Type == RAMDISK_FILE) {
            RamdiskModuleHeader_t* moduleHeader =
                    (RamdiskModuleHeader_t*)((uintptr_t)ramdiskBuffer + entry->DataHeaderOffset);
            osStatus = __ParseRamdiskFile(&entry->Name[0], moduleHeader);
            if (osStatus != OsSuccess) {
                return osStatus;
            }
        }
    }
    return OsSuccess;
}

static void
__SpawnServices(void)
{
    UUId_t                 handle;
    ProcessConfiguration_t processConfiguration;
    ProcessConfigurationInitialize(&processConfiguration);

    foreach (i, &g_ramdiskFiles) {
        struct RamdiskFile* file = i->value;
        if (file->IsService) {
            OsStatus_t osStatus = PmCreateProcessInternal(
                    MStringRaw(file->Name),
                    NULL,
                    NULL,
                    &processConfiguration,
                    NULL,
                    &handle
            );
            if (osStatus != OsSuccess) {
                WARNING("__SpawnServices failed to spawn service %s", MStringRaw(file->Name));
            }
        }
    }
}

void PmBootstrap(void)
{
    OsStatus_t osStatus;
    void*      ramdisk;
    size_t     ramdiskSize;

    // Let's map in the ramdisk and discover various service modules
    osStatus = DdkUtilsMapRamdisk(&ramdisk, &ramdiskSize);
    if (osStatus != OsSuccess) {
        TRACE("ProcessBootstrap failed to map ramdisk into address space %u", osStatus);
        return;
    }

    // Initialize the CRC32 table
    Crc32GenerateTable();

    osStatus = __ParseRamdisk(ramdisk, ramdiskSize);
    if (osStatus != OsSuccess) {
        ERROR("ProcessBootstrap failed to parse ramdisk");
        return;
    }

    __SpawnServices();

    osStatus = MemoryFree(ramdisk, ramdiskSize);
    if (osStatus != OsSuccess) {
        ERROR("ProcessBootstrap failed to free the ramdisk memory");
    }
}

OsStatus_t
PmBootstrapFindRamdiskFile(
        _In_  MString_t* path,
        _Out_ void**     bufferOut,
        _Out_ size_t*    bufferSizeOut)
{
    foreach (i, &g_ramdiskFiles) {
        struct RamdiskFile* file = i->value;
        if (MStringCompare(file->Name, path, 0) == MSTRING_FULL_MATCH) {
            if (bufferOut) *bufferOut = (void*)file->Data;
            if (bufferSizeOut) *bufferSizeOut = file->DataLength;
            return OsSuccess;
        }
    }
    return OsDoesNotExist;
}
