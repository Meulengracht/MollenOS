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
#include <vafs/directory.h>
#include <vafs/file.h>

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
        _In_  struct VaFsDirectoryHandle* directoryHandle,
        _In_  const char*                 filename,
        _Out_ void**                      bufferOut,
        _Out_ size_t*                     lengthOut)
{
    struct VaFsFileHandle* fileHandle;
    size_t                 bytesRead;
    size_t                 fileSize;
    void*                  fileBuffer;
    int                    status;

    // now lets access the file
    status = vafs_directory_open_file(directoryHandle, filename, &fileHandle);
    if (status) {
        ERROR("__ReadFile file %s was not found", filename);
        return OS_ENOENT;
    }

    // allocate a buffer for the file, and read the data
    fileSize = vafs_file_length(fileHandle);
    fileBuffer = malloc(fileSize);
    if (!fileBuffer) {
        return OS_EUNKNOWN;
    }

    bytesRead = vafs_file_read(fileHandle, fileBuffer, fileSize);
    if (bytesRead != fileSize) {
        WARNING("__ReadFile read %" PRIuIN "/%" PRIuIN " bytes from file", bytesRead, fileSize);
    }

    vafs_file_close(fileHandle);
    *bufferOut = fileBuffer;
    *lengthOut = fileSize;
    return OS_EOK;
}

static oserr_t
__ParseModuleConfiguration(
        _In_ struct VaFsDirectoryHandle* directoryHandle,
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
    struct VaFsDirectoryHandle* directoryHandle;
    struct VaFsEntry            entry;
    int                         status;
    oserr_t                  osStatus = OS_EOK;
    TRACE("__ParseRamdisk()");

    status = vafs_open_memory(ramdiskBuffer, ramdiskSize, &vafs);
    if (status) {
        return OS_EUNKNOWN;
    }

    status = DdkInitrdHandleVafsFilter(vafs);
    if (status) {
        vafs_close(vafs);
        return OS_ENOTSUPPORTED;
    }

    status = vafs_directory_open(vafs, "/modules", &directoryHandle);
    if (status) {
        vafs_close(vafs);
        return OS_ENOTSUPPORTED;
    }

    while (vafs_directory_read(directoryHandle, &entry) == 0) {
        if (entry.Type != VaFsEntryType_File) {
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
    vafs_directory_close(directoryHandle);
    vafs_close(vafs);
    return osStatus;
}

void
DmRamdiskDiscover(void)
{
    oserr_t osStatus;
    void*      ramdisk;
    size_t     ramdiskSize;
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
