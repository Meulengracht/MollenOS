/**
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
 * Device Manager - Bootstrapper
 * Provides system bootstrap functionality, parses ramdisk for additional system
 * drivers and loads them if any matching device is present.
 */

#define __TRACE

#include <ddk/crc32.h>
#include <ddk/ramdisk.h>
#include <ddk/utils.h>
#include <discover.h>
#include <ds/mstring.h>
#include <stdlib.h>

static OsStatus_t
__ParseRamdiskFile(
        _In_ const uint8_t*         name,
        _In_ RamdiskModuleHeader_t* moduleHeader)
{
    struct DriverIdentification identification;

    MString_t* path;
    uint8_t*   moduleData;
    uint32_t   crcOfData;
    OsStatus_t osStatus;
    TRACE("__ParseRamdiskFile(name=%s)", name);

    // skip services
    if (moduleHeader->Flags & RAMDISK_MODULE_SERVER) {
        return OsSuccess;
    }

    // calculate CRC32 of data
    moduleData = (uint8_t*)((uintptr_t)moduleHeader + sizeof(RamdiskModuleHeader_t));
    crcOfData  = Crc32Calculate(-1, moduleData, moduleHeader->LengthOfData);
    if (crcOfData != moduleHeader->Crc32OfData) {
        ERROR("__ParseRamdiskModule crc validation failed!");
        return OsError;
    }

    path = MStringCreate("rd:/", StrUTF8);
    MStringAppendCharacters(path, (const char*)name, StrUTF8);

    identification.VendorId  = moduleHeader->VendorId;
    identification.ProductId = moduleHeader->DeviceId;
    osStatus = DmDiscoverAddDriver(path, &identification, 1);
    MStringDestroy(path);
    return osStatus;
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

    TRACE("__ParseRamdisk(buffer=0x%" PRIxIN ", size=0x%" PRIxIN ")",
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
        if (entry->Type == RAMDISK_MODULE) {
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

void
DmRamdiskDiscover(void)
{
    OsStatus_t osStatus;
    void*      ramdisk;
    size_t     ramdiskSize;
    TRACE("DmRamdiskDiscover()");

    // Let's map in the ramdisk and discover various service modules
    osStatus = DdkUtilsMapRamdisk(&ramdisk, &ramdiskSize);
    if (osStatus != OsSuccess) {
        TRACE("DmRamdiskDiscover failed to map ramdisk into address space %u", osStatus);
        return;
    }

    // Initialize the CRC32 table
    Crc32GenerateTable();

    osStatus = __ParseRamdisk(ramdisk, ramdiskSize);
    if (osStatus != OsSuccess) {
        ERROR("DmRamdiskDiscover failed to parse ramdisk");
        return;
    }

    // cleanup the memory immediately as we no longer need it
    osStatus = MemoryFree(ramdisk, ramdiskSize);
    if (osStatus != OsSuccess) {
        ERROR("DmRamdiskDiscover failed to free the ramdisk memory");
    }
}
