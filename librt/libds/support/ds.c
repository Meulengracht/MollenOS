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

#include <string.h>
#include <ds/ds.h>
#include "../pe/pe.h"

#define __TRACE
#ifdef LIBC_KERNEL
#define __MODULE "DATA"
#include <system/interrupts.h>
#include <memoryspace.h>
#include <stdio.h>
#include <debug.h>
#include <heap.h>
#else
#include <os/buffer.h>
#include <os/memory.h>
#include <os/utils.h>
#include <stdlib.h>
#include <stdio.h>
#endif

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
OsStatus_t LoadFile(MString_t* Path, MString_t* FullPath, void** BufferOut, size_t* LengthOut)
{
#ifdef LIBC_KERNEL
    Status = ModulesQueryPath(Path, (void**)&Buffer, &Size);
#else
    FileSystemCode_t    FsCode;
    UUId_t              fHandle;
    DmaBuffer_t         TransferBuffer  = { 0 };
    LargeInteger_t      QueriedSize     = { { 0 } };
    void*               fBuffer         = NULL;
    size_t fRead = 0, fIndex = 0;
    size_t fSize = 0;

    // Open the file as read-only
    FsCode = OpenFile(Path, 0, __FILE_READ_ACCESS, &fHandle);
    if (FsCode != FsOk) {
        ERROR("Invalid path given: %s", Path);
        return OsError;
    }

    if (GetFileSize(fHandle, &QueriedSize.u.LowPart, NULL) != OsSuccess) {
        ERROR("Failed to retrieve the file size");
        CloseFile(fHandle);
        return OsError;
    }

    if (FullPath != NULL) {
        *FullPath = (char*)kmalloc(_MAXPATH);
        memset((void*)*FullPath, 0, _MAXPATH);
        if (GetFilePath(fHandle, *FullPath, _MAXPATH) != OsSuccess) {
            ERROR("Failed to query file handle for full path");
            kfree((void*)*FullPath);
            CloseFile(fHandle);
            return OsError;
        }
    }

    fSize = (size_t)QueriedSize.QuadPart;
    if (fSize != 0) {
        if (CreateMemoryBuffer(MEMORY_BUFFER_KERNEL, fSize, &TransferBuffer) != OsSuccess) {
            ERROR("Failed to create a memory buffer");
            CloseFile(fHandle);
            return OsError;
        }
        
        fBuffer = kmalloc(fSize);
        if (fBuffer == NULL) {
            ERROR("Failed to allocate resources for file-loading");
            CloseFile(fHandle);
            return OsError;
        }

        FsCode = ReadFile(fHandle, TransferBuffer.Handle, fSize, &fIndex, &fRead);
        if (FsCode != FsOk) {
            ERROR("Failed to read file, code %i", FsCode);
            kfree(fBuffer);
            CloseFile(fHandle);
            return OsError;
        }
        memcpy(fBuffer, (const void*)TransferBuffer.Address, fRead);

        // Cleanup by removing the memory mappings and freeing the
        // physical space allocated.
        RemoveSystemMemoryMapping(GetCurrentSystemMemorySpace(), TransferBuffer.Address, TransferBuffer.Capacity);
        DestroyHandle(TransferBuffer.Handle);
    }
    CloseFile(fHandle);
    *Data   = fBuffer;
    *Length = fSize;
    return OsSuccess;
#endif
}

OsStatus_t CreateImageSpace(MemorySpaceHandle_t* HandleOut)
{
#ifdef LIBC_KERNEL
    *HandleOut = (MemorySpaceHandle_t)GetCurrentSystemMemorySpace();
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
    OsStatus_t Status;
#ifdef LIBC_KERNEL
    Status = CreateSystemMemorySpaceMapping((SystemMemorySpace_t*)Handle, NULL, Address, Length, Flags, __MASK);
    _CRT_UNUSED(HandleOut);
#else
    struct MemoryMappingParameters Parameters;
    DmaBuffer_t* Buffer = (DmaBuffer_t*)dsalloc(sizeof(DmaBuffer_t));

    memset(Buffer, 0, sizeof(DmaBuffer_t));
    Parameters.VirtualAddress = *Address;
    Parameters.Length         = Length;
    Parameters.Flags          = Flags;
    
    Status = CreateMemoryMapping((UUId_t)Handle, &Parameters, Buffer);
    if (Status != OsSuccess) {
        dsfree(Buffer);
        return Status;
    }
    *Address   = (uintptr_t)GetBufferDataPointer(Buffer);
    *HandleOut = (MemoryMapHandle_t)Buffer;
#endif
    return Status;
}

// Releases the access previously granted to the mapping, however this is not something
// that is neccessary in kernel mode, so this function does nothing
void ReleaseImageMapping(MemoryMapHandle_t Handle)
{
#ifdef LIBC_KERNEL
    _CRT_UNUSED(Handle);
#else
    DestroyBuffer((DmaBuffer_t*)Handle);
#endif
}
