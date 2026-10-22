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
#include <configparser.h>
#include <errno.h>
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
    TRACE("__ReadFile(filename=%s)", filename);

    // now lets access the file
    status = vafs_directory_reader_open_object_in(directoryHandle, filename, &fileHandle);
    if (status) {
        WARNING("__ReadFile: opening file %s failed: %i (errno %i)", filename, status, errno);
        return OS_ENOENT;
    }

    // Object readers can also represent directories and symlinks. These
    // bootstrap callers require a regular file that fits in addressable memory.
    length = vafs_object_reader_length(fileHandle);
    status = vafs_object_reader_stat(fileHandle, &metadata);
    if (status) {
        ERROR("__ReadFile: stat for file %s failed: %i (errno %i)", filename, status, errno);
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
    TRACE("__ReadFile: file %s has %zu bytes", filename, fileSize);
    
    if (bufferOut == NULL) {
        if (lengthOut != NULL) {
            *lengthOut = fileSize;
        }
        vafs_object_reader_close(fileHandle);
        return OS_EOK;
    }

    fileBuffer = malloc(fileSize ? fileSize : 1);
    if (!fileBuffer) {
        ERROR("__ReadFile: failed to allocate %zu bytes for file %s", fileSize, filename);
        vafs_object_reader_close(fileHandle);
        return OS_EOOM;
    }

    bytesRead = vafs_object_reader_read(fileHandle, fileBuffer, fileSize);
    if (bytesRead != fileSize) {
        ERROR("__ReadFile: file %s read returned %llu of %zu bytes",
              filename, (unsigned long long)bytesRead, fileSize);
        vafs_object_reader_close(fileHandle);
        free(fileBuffer);
        return OS_EUNKNOWN;
    }
    vafs_object_reader_close(fileHandle);

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
    oserr_t                     oserr;
    void*                       buffer;
    size_t                      length;
    TRACE("__ParseModuleConfiguration(name=%s)", name);

    // build the path for the config first
    path = mstr_new_u8(name);
    if (!path) {
        ERROR("__ParseModuleConfiguration: failed to allocate path for %s", name);
        return OS_EOOM;
    }

    // we make an assumption here that .dll exists as that was what triggered this function
    yamlPath = mstr_replace_u8(path, ".dll", ".yaml");
    if (yamlPath == NULL) {
        ERROR("__ParseModuleConfiguration: failed to build configuration path for %s", name);
        mstr_delete(path);
        return OS_EOOM;
    }
    
    yamlPathu8 = mstr_u8(yamlPath);
    if (yamlPathu8 == NULL) {
        ERROR("__ParseModuleConfiguration: failed to encode configuration path for %s", name);
    }

    oserr = __ReadFile(directoryHandle, yamlPathu8, &buffer, &length);
    if (oserr != OS_EOK) {
        WARNING("__ParseModuleConfiguration: reading configuration for %s failed: %u", name, oserr);
        mstr_delete(yamlPath);
        free(yamlPathu8);
        return oserr;
    }
    mstr_delete(yamlPath);
    free(yamlPathu8);

    driverConfig = malloc(sizeof(struct DriverConfiguration));
    if (!driverConfig) {
        ERROR("__ParseModuleConfiguration: failed to allocate configuration for %s", name);
        mstr_delete(path);
        free(buffer);
        return OS_EOOM;
    }

    oserr = DmDriverConfigParseYaml(buffer, length, driverConfig);
    if (oserr != OS_EOK) {
        ERROR("__ParseModuleConfiguration: parsing configuration for %s failed: %u", name, oserr);
        free(buffer);
        mstr_delete(path);
        free(driverConfig);
        return oserr;
    }
    free(buffer);
    mstr_delete(path);

    // now we build the actual path to the file itself
    path = mstr_fmt("/initfs/modules/%s", name);
    if (path == NULL) {
        ERROR("__ParseModuleConfiguration: failed to allocate executable path for %s", name);
        free(driverConfig);
        return OS_EOOM;
    }

    oserr = DmDiscoverAddDriver(path, driverConfig);
    if (oserr != OS_EOK) {
        WARNING("__ParseModuleConfiguration: registering driver %s failed: %u", name, oserr);
    }
    mstr_delete(path);
    return oserr;
}

static oserr_t
__ParseRamdisk(
    _In_ void*  ramdiskBuffer,
    _In_ size_t ramdiskSize)
{
    struct VaFs*                   vafs;
    struct VaFsDirectoryReader*    directoryHandle;
    struct VaFsEntry               entry;
    struct VaFsReaderConfiguration configuration;
    int                            status;
    oserr_t                        oserr = OS_EOK;
    TRACE("__ParseRamdisk(size=%zu)", ramdiskSize);

    DdkInitrdConfigureVafsReader(&configuration);
    status = vafs_reader_open_memory(ramdiskBuffer, ramdiskSize, &configuration, &vafs);
    if (status) {
        ERROR("__ParseRamdisk: opening ramdisk reader failed: %i (errno %i)", status, errno);
        return OS_EUNKNOWN;
    }

    status = vafs_directory_reader_open(vafs, "/modules", VaFsLookup_None, &directoryHandle);
    if (status) {
        WARNING("__ParseRamdisk: opening /modules failed: %i (errno %i)", status, errno);
        status = vafs_reader_close(vafs);
        if (status != 0) {
            WARNING("__ParseRamdisk: closing reader after directory-open failure failed: %i", status);
        }
        return OS_ENOTSUPPORTED;
    }

    for (;;) {
        status = vafs_directory_reader_next(directoryHandle, &entry);
        if (status != 0) {
            if (errno != ENOENT) {
                WARNING("__ParseRamdisk: enumerating /modules failed: %i (errno %i)", status, errno);
            } else {
                TRACE("__ParseRamdisk: reached end of /modules");
            }
            break;
        }
        if (entry.Type != VaFsEntryType_File && entry.Type != VaFsEntryType_Hardlink) {
            continue;
        }

        if (__EndsWith(entry.Name, ".dll")) {
            oserr = __ParseModuleConfiguration(directoryHandle, entry.Name);
            if (oserr != OS_EOK) {
                WARNING("__ParseRamdisk: discovery stopped at module %s: %u", entry.Name, oserr);
                break;
            }
        }
    }

    // close the directory and cleanup
    status = vafs_directory_reader_close(directoryHandle);
    if (status != 0) {
        WARNING("__ParseRamdisk: closing /modules reader failed: %i", status);
    }

    status = vafs_reader_close(vafs);
    if (status != 0) {
        WARNING("__ParseRamdisk: closing ramdisk reader failed: %i", status);
    }
    return oserr;
}

void
DmRamdiskDiscover(void)
{
    oserr_t oserr;
    void*   ramdisk;
    size_t  ramdiskSize;
    TRACE("DmRamdiskDiscover()");

    // Let's map in the ramdisk and discover various service modules
    oserr = DdkUtilsMapRamdisk(&ramdisk, &ramdiskSize);
    if (oserr != OS_EOK) {
        ERROR("DmRamdiskDiscover: failed to map ramdisk into address space: %u", oserr);
        return;
    }

    oserr = __ParseRamdisk(ramdisk, ramdiskSize);
    if (oserr != OS_EOK) {
        ERROR("DmRamdiskDiscover: failed to parse ramdisk: %u", oserr);
        return;
    }

    // cleanup the memory immediately as we no longer need it
    oserr = MemoryFree(ramdisk, ramdiskSize);
    if (oserr != OS_EOK) {
        WARNING("DmRamdiskDiscover: failed to free %zu bytes of ramdisk memory: %u", ramdiskSize, oserr);
    }
}
