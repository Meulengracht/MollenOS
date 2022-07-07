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
#include <os/mollenos.h>
#include "pe.h"
#include "process.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct MemoryMappingState {
    MemorySpaceHandle_t Handle;
    uintptr_t           Address;
    size_t              Length;
    unsigned int        Flags;
} MemoryMappingState_t;

static SystemDescriptor_t g_systemInformation = { 0 };
static uintptr_t          g_systemBaseAddress = 0;

/*******************************************************************************
 * Support Methods (PE)
 *******************************************************************************/
static inline oscode_t
__TestFilePath(
        _In_ MString_t* path)
{
    OsFileDescriptor_t fileDescriptor;
    return GetFileInformationFromPath(MStringRaw(path), 1, &fileDescriptor);
}

static oscode_t
__GuessBasePath(
        _In_  uuid_t      processHandle,
        _In_  MString_t*  path,
        _Out_ MString_t** fullPathOut)
{
    // Check the working directory, if it fails iterate the environment defaults
    Process_t* process = PmGetProcessByHandle(processHandle);
    MString_t* result;
    int        isApp;
    int        isDll;

    TRACE("__GuessBasePath(process=%u, path=%s)",
          processHandle, MStringRaw(path));

    // Start by testing against the loaders current working directory,
    // however this won't work for the base process
    if (process != NULL) {
        result = MStringClone(process->working_directory);
        MStringAppendCharacter(result, '/');
        MStringAppend(result, path);
        if (__TestFilePath(result) == OsOK) {
            *fullPathOut = result;
            return OsOK;
        }
    }
    else {
        result = MStringCreate(NULL, StrUTF8);
    }

    // At this point we have to run through all PATH values
    // Look at the type of file we are trying to load. .app? .dll?
    // for other types its most likely resource load
    isApp = MStringFindC(path, ".run");
    isDll = MStringFindC(path, ".dll");
    if (isApp != MSTRING_NOT_FOUND || isDll != MSTRING_NOT_FOUND) {
        MStringReset(result, "$bin/", StrUTF8);
    }
    else {
        MStringReset(result, "$data/", StrUTF8);
    }
    MStringAppend(result, path);
    if (__TestFilePath(result) == OsOK) {
        *fullPathOut = result;
        return OsOK;
    }
    else {
        MStringDestroy(result);
        return OsError;
    }
}

static MString_t*
__TestRamdiskPath(
        _In_ const char* basePath,
        _In_  MString_t* path)
{
    oscode_t osStatus;
    MString_t* temporaryResult;
    TRACE("__TestRamdiskPath(basePath=%s, path=%s)", basePath, MStringRaw(path));

    // create the full path for the ramdisk
    temporaryResult = MStringCreate(basePath, StrUTF8);
    MStringAppend(temporaryResult, path);

    // try to find the file in the ramdisk
    osStatus = PmBootstrapFindRamdiskFile(temporaryResult, NULL, NULL);
    if (osStatus == OsOK) {
        return temporaryResult;
    }
    MStringDestroy(temporaryResult);
    return NULL;
}

static oscode_t
__ResolveRelativePath(
        _In_  uuid_t      processId,
        _In_  MString_t*  parentPath,
        _In_  MString_t*  path,
        _Out_ MString_t** fullPathOut)
{
    oscode_t osStatus        = OsError;
    MString_t* temporaryResult = path;
    TRACE("__ResolveRelativePath(processId=%u, parentPath=%s, path=%s)",
          processId, parentPath ? MStringRaw(parentPath) : "null", MStringRaw(path));

    // Let's test against parent being loaded through the ramdisk
    if (parentPath && MStringFindC(parentPath, "rd:/") != MSTRING_NOT_FOUND) {
        // create the full path for the ramdisk
        temporaryResult = __TestRamdiskPath("rd:/bin/", path);
        if (!temporaryResult) {
            // sometimes additional modules will be loaded (i.e fs modules)
            temporaryResult = __TestRamdiskPath("rd:/modules/", path);
        }

        if (temporaryResult) {
            *fullPathOut = temporaryResult;
            return OsOK;
        }

        // restore temporaryResult
        temporaryResult = path;
    }

    osStatus = __GuessBasePath(processId, path, &temporaryResult);
    TRACE("__ResolveRelativePath basePath=%s", MStringRaw(temporaryResult));

    // If we already deduced an absolute path skip the canonicalizing moment
    if (osStatus == OsOK && MStringFind(temporaryResult, ':', 0) != MSTRING_NOT_FOUND) {
        *fullPathOut = temporaryResult;
        return osStatus;
    }
    return OsNotExists;
}

oscode_t
PeImplResolveFilePath(
        _In_  uuid_t      processId,
        _In_  MString_t*  parentPath,
        _In_  MString_t*  path,
        _Out_ MString_t** fullPathOut)
{
    oscode_t osStatus = OsOK;
    ENTRY("ResolveFilePath(processId=%u, path=%s)", processId, MStringRaw(path));

    if (MStringFind(path, ':', 0) == MSTRING_NOT_FOUND) {
        // If we don't even have an environmental identifier present, we
        // have to get creative and guess away
        osStatus = __ResolveRelativePath(processId, parentPath, path, fullPathOut);
    }
    else {
        // Assume absolute path
        *fullPathOut = MStringClone(path);
    }

    EXIT("ResolveFilePath");
    return osStatus;
}

oscode_t
PeImplLoadFile(
        _In_  MString_t* fullPath,
        _Out_ void**     bufferOut,
        _Out_ size_t*    lengthOut)
{
    FILE*      file;
    long       fileSize;
    void*      fileBuffer;
    size_t     bytesRead;
    oscode_t osStatus = OsOK;
    ENTRY("LoadFile %s", MStringRaw(fullPath));

    // special case:
    // load from ramdisk
    if (MStringFindC(fullPath, "rd:/") != MSTRING_NOT_FOUND) {
        return PmBootstrapFindRamdiskFile(fullPath, bufferOut, lengthOut);
    }

    file = fopen(MStringRaw(fullPath), "rb");
    if (!file) {
        ERROR("LoadFile fopen failed: %i", errno);
        osStatus = OsNotExists;
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
        osStatus = OsOutOfMemory;
        goto exit;
    }

    bytesRead = fread(fileBuffer, 1, fileSize, file);
    fclose(file);

    TRACE("LoadFile read %" PRIuIN " bytes from file", bytesRead);
    if (bytesRead != fileSize) {
        osStatus = OsIncomplete;
    }

    *bufferOut = fileBuffer;
    *lengthOut = fileSize;

exit:
    EXIT("LoadFile");
    return osStatus;
}

void
PeImplUnloadFile(
        _In_ void* buffer)
{
    // When we implement caching we will check if it should stay cached
    free(buffer);
}

uintptr_t
PeImplGetPageSize(void)
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

oscode_t
PeImplCreateImageSpace(
        _Out_ MemorySpaceHandle_t* handleOut)
{
    uuid_t     memorySpaceHandle = UUID_INVALID;
    oscode_t osStatus          = CreateMemorySpace(0, &memorySpaceHandle);
    if (osStatus != OsOK) {
        return osStatus;
    }
    *handleOut = (MemorySpaceHandle_t)(uintptr_t)memorySpaceHandle;
    return OsOK;
}

// Acquires (and creates) a memory mapping in the given memory space handle. The mapping is directly
// accessible in kernel mode, and in usermode a transfer-buffer is transparently provided as proxy.
oscode_t
PeImplAcquireImageMapping(
        _In_    MemorySpaceHandle_t memorySpaceHandle,
        _InOut_ uintptr_t*          address,
        _In_    size_t              length,
        _In_    unsigned int        flags,
        _In_    MemoryMapHandle_t*  handleOut)
{
    MemoryMappingState_t* stateObject = (MemoryMappingState_t*)malloc(sizeof(MemoryMappingState_t));
    oscode_t            osStatus;

    if (!stateObject) {
        return OsOutOfMemory;
    }

    stateObject->Handle  = memorySpaceHandle;
    stateObject->Address = *address;
    stateObject->Length  = length;
    stateObject->Flags   = flags;
    *handleOut = (MemoryMapHandle_t)stateObject;

    // When creating these mappings we must always
    // map in with write flags, and then clear the 'write' flag on release if it was requested
    struct MemoryMappingParameters Parameters;
    Parameters.VirtualAddress = *address;
    Parameters.Length         = length;
    Parameters.Flags          = flags;

    osStatus = CreateMemoryMapping((uuid_t)(uintptr_t)memorySpaceHandle, &Parameters, (void**)&stateObject->Address);
    *address = stateObject->Address;
    if (osStatus != OsOK) {
        free(stateObject);
    }
    return osStatus;
}

// Releases the access previously granted to the mapping, however this is not something
// that is neccessary in kernel mode, so this function does nothing
void
PeImplReleaseImageMapping(
        _In_ MemoryMapHandle_t mapHandle)
{
    MemoryMappingState_t* StateObject = (MemoryMappingState_t*)mapHandle;
    MemoryFree((void*)StateObject->Address, StateObject->Length);
    free(StateObject);
}
