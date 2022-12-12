/**
 * Copyright 2018, Philip Meulengracht
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
 *
 * PE/COFF Image Loader
 *    - Implements support for loading and processing pe/coff image formats
 *      and implemented as a part of libds to share between services and kernel
 */

#define __TRACE

#include <ddk/memory.h>
#include <ddk/utils.h>
#include <ds/mstring.h>
#include <internal/_syscalls.h>
#include <os/services/file.h>
#include <os/memory.h>
#include "pe.h"
#include "process.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static SystemDescriptor_t g_systemInformation = { 0 };
static uintptr_t          g_systemBaseAddress = 0;

oserr_t
PELoadImage(
        _In_  mstring_t* fullPath,
        _Out_ void**     bufferOut,
        _Out_ size_t*    lengthOut)
{
    FILE*   file;
    long    fileSize;
    void*   fileBuffer;
    size_t  bytesRead;
    oserr_t osStatus = OS_EOK;
    char*   pathu8;

    pathu8 = mstr_u8(fullPath);
    ENTRY("LoadFile %s", pathu8);

    // special case:
    // load from ramdisk
    if (mstr_find_u8(fullPath, "/initfs/", 0) != -1) {
        return PmBootstrapFindRamdiskFile(fullPath, bufferOut, lengthOut);
    }

    file = fopen(pathu8, "rb");
    free(pathu8);
    if (!file) {
        ERROR("LoadFile fopen failed: %i", errno);
        osStatus = OS_ENOENT;
        goto exit;
    }

    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    rewind(file);

    TRACE("[load_file] size %" PRIuIN, fileSize);
    fileBuffer = malloc(fileSize);
    if (!fileBuffer) {
        ERROR("LoadFile null");
        fclose(file);
        osStatus = OS_EOOM;
        goto exit;
    }

    bytesRead = fread(fileBuffer, 1, fileSize, file);
    fclose(file);

    TRACE("LoadFile read %" PRIuIN " bytes from file", bytesRead);
    if (bytesRead != fileSize) {
        osStatus = OS_EINCOMPLETE;
    }

    *bufferOut = fileBuffer;
    *lengthOut = fileSize;

exit:
    EXIT("LoadFile");
    return osStatus;
}

uintptr_t
PECurrentPageSize(void)
{
    if (g_systemInformation.PageSizeBytes == 0) {
        SystemQuery(&g_systemInformation);
    }
    return g_systemInformation.PageSizeBytes;
}

uintptr_t
PeImplGetBaseAddress(void)
{
    if (g_systemBaseAddress == 0) {
        Syscall_GetProcessBaseAddress(&g_systemBaseAddress);
    }
    return g_systemBaseAddress;
}

clock_t
PeImplGetTimestampMs(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);

    // return timestamp in ms
    return (ts.tv_sec * MSEC_PER_SEC) + (ts.tv_nsec / NSEC_PER_MSEC);
}

oserr_t
PeImplCreateImageSpace(
        _Out_ uuid_t* handleOut)
{
    uuid_t  memorySpaceHandle = UUID_INVALID;
    oserr_t osStatus          = CreateMemorySpace(0, &memorySpaceHandle);
    if (osStatus != OS_EOK) {
        return osStatus;
    }
    *handleOut = memorySpaceHandle;
    return OS_EOK;
}
