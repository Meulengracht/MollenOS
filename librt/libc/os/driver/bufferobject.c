/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Buffer Support Definitions & Structures
 * - This header describes the base buffer-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */
#define __TRACE

#include <os/buffer.h>
#include <os/mollenos.h>
#include <os/syscall.h>
#include <os/utils.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* CreateBuffer 
 * Creates a new buffer object with the given size, 
 * this allows hardware drivers to interact with the buffer */
DmaBuffer_t*
CreateBuffer(
    _In_ UUId_t                 FromHandle,
    _In_ size_t                 Length)
{
    DmaBuffer_t *Buffer = NULL;

    // Make sure the length is positive if we are requesting
    // to create a new buffer
    if (FromHandle == UUID_INVALID && Length == 0) {
        return NULL;
    }

    Buffer = (DmaBuffer_t*)malloc(sizeof(DmaBuffer_t));
    memset((void*)Buffer, 0, sizeof(DmaBuffer_t));
    if (FromHandle != UUID_INVALID) {
        if (Syscall_AcquireBuffer(FromHandle, Buffer) != OsSuccess) {
            free(Buffer);
            return NULL;
        }
    }
    else {
        if (Syscall_CreateBuffer(0, Length, Buffer) != OsSuccess) {
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
    _In_ DmaBuffer_t*           BufferObject)
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

/* ZeroBuffer 
 * Clears the entire buffer and resets the internal indexes */
OsStatus_t
ZeroBuffer(
    _In_ DmaBuffer_t*           BufferObject)
{
    // Reset buffer
    memset((void*)BufferObject->Address, 0, BufferObject->Capacity);
    BufferObject->Position = 0;
    return OsSuccess;
}

/* SeekBuffer
 * Seeks the current write/read marker to a specified point
 * in the buffer */
OsStatus_t
SeekBuffer(
    _In_ DmaBuffer_t*           BufferObject,
    _In_ size_t                 Position)
{
    // Sanitize parameters
    if (BufferObject == NULL || BufferObject->Capacity < Position) {
        return OsError;
    }
    BufferObject->Position = Position;
    return OsSuccess;
}

/* ReadBuffer 
 * Reads <BytesToWrite> into the given user-buffer from the given buffer-object. 
 * It performs indexed reads, so the read will be from the current position */
OsStatus_t
ReadBuffer(
    _In_      DmaBuffer_t*      BufferObject,
    _Out_     const void*       Buffer,
    _In_      size_t            BytesToRead,
    _Out_Opt_ size_t*           BytesRead)
{
    // Variables
    size_t BytesNormalized = 0;

    // Sanitize all in-params
    if (BufferObject == NULL || Buffer == NULL) {
        return OsError;
    }

    // Sanitize
    if (BytesToRead == 0) {
        if (BytesRead != NULL) {
            *BytesRead = 0;
        }
        return OsSuccess;
    }

    // Normalize and read
    BytesNormalized = MIN(BytesToRead, BufferObject->Capacity - BufferObject->Position);
    if (BytesNormalized == 0) {
        WARNING("ReadBuffer::BytesNormalized == 0");
        return OsError;
    }
    memcpy((void*)Buffer, (const void*)(BufferObject->Address + BufferObject->Position), BytesNormalized);

    // Increase position
    BufferObject->Position += BytesNormalized;

    // Update out
    if (BytesRead != NULL) {
        *BytesRead = BytesNormalized;
    }
    return OsSuccess;
}

/* WriteBuffer 
 * Writes <BytesToWrite> into the allocated buffer-object from the given user-buffer. 
 * It performs indexed writes, so the next write will be appended to the current position */
OsStatus_t
WriteBuffer(
    _In_      DmaBuffer_t*      BufferObject,
    _In_      const void*       Buffer,
    _In_      size_t            BytesToWrite,
    _Out_Opt_ size_t*           BytesWritten)
{
    // Variables
    size_t BytesNormalized = 0;

    // Sanitize all in-params
    if (BufferObject == NULL || Buffer == NULL) {
        return OsError;
    }

    // Sanitize
    if (BytesToWrite == 0) {
        if (BytesWritten != NULL) {
            *BytesWritten = 0;
        }
        return OsSuccess;
    }

    // Normalize and write
    BytesNormalized = MIN(BytesToWrite, BufferObject->Capacity - BufferObject->Position);
    if (BytesNormalized == 0) {
        WARNING("WriteBuffer::BytesNormalized == 0");
        return OsError;
    }
    memcpy((void*)(BufferObject->Address + BufferObject->Position), Buffer, BytesNormalized);

    // Increase position
    BufferObject->Position += BytesNormalized;

    // Update out
    if (BytesWritten != NULL) {
        *BytesWritten = BytesNormalized;
    }
    return OsSuccess;
}

/* CombineBuffer 
 * Writes <BytesToTransfer> into the destination from the given
 * source buffer, make sure the position in both buffers are correct.
 * The number of bytes transferred is set as output */
OsStatus_t
CombineBuffer(
    _In_      DmaBuffer_t*      Destination,
    _In_      DmaBuffer_t*      Source,
    _In_      size_t            BytesToTransfer,
    _Out_Opt_ size_t*           BytesTransferred)
{
    // Variables
    size_t BytesNormalized = 0;
    
    // Sanitize parameters
    if (Destination == NULL || Source == NULL) {
        return OsError;
    }

    // Sanitize
    if (BytesToTransfer == 0) {
        if (BytesTransferred != NULL) {
            *BytesTransferred = 0;
        }
        return OsSuccess;
    }

    // Normalize and write
    BytesNormalized = MIN(BytesToTransfer, 
        MIN(Destination->Capacity - Destination->Position, Source->Capacity - Source->Position));
    if (BytesNormalized == 0) {
        WARNING("CombineBuffer::Source(Position %u, Length %u)", Source->Position, Source->Capacity);
        WARNING("CombineBuffer::Destination(Position %u, Length %u)", Destination->Position, Destination->Capacity);
        WARNING("CombineBuffer::BytesNormalized == 0");
        return OsError;
    }
    memcpy((void*)(Destination->Address + Destination->Position), 
        (const void*)(Source->Address + Source->Position), BytesNormalized);

    // Increase positions
    Destination->Position   += BytesNormalized;
    Source->Position        += BytesNormalized;
    if (BytesTransferred != NULL) {
        *BytesTransferred = BytesNormalized;
    }
    return OsSuccess;
}

/* GetBufferHandle
 * Retrieves the handle of the dma buffer for other processes to use. */
UUId_t
GetBufferHandle(
    _In_ DmaBuffer_t*           BufferObject)
{
    return (BufferObject != NULL) ? BufferObject->Handle : UUID_INVALID;
}

/* GetBufferSize 
 * Retrieves the size of the dma buffer. This might vary from the length given in
 * creation of the buffer as it may change it for performance reasons. */
size_t
GetBufferSize(
    _In_ DmaBuffer_t*           BufferObject)
{
    return (BufferObject != NULL) ? BufferObject->Capacity : 0;
}

/* GetBufferDma
 * Retrieves the dma address of the memory buffer. This address cannot be used
 * for accessing memory, but instead is a pointer to the physical memory. */
uintptr_t
GetBufferDma(
    _In_ DmaBuffer_t*           BufferObject)
{
    return (BufferObject != NULL) ? BufferObject->Dma : 0;
}

/* GetBufferDataPointer
 * Retrieves the data pointer to the physical memory. This can be used to access
 * the physical memory as this pointer is mapped to the dma. */
void*
GetBufferDataPointer(
    _In_ DmaBuffer_t*           BufferObject)
{
    return (BufferObject != NULL) ? (void*)BufferObject->Address : NULL;
}
