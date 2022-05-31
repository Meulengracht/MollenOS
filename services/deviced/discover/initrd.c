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
        return OsDoesNotExist;
    }

    // allocate a buffer for the file, and read the data
    fileSize = vafs_file_length(fileHandle);
    fileBuffer = malloc(fileSize);
    if (!fileBuffer) {
        return OsError;
    }

    bytesRead = vafs_file_read(fileHandle, fileBuffer, fileSize);
    if (bytesRead != fileSize) {
        WARNING("__ReadFile read %" PRIuIN "/%" PRIuIN " bytes from file", bytesRead, fileSize);
    }

    vafs_file_close(fileHandle);
    *bufferOut = fileBuffer;
    *lengthOut = fileSize;
    return OsSuccess;
}

static OsStatus_t
__ParseModuleConfiguration(
        _In_ struct VaFsDirectoryHandle* directoryHandle,
        _In_ const char*                 name)
{
    struct DriverConfiguration* driverConfig;
    MString_t*           path;
    OsStatus_t           osStatus;
    void*                buffer;
    size_t               length;
    TRACE("__ParseRamdiskFile(name=%s)", name);

    // build the path for the config first
    path = MStringCreate(name, StrUTF8);
    if (!path) {
        return OsOutOfMemory;
    }

    // we make an assumption here that .dll exists as that was what triggered this function
    (void)MStringReplaceC(path, ".dll", ".yaml");
    osStatus = __ReadFile(directoryHandle, MStringRaw(path), &buffer, &length);
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    driverConfig = malloc(sizeof(struct DriverConfiguration));
    if (!driverConfig) {
        MStringDestroy(path);
        free(buffer);
        return OsOutOfMemory;
    }

    osStatus = DmDriverConfigParseYaml(buffer, length, driverConfig);
    free(buffer);

    if (osStatus != OsSuccess) {
        MStringDestroy(path);
        free(driverConfig);
        return osStatus;
    }

    // now we build the actual path to the file itself
    MStringReset(path, "rd:/modules/", StrUTF8);
    MStringAppendCharacters(path, (const char*)name, StrUTF8);

    osStatus = DmDiscoverAddDriver(path, driverConfig);
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
        if (__EndsWith(entry.Name, ".dll")) {
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
