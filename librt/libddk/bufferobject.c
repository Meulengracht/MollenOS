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
    _Out_ UUId_t* HandleOut)
{
    
}

OsStatus_t
BufferCreateFrom(
    _In_ UUId_t ExistingHandle)
{
    
}

OsStatus_t
BufferDestroy(
    _In_ UUId_t Handle)
{
    return Syscall_DestroyHandle(Handle);
}

OsStatus_t
BufferResize(
    _In_ UUId_t Handle,
    _In_ size_t Size)
{
    
}

void*
BufferGetAccessPointer(
    _In_ UUId_t Handle)
{
    
}

OsStatus_t
BufferGetMetrics(
    _In_  UUId_t  Handle,
    _Out_ size_t* SizeOut,
    _Out_ size_t* CapacityOut)
{
    
}

OsStatus_t
BufferCreateManaged(
    _In_  UUId_t            ExistingHandle,
    _Out_ ManagedBuffer_t** BufferOut)
{
    ManagedBuffer_t* Buffer;
    OsStatus_t       Status;
    
    Status = BufferCreateFrom(ExistingHandle);
    if (Status != OsSuccess) {
        
    }
    
    Buffer = (ManagedBuffer_t*)malloc(sizeof(ManagedBuffer_t));
    if (!Buffer) {
        return OsOutOfMemory;
    }
    
    return OsSuccess;
}

OsStatus_t
BufferDestroyManaged(
    _In_ ManagedBuffer_t* Buffer)
{
    OsStatus_t Status;
    
    if (!BufferObject) {
        return OsInvalidParameters;
    }
    
    Status = BufferDestroy(Buffer->Handle);
    free(Buffer);
    return Status;
}

OsStatus_t
BufferZero(
    _In_ ManagedBuffer_t* Buffer)
{
    if (!BufferObject) {
        return OsInvalidParameters;
    }
    
    memset(BufferObject->Data, 0, BufferObject->Length);
    BufferObject->Position = 0;
    return OsSuccess;
}

OsStatus_t
BufferSeek(
    _In_ ManagedBuffer_t* Buffer,
    _In_ off_t            Offset)
{
    if (!BufferObject || BufferObject->Length < Offset) {
        return OsInvalidParameters;
    }
    BufferObject->Position = Offset;
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

    if (BytesToRead == 0) {
        if (BytesRead != NULL) {
            *BytesRead = 0;
        }
        return OsSuccess;
    }

    // Normalize and read
    BytesNormalized = MIN(BytesToRead, Buffer->Length - Buffer->Position);
    if (BytesNormalized == 0) {
        WARNING("ReadBuffer::BytesNormalized == 0");
        return OsError;
    }
    
    memcpy(Data, (const void*)((char*)Buffer->Data + Buffer->Position), BytesNormalized);
    Buffer->Position += BytesNormalized;
    if (BytesRead != NULL) {
        *BytesRead = BytesNormalized;
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

    if (BytesToWrite == 0) {
        if (BytesWritten != NULL) {
            *BytesWritten = 0;
        }
        return OsSuccess;
    }

    // Normalize and write
    BytesNormalized = MIN(BytesToWrite, Buffer->Length - Buffer->Position);
    if (BytesNormalized == 0) {
        WARNING("WriteBuffer::BytesNormalized == 0");
        return OsError;
    }
    
    memcpy((void*)(Buffer->Data + Buffer->Position), Buffer, BytesNormalized);
    Buffer->Position += BytesNormalized;
    if (BytesWritten != NULL) {
        *BytesWritten = BytesNormalized;
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
        if (BytesTransferred != NULL) {
            *BytesTransferred = 0;
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
    if (BytesTransferred != NULL) {
        *BytesTransferred = BytesNormalized;
    }
    return OsSuccess;
}











/* CreateBuffer 
 * Creates a new buffer object with the given size, 
 * this allows hardware drivers to interact with the buffer */
DmaBuffer_t*
CreateBuffer(
    _In_ UUId_t FromHandle,
    _In_ size_t Length)
{
    DmaBuffer_t* Buffer;

    // Make sure the length is positive if we are requesting
    // to create a new buffer
    if (FromHandle == UUID_INVALID && Length == 0) {
        return NULL;
    }

    Buffer = (DmaBuffer_t*)malloc(sizeof(DmaBuffer_t));
    if (!Buffer) {
        return NULL;
    }
    memset((void*)Buffer, 0, sizeof(DmaBuffer_t));
    if (FromHandle != UUID_INVALID) {
        if (Syscall_AcquireBuffer(FromHandle, Buffer) != OsSuccess) {
            free(Buffer);
            return NULL;
        }
    }
    else {
        if (Syscall_CreateBuffer(Length, Buffer) != OsSuccess) {
            free(Buffer);
            return NULL;
        }
    }
    return Buffer;
}

/* DestroyBuffer
 * Destroys the given buffer object and release resources
 * allocated with the CreateBuffer function */
OsStatus_t
DestroyBuffer(
    _In_ DmaBuffer_t* BufferObject)
{
    OsStatus_t Status;

    // Sanitize the parameter
    if (BufferObject == NULL) {
        return OsError;
    }

    // First step is to unmap the virtual space
    if (BufferObject->Address != 0) {
        Status = MemoryFree((void*)BufferObject->Address, BufferObject->Capacity);
        if (Status != OsSuccess) {
            return OsError;
        }
    }

    // Cleanup the handle
    Status = Syscall_DestroyHandle(BufferObject->Handle);
    free(BufferObject);
    return Status;
}
