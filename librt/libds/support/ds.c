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
#include <modules/modules.h>
#include <memoryspace.h>
#include <stdio.h>
#include <debug.h>
#include <heap.h>
#else
#include <os/buffer.h>
#include <os/memory.h>
#include <os/utils.h>
#include <os/file.h>
#include <stdlib.h>
#include <stdio.h>
#endif

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
OsStatus_t LoadFile(MString_t* Path, MString_t** FullPath, void** BufferOut, size_t* LengthOut)
{
    OsStatus_t Status;
#ifdef LIBC_KERNEL
    Status = ModulesQueryPath(Path, (void**)&Buffer, &Size);
    if (Status == OsSuccess) {
        *FullPath = MStringCreate(MStringRaw(Path), StrUTF8);
    }
#else
    LargeInteger_t   QueriedSize = { { 0 } };
    void*            Buffer      = NULL;
    FileSystemCode_t FsCode;
    UUId_t           Handle;
    size_t           Size;

    // Open the file as read-only
    FsCode = OpenFile(Path, 0, __FILE_READ_ACCESS, &Handle);
    if (FsCode != FsOk) {
        ERROR("Invalid path given: %s", MStringRaw(Path));
        return OsError;
    }

    Status = GetFileSize(Handle, &QueriedSize.u.LowPart, NULL);
    if (Status != OsSuccess) {
        ERROR("Failed to retrieve the file size");
        CloseFile(Handle);
        return Status;
    }

    if (FullPath != NULL) {
        char* PathBuffer = (char*)dsalloc(_MAXPATH);
        memset(PathBuffer, 0, _MAXPATH);

        Status = GetFilePath(Handle, PathBuffer, _MAXPATH);
        if (Status != OsSuccess) {
            ERROR("Failed to query file handle for full path");
            dsfree(PathBuffer);
            CloseFile(Handle);
            return Status;
        }
        *FullPath = MStringCreate(PathBuffer, StrUTF8);
        dsfree(PathBuffer);
    }

    Size = (size_t)QueriedSize.QuadPart;
    if (Size != 0) {
        DmaBuffer_t* TransferBuffer = CreateBuffer(UUID_INVALID, Size);
        if (TransferBuffer != NULL) {
            Buffer = dsalloc(Size);
            if (Buffer != NULL) {
                size_t Index, Read = 0;
                FsCode = ReadFile(Handle, GetBufferHandle(TransferBuffer), Size, &Index, &Read);
                if (FsCode == FsOk && Read != 0) {
                    memcpy(Buffer, (const void*)GetBufferDataPointer(TransferBuffer), Read);
                }
                else {
                    MStringDestroy(*FullPath);
                    dsfree(Buffer);
                    Status = OsError;
                    Buffer = NULL;
                }
            }
            DestroyBuffer(TransferBuffer);
        }
    }
    CloseFile(Handle);
    *BufferOut = Buffer;
    *LengthOut = Size;
#endif
    return Status;
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
