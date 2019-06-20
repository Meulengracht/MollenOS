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
BufferCreate(
    _In_  size_t  InitialSize,
    _In_  size_t  Capacity,
    _In_  Flags_t Flags,
    _Out_ UUId_t* HandleOut,
    _Out_ void*   BufferOut)
{
    if (!HandleOut || !BufferOut || !Capacity) {
        return OsInvalidParameters;
    }
    return Syscall_BufferCreate(InitialSize, Capacity, Flags, HandleOut, BufferOut);
}

OsStatus_t
BufferCreateFrom(
    _In_  UUId_t ExistingHandle,
    _Out_ void** BufferOut)
{
    if (!BufferOut || ExistingHandle == UUID_INVALID) {
        return OsInvalidParameters;
    }
    return Syscall_BufferInherit(ExistingHandle, BufferOut);
}

OsStatus_t
BufferDestroy(
    _In_ UUId_t Handle,
    _In_ void*  Buffer)
{
    OsStatus_t Status;
    size_t     Length;
    size_t     Capacity;
    
    if (Handle == UUID_INVALID || !Buffer) {
        return OsInvalidParameters;
    }
    
    // Get metrics of the buffer so we can free
    Status = BufferGetMetrics(Handle, &Length, &Capacity);
    if (Status != OsSuccess) {
        return Status;
    }
    
    // Free and cleanup the buffer space, we do this in two go's
    Status = MemoryFree(Buffer, Capacity);
    if (Status != OsSuccess) {
        return Status;
    }
    return Syscall_DestroyHandle(Handle);
}

OsStatus_t
BufferResize(
    _In_ UUId_t Handle,
    _In_ void*  Buffer,
    _In_ size_t Size)
{
    if (Handle == UUID_INVALID || !Buffer) {
        return OsInvalidParameters;
    }
    return Syscall_BufferResize(Handle, Buffer, Size);
}

OsStatus_t
BufferGetMetrics(
    _In_  UUId_t  Handle,
    _Out_ size_t* SizeOut,
    _Out_ size_t* CapacityOut)
{
    if (Handle == UUID_INVALID || !SizeOut || !CapacityOut) {
        return OsInvalidParameters;
    }
    return Syscall_BufferGetMetrics(Handle, SizeOut, CapacityOut);
}

OsStatus_t
BufferGetVectors(
    _In_  UUId_t    Handle,
    _Out_ uintptr_t VectorOut[])
{
    if (Handle == UUID_INVALID) {
        return OsInvalidParameters;
    }
    return Syscall_BufferGetVectors(Handle, VectorOut);
}

OsStatus_t
BufferCreateManaged(
    _In_  UUId_t            ExistingHandle,
    _Out_ ManagedBuffer_t** BufferOut)
{
    ManagedBuffer_t* Buffer;
    OsStatus_t       Status;
    void*            BufferPointer;
    
    Status = BufferCreateFrom(ExistingHandle, &BufferPointer);
    if (Status != OsSuccess) {
        return Status;
    }
    
    Buffer = (ManagedBuffer_t*)malloc(sizeof(ManagedBuffer_t));
    if (!Buffer) {
        return OsOutOfMemory;
    }
    
    Buffer->BufferHandle = ExistingHandle;
    Buffer->Data         = BufferPointer;
    Buffer->Position     = 0;
    
    Status = BufferGetMetrics(ExistingHandle, &Buffer->Length, &Buffer->Capacity);
    if (Status == OsSuccess) {
        *BufferOut = Buffer;
    }
    else {
        // cleanup??
        assert(0);
    }
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
    
    Status = BufferDestroy(Buffer->BufferHandle, Buffer->Data);
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
