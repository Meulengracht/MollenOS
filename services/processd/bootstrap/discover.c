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
 * Process Manager - Bootstrapper
 * Provides system bootstrap functionality, parses ramdisk for additional system
 * services and boots them.
 */

#define __TRACE

#include <ddk/initrd.h>
#include <ddk/utils.h>
#include <ds/mstring.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe.h"
#include "process.h"
#include <vafs/vafs.h>
#include <vafs/directory.h>
#include <vafs/file.h>

static struct VaFs* g_vafs          = NULL;
static void*        g_ramdiskBuffer = NULL;
static size_t       g_ramdiskSize   = 0;

static int
__EndsWith(
        _In_ const char* text,
        _In_ const char* suffix)
{
    size_t lengthOfText;
    size_t lengthOfSuffix;

    if (!text || !suffix){
        return 0;
    }

    lengthOfText   = strlen(text);
    lengthOfSuffix = strlen(suffix);
    if (lengthOfSuffix > lengthOfText) {
        return 0;
    }

    return strncmp(text + lengthOfText - lengthOfSuffix, suffix, lengthOfSuffix);
}

static oscode_t
__ParseRamdisk(
        _In_ void*  ramdiskBuffer,
        _In_ size_t ramdiskSize)
{
    struct VaFsDirectoryHandle* directoryHandle;
    struct VaFsEntry            entry;
    int                         status;
    char*                       pathBuffer;
    oscode_t                  osStatus;
    ProcessConfiguration_t      processConfiguration;
    TRACE("__ParseRamdisk()");

    status = vafs_open_memory(ramdiskBuffer, ramdiskSize, &g_vafs);
    if (status) {
        ERROR("__ParseRamdisk failed to open vafs image");
        return OsError;
    }

    status = DdkInitrdHandleVafsFilter(g_vafs);
    if (status) {
        ERROR("__ParseRamdisk vafs image is using an unsupported filter");
        vafs_close(g_vafs);
        return OsNotSupported;
    }

    status = vafs_directory_open(g_vafs, "/services", &directoryHandle);
    if (status) {
        ERROR("__ParseRamdisk failed to open /services in vafs image");
        vafs_close(g_vafs);
        return OsNotSupported;
    }

    pathBuffer = malloc(128);
    if (!pathBuffer) {
        ERROR("__ParseRamdisk out of memory");
        vafs_directory_close(directoryHandle);
        vafs_close(g_vafs);
        return OsOutOfMemory;
    }

    ProcessConfigurationInitialize(&processConfiguration);
    while (vafs_directory_read(directoryHandle, &entry) == 0) {
        TRACE("__ParseRamdisk found entry %s", entry.Name);
        if (entry.Type != VaFsEntryType_File) {
            continue;
        }

        if (!__EndsWith(entry.Name, ".dll")) {
            UUId_t handle;

            snprintf(pathBuffer, 128-1, "rd:/services/%s", entry.Name);
            TRACE("__ParseRamdisk file found: %s", &pathBuffer[0]);
            osStatus = PmCreateProcessInternal(
                    (const char*)pathBuffer,
                    NULL,
                    NULL,
                    &processConfiguration,
                    NULL,
                    &handle
            );
            if (osStatus != OsOK) {
                WARNING("__ParseRamdisk failed to spawn service %s", pathBuffer);
            }
        }
    }

    // close the directory and cleanup
    vafs_directory_close(directoryHandle);
    free(pathBuffer);
    return OsOK;
}

void PmBootstrap(void)
{
    oscode_t osStatus;
    void*      ramdisk;
    size_t     ramdiskSize;
    TRACE("PmBootstrap()");

    // Let's map in the ramdisk and discover various service modules
    osStatus = DdkUtilsMapRamdisk(&ramdisk, &ramdiskSize);
    if (osStatus != OsOK) {
        TRACE("ProcessBootstrap failed to map ramdisk into address space %u", osStatus);
        return;
    }

    // store buffer and size for later cleanup
    g_ramdiskBuffer = ramdisk;
    g_ramdiskSize   = ramdiskSize;

    osStatus = __ParseRamdisk(ramdisk, ramdiskSize);
    if (osStatus != OsOK) {
        ERROR("ProcessBootstrap failed to parse ramdisk");
        return;
    }
}

void
PmBootstrapCleanup(void)
{
    oscode_t osStatus;

    // close the vafs handle before freeing the buffer
    vafs_close(g_vafs);

    if (g_ramdiskBuffer && g_ramdiskSize) {
        osStatus = MemoryFree(g_ramdiskBuffer, g_ramdiskSize);
        if (osStatus != OsOK) {
            ERROR("PmBootstrapCleanup failed to free the ramdisk memory");
        }
    }
}

oscode_t
PmBootstrapFindRamdiskFile(
        _In_  MString_t* path,
        _Out_ void**     bufferOut,
        _Out_ size_t*    bufferSizeOut)
{
    struct VaFsDirectoryHandle* directoryHandle;
    struct VaFsFileHandle*      fileHandle;
    char*                       internPath;
    char*                       internFilename;
    int                         status;
    char                        tempbuf[64] = { 0 };

    TRACE("PmBootstrapFindRamdiskFile(path=%s)", MStringRaw(path));

    // skip the rd:/ prefix
    internPath = strchr(MStringRaw(path), '/') + 1;
    internFilename = strrchr(MStringRaw(path), '/');

    // Ok, so max out at len(tempbuf) - 1, but minimum 1 char to include the initial '/'
    strncpy(
            &tempbuf[0],
            internPath,
            MAX(1, MIN(internFilename - internPath, sizeof(tempbuf) - 1))
    );

    // skip the starting '/'
    internFilename++;

    status = vafs_directory_open(g_vafs, &tempbuf[0], &directoryHandle);
    if (status) {
        ERROR("PmBootstrapFindRamdiskFile failed to open %s, corrupt image buffer", &tempbuf[0]);
        return OsError;
    }

    // now lets access the file
    status = vafs_directory_open_file(directoryHandle, internFilename, &fileHandle);
    if (status) {
        WARNING("PmBootstrapFindRamdiskFile file %s was not found", internFilename);
        return OsNotExists;
    }

    // allocate a buffer for the file, and read the data
    if (bufferOut && bufferSizeOut)
    {
        size_t bytesRead;
        size_t fileSize = vafs_file_length(fileHandle);
        void*  fileBuffer = malloc(fileSize);
        if (!fileBuffer) {
            return OsError;
        }

        bytesRead = vafs_file_read(fileHandle, fileBuffer, fileSize);
        if (bytesRead != fileSize) {
            WARNING("PmBootstrapFindRamdiskFile read %" PRIuIN "/%" PRIuIN " bytes from file", bytesRead, fileSize);
        }

        *bufferOut = fileBuffer;
        *bufferSizeOut = fileSize;
    }

    // close the file and directory handle
    vafs_file_close(fileHandle);
    vafs_directory_close(directoryHandle);

    return OsOK;
}
