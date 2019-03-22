/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * MollenOS - Generic Data Structures (Shared)
 */

#include <os/mollenos.h>
#include <ds/mstring.h>
#include <ds/ds.h>
#include "../pe/pe.h"
#include <string.h>

#define __MODULE "DATA"
#ifdef LIBC_KERNEL
#define __TRACE
#include <system/interrupts.h>
#include <modules/manager.h>
#include <memoryspace.h>
#include <machine.h>
#include <timers.h>
#include <stdio.h>
#include <debug.h>
#include <heap.h>
#else
#define __TRACE
#include <internal/_syscalls.h>
#include <ddk/buffer.h>
#include <ddk/memory.h>
#include <ddk/utils.h>
#include <ddk/services/file.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static SystemDescriptor_t __SystemInformation = { 0 };
static uintptr_t          __SystemBaseAddress = 0;
#endif

typedef struct _MemoryMappingState {
    MemorySpaceHandle_t Handle;
    uintptr_t           Address;
    size_t              Length;
    Flags_t             Flags;
} MemoryMappingState_t;

/*******************************************************************************
 * Support Methods (DS)
 *******************************************************************************/
void* dsalloc(size_t size)
{
#ifdef LIBC_KERNEL
	return kmalloc(size);
#else
	return malloc(size);
#endif
}

void dsfree(void* pointer)
{
#ifdef LIBC_KERNEL
	kfree(pointer);
#else
	free(pointer);
#endif
}

void dslock(SafeMemoryLock_t* lock)
{
    bool locked = true;

#ifdef LIBC_KERNEL
    lock->Flags = InterruptDisable();
#endif
    while (1) {
        bool val = atomic_exchange(&lock->SyncObject, locked);
        if (val == false) {
            break;
        }
    }
}

void dsunlock(SafeMemoryLock_t* lock)
{
    atomic_exchange(&lock->SyncObject, false);
#ifdef LIBC_KERNEL
    InterruptRestoreState(lock->Flags);
#endif
}

void dstrace(const char* fmt, ...)
{
    char Buffer[256] = { 0 };
    va_list Arguments;

    va_start(Arguments, fmt);
    vsnprintf(&Buffer[0], sizeof(Buffer), fmt, Arguments);
    va_end(Arguments);
    TRACE(&Buffer[0]);
}

void dswarning(const char* fmt, ...)
{
    char Buffer[256] = { 0 };
    va_list Arguments;

    va_start(Arguments, fmt);
    vsnprintf(&Buffer[0], sizeof(Buffer), fmt, Arguments);
    va_end(Arguments);
    WARNING(&Buffer[0]);
}

void dserror(const char* fmt, ...)
{
    char Buffer[256] = { 0 };
    va_list Arguments;

    va_start(Arguments, fmt);
    vsnprintf(&Buffer[0], sizeof(Buffer), fmt, Arguments);
    va_end(Arguments);
    ERROR(&Buffer[0]);
}

int dsmatchkey(KeyType_t type, DataKey_t key1, DataKey_t key2)
{
	switch (type) {
        case KeyId: {
			if (key1.Value.Id == key2.Value.Id) {
                return 0;
            }
        } break;
		case KeyInteger: {
			if (key1.Value.Integer == key2.Value.Integer) {
                return 0;
            }
		} break;
		case KeyString: {
			return strcmp(key1.Value.String.Pointer, key2.Value.String.Pointer);
		} break;
	}
	return -1;
}

int dssortkey(KeyType_t type, DataKey_t key1, DataKey_t key2)
{
	switch (type) {
        case KeyId: {
			if (key1.Value.Id == key2.Value.Id)
				return 0;
			else if (key1.Value.Id > key2.Value.Id)
				return 1;
			else
				return -1;
        } break;
		case KeyInteger: {
			if (key1.Value.Integer == key2.Value.Integer)
				return 0;
			else if (key1.Value.Integer > key2.Value.Integer)
				return 1;
			else
				return -1;
		} break;
		case KeyString: {
			return strcmp(key1.Value.String.Pointer, key2.Value.String.Pointer);
		} break;
	}
	return 0;
}

/*******************************************************************************
 * Support Methods (PE)
 *******************************************************************************/
uintptr_t GetPageSize(void)
{
#ifdef LIBC_KERNEL
    return GetMemorySpacePageSize();
#else
    if (__SystemInformation.PageSizeBytes == 0) {
        SystemQuery(&__SystemInformation);
    }
    return __SystemInformation.PageSizeBytes;
#endif
}

uintptr_t GetBaseAddress(void)
{
#ifdef LIBC_KERNEL
    return GetMachine()->MemoryMap.UserCode.Start;
#else
    if (__SystemBaseAddress == 0) {
        Syscall_GetProcessBaseAddress(&__SystemBaseAddress);
    }
    return __SystemBaseAddress;
#endif
}

clock_t GetTimestamp(void)
{
    clock_t Result;
#ifdef LIBC_KERNEL
    TimersGetSystemTick(&Result);
#else
    Result = clock();
#endif
    return Result;
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
#ifdef LIBC_KERNEL
    *HandleOut = (MemorySpaceHandle_t)GetCurrentMemorySpace();
#else
    UUId_t     MemorySpaceHandle = UUID_INVALID;
    OsStatus_t Status            = CreateMemorySpace(0, &MemorySpaceHandle);
    if (Status != OsSuccess) {
        return Status;
    }
    *HandleOut = (MemorySpaceHandle_t)MemorySpaceHandle;
#endif
    return OsSuccess;
}

// Acquires (and creates) a memory mapping in the given memory space handle. The mapping is directly 
// accessible in kernel mode, and in usermode a transfer-buffer is transparently provided as proxy.
OsStatus_t AcquireImageMapping(MemorySpaceHandle_t Handle, uintptr_t* Address, size_t Length, Flags_t Flags, MemoryMapHandle_t* HandleOut)
{
    MemoryMappingState_t* StateObject = (MemoryMappingState_t*)dsalloc(sizeof(MemoryMappingState_t));
    OsStatus_t            Status;

    StateObject->Handle  = Handle;
    StateObject->Address = *Address;
    StateObject->Length  = Length;
    StateObject->Flags   = Flags;
    *HandleOut           = (MemoryMapHandle_t)StateObject;

    // When creating these mappings we must always
    // map in with write flags, and then clear the write flag on release if it was requested
#ifdef LIBC_KERNEL
    // Translate memory flags to kernel flags
    Flags_t KernelFlags    = MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_DOMAIN;
    Flags_t PlacementFlags = MAPPING_PHYSICAL_DEFAULT | MAPPING_VIRTUAL_FIXED;
    if (Flags & MEMORY_EXECUTABLE) {
        KernelFlags |= MAPPING_EXECUTABLE;
    }
    Status = CreateMemorySpaceMapping((SystemMemorySpace_t*)Handle, NULL, Address, Length, 
        KernelFlags, PlacementFlags, __MASK);
#else
    struct MemoryMappingParameters Parameters;
    Parameters.VirtualAddress = *Address;
    Parameters.Length         = Length;
    Parameters.Flags          = Flags;
    
    Status   = CreateMemoryMapping((UUId_t)Handle, &Parameters, (void**)&StateObject->Address);
    *Address = StateObject->Address;
#endif
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

#ifdef LIBC_KERNEL
    // Translate memory flags to kernel flags
    Flags_t KernelFlags = MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_DOMAIN;
    if (StateObject->Flags & MEMORY_EXECUTABLE) {
        KernelFlags |= MAPPING_EXECUTABLE;
    }
    if (!(StateObject->Flags & (MEMORY_WRITE | MEMORY_EXECUTABLE))) {
        KernelFlags |= MAPPING_READONLY;
    }
    ChangeMemorySpaceProtection(StateObject->Handle, StateObject->Address, StateObject->Length, KernelFlags, NULL);
#else
    MemoryFree((void*)StateObject->Address, StateObject->Length);
#endif
    dsfree(StateObject);
}
