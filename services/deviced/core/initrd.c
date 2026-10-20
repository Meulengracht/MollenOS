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

#define __TRACE

#include <ddk/initrd.h>
#include <ddk/utils.h>
#include <discover.h>
#include <ds/mstring.h>
#include <configparser.h>
#include <os/memory.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>
#include <vafs/reader.h>

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

static oserr_t
__ReadFile(
    _In_      struct VaFsDirectoryReader* directoryHandle,
    _In_      const char*                 filename,
    _Out_Opt_ void**                      bufferOut,
    _Out_Opt_ size_t*                     lengthOut)
{
    struct VaFsObjectReader* fileHandle;
    struct VaFsMetadata      metadata;
    uint64_t                 bytesRead;
    size_t                   fileSize;
    void*                    fileBuffer;
    int                      status;
    uint64_t                 length;

    // now lets access the file
    status = vafs_directory_reader_open_object_in(directoryHandle, filename, &fileHandle);
    if (status) {
        ERROR("__ReadFile file %s was not found", filename);
        return OS_ENOENT;
    }

    // Object readers can also represent directories and symlinks. These
    // bootstrap callers require a regular file that fits in addressable memory.
    length = vafs_object_reader_length(fileHandle);
    status = vafs_object_reader_stat(fileHandle, &metadata);
    if (status) {
        ERROR("__ReadFile failed to stat file %s", filename);
        vafs_object_reader_close(fileHandle);
        return OS_EUNKNOWN;
    }

    if (metadata.Type != VaFsEntryType_File && metadata.Type != VaFsEntryType_Hardlink) {
        ERROR("__ReadFile file %s is not a regular file", filename);
        vafs_object_reader_close(fileHandle);
        return OS_EINVALPARAMS;
    }

    if (length > SIZE_MAX) {
        ERROR("__ReadFile file %s is too large", filename);
        vafs_object_reader_close(fileHandle);
        return OS_EINVALPARAMS;
    }

    fileSize = (size_t)length;
    
    if (bufferOut == NULL) {
        if (lengthOut != NULL) {
            *lengthOut = fileSize;
        }
        vafs_object_reader_close(fileHandle);
        return OS_EOK;
    }

    fileBuffer = malloc(fileSize ? fileSize : 1);
    if (!fileBuffer) {
        vafs_object_reader_close(fileHandle);
        return OS_EOOM;
    }

    bytesRead = vafs_object_reader_read(fileHandle, fileBuffer, fileSize);
    vafs_object_reader_close(fileHandle);
    if (bytesRead != fileSize) {
        free(fileBuffer);
        return OS_EUNKNOWN;
    }

    *bufferOut = fileBuffer;
    if (lengthOut != NULL) {
        *lengthOut = fileSize;
    }
    return OS_EOK;
}

static oserr_t
__ParseModuleConfiguration(
    _In_ struct VaFsDirectoryReader* directoryHandle,
    _In_ const char*                 name)
{
    struct DriverConfiguration* driverConfig;
    mstring_t*                  path;
    mstring_t*                  yamlPath;
    char*                       yamlPathu8;
    oserr_t                     osStatus;
    void*                       buffer;
    size_t                      length;
    TRACE("__ParseRamdiskFile(name=%s)", name);

    // build the path for the config first
    path = mstr_new_u8(name);
    if (!path) {
        return OS_EOOM;
    }

    // we make an assumption here that .dll exists as that was what triggered this function
    yamlPath = mstr_replace_u8(path, ".dll", ".yaml");
    yamlPathu8 = mstr_u8(yamlPath);
    osStatus = __ReadFile(directoryHandle, yamlPathu8, &buffer, &length);
    mstr_delete(yamlPath);
    free(yamlPathu8);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    driverConfig = malloc(sizeof(struct DriverConfiguration));
    if (!driverConfig) {
        mstr_delete(path);
        free(buffer);
        return OS_EOOM;
    }

    osStatus = DmDriverConfigParseYaml(buffer, length, driverConfig);
    free(buffer);
    mstr_delete(path);

    if (osStatus != OS_EOK) {
        free(driverConfig);
        return osStatus;
    }

    // now we build the actual path to the file itself
    path = mstr_fmt("/initfs/modules/%s", name);
    if (path == NULL) {
        free(driverConfig);
        return OS_EOOM;
    }

    osStatus = DmDiscoverAddDriver(path, driverConfig);
    mstr_delete(path);
    return osStatus;
}

static oserr_t
__ParseRamdisk(
    _In_ void*  ramdiskBuffer,
    _In_ size_t ramdiskSize)
{
    struct VaFs*                vafs;
    struct VaFsDirectoryReader* directoryHandle;
    struct VaFsEntry            entry;
    int                         status;
    oserr_t                  osStatus = OS_EOK;
    TRACE("__ParseRamdisk()");

    struct VaFsReaderConfiguration configuration;
    DdkInitrdConfigureVafsReader(&configuration);
    status = vafs_reader_open_memory(ramdiskBuffer, ramdiskSize, &configuration, &vafs);
    if (status) {
        return OS_EUNKNOWN;
    }

    status = vafs_directory_reader_open(vafs, "/modules", VaFsLookup_None, &directoryHandle);
    if (status) {
        vafs_reader_close(vafs);
        return OS_ENOTSUPPORTED;
    }

    while (vafs_directory_reader_next(directoryHandle, &entry) == 0) {
        if (entry.Type != VaFsEntryType_File && entry.Type != VaFsEntryType_Hardlink) {
            continue;
        }

        if (__EndsWith(entry.Name, ".dll")) {
            osStatus = __ParseModuleConfiguration(directoryHandle, entry.Name);
            if (osStatus != OS_EOK) {
                break;
            }
        }
    }

    // close the directory and cleanup
    vafs_directory_reader_close(directoryHandle);
    vafs_reader_close(vafs);
    return osStatus;
}

void
DmRamdiskDiscover(void)
{
    oserr_t osStatus;
    void*   ramdisk;
    size_t  ramdiskSize;
    TRACE("DmRamdiskDiscover()");

    // Let's map in the ramdisk and discover various service modules
    osStatus = DdkUtilsMapRamdisk(&ramdisk, &ramdiskSize);
    if (osStatus != OS_EOK) {
        TRACE("DmRamdiskDiscover failed to map ramdisk into address space %u", osStatus);
        return;
    }

    osStatus = __ParseRamdisk(ramdisk, ramdiskSize);
    if (osStatus != OS_EOK) {
        ERROR("DmRamdiskDiscover failed to parse ramdisk");
        return;
    }

    // cleanup the memory immediately as we no longer need it
    osStatus = MemoryFree(ramdisk, ramdiskSize);
    if (osStatus != OS_EOK) {
        ERROR("DmRamdiskDiscover failed to free the ramdisk memory");
    }
}
