/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Buffer Support Definitions & Structures
 * - This header describes the base buffer-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */
#define __TRACE

#include <internal/_syscalls.h>
#include <os/mollenos.h>
#include <ddk/buffer.h>
#include <ddk/utils.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

OsStatus_t
BufferGetMetrics(
    _In_  UUId_t     Handle,
    _Out_ size_t*    SizeOut,
    _Out_ uintptr_t* VectorOut)
{
    if (Handle == UUID_INVALID) {
        return OsInvalidParameters;
    }
    return Syscall_BufferGetMetrics(Handle, SizeOut, VectorOut);
}

OsStatus_t
BufferCreateManaged(
    _In_  UUId_t            ExistingHandle,
    _Out_ ManagedBuffer_t** BufferOut)
{
    ManagedBuffer_t* Buffer;
    OsStatus_t       Status;
    void*            BufferPointer;
    
    Buffer = (ManagedBuffer_t*)malloc(sizeof(ManagedBuffer_t));
    if (!Buffer) {
        return OsOutOfMemory;
    }
    
    Status = BufferGetMetrics(ExistingHandle, &Buffer->Length, NULL);
    if (Status != OsSuccess) {
        free(Buffer);
        return Status;
    }
    
    Status = MemoryAllocate(NULL, Buffer->Length,
        MEMORY_SHARED_CLONE | MEMORY_READ | MEMORY_WRITE,
        &BufferPointer, &ExistingHandle);
    if (Status != OsSuccess) {
        return Status;
    }
    
    Buffer->BufferHandle = ExistingHandle;
    Buffer->Data         = BufferPointer;
    Buffer->Position     = 0;
    
    *BufferOut = Buffer;
    return Status;
}

OsStatus_t
BufferDestroyManaged(
    _In_ ManagedBuffer_t* Buffer)
{
    OsStatus_t Status;
    
    if (!Buffer) {
        return OsInvalidParameters;
    }
    
    Status = Syscall_DestroyHandle(Buffer->BufferHandle);
    free(Buffer);
    return Status;
}

OsStatus_t
BufferZero(
    _In_ ManagedBuffer_t* Buffer)
{
    if (!Buffer) {
        return OsInvalidParameters;
    }
    
    memset(Buffer->Data, 0, Buffer->Length);
    Buffer->Position = 0;
    return OsSuccess;
}

OsStatus_t
BufferSeek(
    _In_ ManagedBuffer_t* Buffer,
    _In_ off_t            Offset)
{
    if (!Buffer || Buffer->Length < Offset) {
        return OsInvalidParameters;
    }
    Buffer->Position = Offset;
    return OsSuccess;
}

OsStatus_t
BufferRead(
    _In_  ManagedBuffer_t* Buffer,
    _In_  void*            Data,
    _In_  size_t           Length,
    _Out_ size_t*          BytesReadOut)
{
    size_t BytesNormalized = 0;

    if (!Buffer || !Data) {
        return OsInvalidParameters;
    }

    if (Length == 0) {
        if (BytesReadOut) {
            *BytesReadOut = 0;
        }
        return OsSuccess;
    }

    // Normalize and read
    BytesNormalized = MIN(Length, Buffer->Length - Buffer->Position);
    if (BytesNormalized == 0) {
        WARNING("ReadBuffer::BytesNormalized == 0");
        return OsError;
    }
    
    memcpy(Data, (const void*)((char*)Buffer->Data + Buffer->Position), BytesNormalized);
    Buffer->Position += BytesNormalized;
    if (BytesReadOut) {
        *BytesReadOut = BytesNormalized;
    }
    return OsSuccess;
}

OsStatus_t
BufferWrite(
    _In_  ManagedBuffer_t* Buffer,
    _In_  const void*      Data,
    _In_  size_t           Length,
    _Out_ size_t*          BytesWrittenOut)
{
    size_t BytesNormalized = 0;

    if (!Buffer || !Data) {
        return OsInvalidParameters;
    }

    if (Length == 0) {
        if (BytesWrittenOut) {
            *BytesWrittenOut = 0;
        }
        return OsSuccess;
    }

    // Normalize and write
    BytesNormalized = MIN(Length, Buffer->Length - Buffer->Position);
    if (BytesNormalized == 0) {
        WARNING("WriteBuffer::BytesNormalized == 0");
        return OsError;
    }
    
    memcpy((void*)(Buffer->Data + Buffer->Position), Buffer, BytesNormalized);
    Buffer->Position += BytesNormalized;
    if (BytesWrittenOut) {
        *BytesWrittenOut = BytesNormalized;
    }
    return OsSuccess;
}

OsStatus_t
BufferCombine(
    _In_ ManagedBuffer_t* Destination,
    _In_ ManagedBuffer_t* Source,
    _In_ size_t           BytesToTransfer,
    _In_ size_t*          BytesTransferredOut)
{
    size_t BytesNormalized = 0;
    
    if (!Destination || !Source) {
        return OsInvalidParameters;
    }

    if (BytesToTransfer == 0) {
        if (BytesTransferredOut) {
            *BytesTransferredOut = 0;
        }
        return OsSuccess;
    }

    // Normalize and write
    BytesNormalized = MIN(BytesToTransfer, 
        MIN(Destination->Length - Destination->Position, Source->Length - Source->Position));
    if (BytesNormalized == 0) {
        WARNING("CombineBuffer::Source(Position %u, Length %u)", Source->Position, Source->Length);
        WARNING("CombineBuffer::Destination(Position %u, Length %u)", Destination->Position, Destination->Length);
        WARNING("CombineBuffer::BytesNormalized == 0");
        return OsError;
    }
    
    memcpy((void*)((char*)Destination->Data + Destination->Position), 
        (const void*)((char*)Source->Data + Source->Position), BytesNormalized);
    Destination->Position   += BytesNormalized;
    Source->Position        += BytesNormalized;
    if (BytesTransferredOut) {
        *BytesTransferredOut = BytesNormalized;
    }
    return OsSuccess;
}
