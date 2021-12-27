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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * PE/COFF Image Loader
 *    - Implements support for loading and processing pe/coff image formats
 *      and implemented as a part of libds to share between services and kernel
 */

#include <ddk/memory.h>
#include <ds/ds.h>
#include <internal/_syscalls.h>
#include <os/mollenos.h>
#include "pe.h"
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
uintptr_t GetPageSize(void)
{
    if (g_systemInformation.PageSizeBytes == 0) {
        SystemQuery(&g_systemInformation);
    }
    return g_systemInformation.PageSizeBytes;
}

uintptr_t GetBaseAddress(void)
{
    if (g_systemBaseAddress == 0) {
        Syscall_GetProcessBaseAddress(&g_systemBaseAddress);
    }
    return g_systemBaseAddress;
}

clock_t GetTimestamp(void)
{
    return clock();
}

#ifdef LIBC_KERNEL
OsStatus_t ResolveFilePath(UUId_t ProcessId, MString_t* Path, MString_t** FullPath)
{
    MString_t* InitRdPath;

    // Don't care about the uuid
    _CRT_UNUSED(ProcessId);

    // Check if path already contains rd:/
    if (MStringFindCString(Path, "rd:/") == MSTRING_NOT_FOUND) {
        InitRdPath = MStringCreate("rd:/", StrUTF8);
        MStringAppendCharacters(InitRdPath, MStringRaw(Path), StrUTF8);
    }
    else {
        InitRdPath = MStringClone(Path);
    }
    *FullPath = InitRdPath;
    return OsSuccess;
}

OsStatus_t LoadFile(MString_t* FullPath, void** BufferOut, size_t* LengthOut)
{
    return GetModuleDataByPath(FullPath, BufferOut, LengthOut);
}

void UnloadFile(MString_t* FullPath, void* Buffer)
{
    // Do nothing, never free the module buffers
    _CRT_UNUSED(FullPath);
    _CRT_UNUSED(Buffer);
}
#endif

OsStatus_t CreateImageSpace(MemorySpaceHandle_t* HandleOut)
{
    UUId_t     MemorySpaceHandle = UUID_INVALID;
    OsStatus_t Status            = CreateMemorySpace(0, &MemorySpaceHandle);
    if (Status != OsSuccess) {
        return Status;
    }
    *HandleOut = (MemorySpaceHandle_t)(uintptr_t)MemorySpaceHandle;
    return OsSuccess;
}

// Acquires (and creates) a memory mapping in the given memory space handle. The mapping is directly
// accessible in kernel mode, and in usermode a transfer-buffer is transparently provided as proxy.
OsStatus_t AcquireImageMapping(MemorySpaceHandle_t Handle, uintptr_t* Address, size_t Length, unsigned int Flags, MemoryMapHandle_t* HandleOut)
{
    MemoryMappingState_t* StateObject = (MemoryMappingState_t*)dsalloc(sizeof(MemoryMappingState_t));
    OsStatus_t            Status;

    if (!StateObject) {
        return OsOutOfMemory;
    }

    StateObject->Handle  = Handle;
    StateObject->Address = *Address;
    StateObject->Length  = Length;
    StateObject->Flags   = Flags;
    *HandleOut           = (MemoryMapHandle_t)StateObject;

    // When creating these mappings we must always
    // map in with write flags, and then clear the 'write' flag on release if it was requested
    struct MemoryMappingParameters Parameters;
    Parameters.VirtualAddress = *Address;
    Parameters.Length         = Length;
    Parameters.Flags          = Flags;

    Status   = CreateMemoryMapping((UUId_t)(uintptr_t)Handle, &Parameters, (void**)&StateObject->Address);
    *Address = StateObject->Address;
    if (Status != OsSuccess) {
        dsfree(StateObject);
    }
    return Status;
}

// Releases the access previously granted to the mapping, however this is not something
// that is neccessary in kernel mode, so this function does nothing
void ReleaseImageMapping(MemoryMapHandle_t Handle)
{
    MemoryMappingState_t* StateObject = (MemoryMappingState_t*)Handle;
    MemoryFree((void*)StateObject->Address, StateObject->Length);
    dsfree(StateObject);
}
