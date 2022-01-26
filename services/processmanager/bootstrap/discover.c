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

#include <ddk/utils.h>
#include <ds/mstring.h>
#include <ds/list.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe.h"
#include "process.h"
#include <vafs/vafs.h>

extern int __handle_filter(struct VaFs* vafs);

static struct VaFs* g_vafs          = NULL;
static void*        g_ramdiskBuffer = NULL;
static size_t       g_ramdiskSize   = 0;

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
__ParseRamdisk(
        _In_ void*  ramdiskBuffer,
        _In_ size_t ramdiskSize)
{
    struct VaFsDirectoryHandle* directoryHandle;
    struct VaFsEntry            entry;
    int                         status;
    char*                       pathBuffer;
    OsStatus_t                  osStatus;
    ProcessConfiguration_t      processConfiguration;

    status = vafs_open_memory(ramdiskBuffer, ramdiskSize, &g_vafs);
    if (status) {
        return OsError;
    }

    status = __handle_filter(g_vafs);
    if (status) {
        vafs_close(g_vafs);
        return OsNotSupported;
    }

    status = vafs_directory_open(g_vafs, "/services", &directoryHandle);
    if (status) {
        vafs_close(g_vafs);
        return OsNotSupported;
    }

    pathBuffer = malloc(128);
    if (!pathBuffer) {
        vafs_close(g_vafs);
        return OsOutOfMemory;
    }

    ProcessConfigurationInitialize(&processConfiguration);
    while (vafs_directory_read(directoryHandle, &entry) == 0) {
        if (!__EndsWith(entry.Name, ".dll")) {
            UUId_t handle;

            snprintf(pathBuffer, 128-1, "/services/%s", entry.Name);
            osStatus = PmCreateProcessInternal(
                    (const char*)pathBuffer,
                    NULL,
                    NULL,
                    &processConfiguration,
                    NULL,
                    &handle
            );
            if (osStatus != OsSuccess) {
                WARNING("__ParseRamdisk failed to spawn service %s", pathBuffer);
            }
        }
    }

    free(pathBuffer);
    return OsSuccess;
}

void PmBootstrap(void)
{
    OsStatus_t osStatus;
    void*      ramdisk;
    size_t     ramdiskSize;
    TRACE("PmBootstrap()");

    // Let's map in the ramdisk and discover various service modules
    osStatus = DdkUtilsMapRamdisk(&ramdisk, &ramdiskSize);
    if (osStatus != OsSuccess) {
        TRACE("ProcessBootstrap failed to map ramdisk into address space %u", osStatus);
        return;
    }

    // store buffer and size for later cleanup
    g_ramdiskBuffer = ramdisk;
    g_ramdiskSize   = ramdiskSize;

    osStatus = __ParseRamdisk(ramdisk, ramdiskSize);
    if (osStatus != OsSuccess) {
        ERROR("ProcessBootstrap failed to parse ramdisk");
        return;
    }
}

void
PmBootstrapCleanup(void)
{
    OsStatus_t osStatus;

    if (g_ramdiskBuffer && g_ramdiskSize) {
        osStatus = MemoryFree(g_ramdiskBuffer, g_ramdiskSize);
        if (osStatus != OsSuccess) {
            ERROR("PmBootstrapCleanup failed to free the ramdisk memory");
        }
    }
    vafs_close(g_vafs);
}

OsStatus_t
PmBootstrapFindRamdiskFile(
        _In_  MString_t* path,
        _Out_ void**     bufferOut,
        _Out_ size_t*    bufferSizeOut)
{
    TRACE("PmBootstrapFindRamdiskFile(path=%s)", MStringRaw(path));

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
