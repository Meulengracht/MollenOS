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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Device Manager - Bootstrapper
 * Provides system bootstrap functionality, parses ramdisk for additional system
 * drivers and loads them if any matching device is present.
 */

//#define __TRACE

#include <ddk/initrd.h>
#include <ddk/utils.h>
#include <discover.h>
#include <ds/mstring.h>
#include <ramdisk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>

static int
__EndsWith(
        _In_ const char* str,
        _In_ const char* suffix)
{
    size_t lenstr;
    size_t lensuffix;

    if (!str || !suffix){
        return 0;
    }

    lenstr = strlen(str);
    lensuffix = strlen(suffix);
    if (lensuffix >  lenstr) {
        return 0;
    }

    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

static OsStatus_t
__ParseModuleConfiguration(
        _In_ struct VaFsDirectoryHandle* directoryHandle,
        _In_ const char*                 name)
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
    
    path = MStringCreate("rd:/", StrUTF8);
    MStringAppendCharacters(path, (const char*)name, StrUTF8);
    (void)DmParseDriverYaml(NULL, 0);

    identification.VendorId  = moduleHeader->VendorId;
    identification.ProductId = moduleHeader->DeviceId;
    identification.Class     = moduleHeader->DeviceType;
    identification.Subclass  = moduleHeader->DeviceSubType;
    osStatus = DmDiscoverAddDriver(path, &identification, 1);
    MStringDestroy(path);
    return osStatus;
}

static OsStatus_t
__ParseRamdisk(
        _In_ void*  ramdiskBuffer,
        _In_ size_t ramdiskSize)
{
    struct VaFs*                vafs;
    struct VaFsDirectoryHandle* directoryHandle;
    struct VaFsEntry            entry;
    int                         status;
    OsStatus_t                  osStatus = OsSuccess;
    TRACE("__ParseRamdisk()");

    status = vafs_open_memory(ramdiskBuffer, ramdiskSize, &vafs);
    if (status) {
        return OsError;
    }

    status = DdkInitrdHandleVafsFilter(vafs);
    if (status) {
        vafs_close(vafs);
        return OsNotSupported;
    }

    status = vafs_directory_open(vafs, "/modules", &directoryHandle);
    if (status) {
        vafs_close(vafs);
        return OsNotSupported;
    }

    while (vafs_directory_read(directoryHandle, &entry) == 0) {
        if (!__EndsWith(entry.Name, ".dll")) {
            osStatus = __ParseModuleConfiguration(directoryHandle, entry.Name);
            if (osStatus != OsSuccess) {
                break;
            }
        }
    }

    // close the directory and cleanup
    vafs_directory_close(directoryHandle);
    vafs_close(vafs);
    return osStatus;
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
