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

#ifndef _BUFFER_INTERFACE_H_
#define _BUFFER_INTERFACE_H_

#include <os/osdefs.h>

// System dma buffer
// Contains information about a dma buffer for use with transfers,
// shared memory or hardware interaction.
typedef struct _DmaBuffer {
    UUId_t              Handle;
    uintptr_t           Address;
    uintptr_t           Dma;
    size_t              Capacity;
    size_t              Position;
} DmaBuffer_t;

_CODE_BEGIN
/* CreateBuffer
 * Creates a new buffer either from an existing handle (Length will be ignored),
 * or creates a new buffer with the given length if FromHandle == UUID_INVALID. */
CRTDECL(
DmaBuffer_t*,
CreateBuffer(
    _In_ UUId_t                 FromHandle,
    _In_ size_t                 Length));

/* DestroyBuffer
 * Destroys the given buffer object and release resources
 * allocated with the CreateBuffer function */
CRTDECL( 
OsStatus_t,
DestroyBuffer(
    _In_ DmaBuffer_t*           BufferObject));

/* ZeroBuffer
 * Clears the entire buffer and resets the internal indices */
CRTDECL( 
OsStatus_t,
ZeroBuffer(
    _In_ DmaBuffer_t*           BufferObject));

/* SeekBuffer
 * Seeks the current write/read marker to a specified point
 * in the buffer */
CRTDECL(
OsStatus_t,
SeekBuffer(
    _In_ DmaBuffer_t*           BufferObject,
    _In_ size_t                 Position));

/* ReadBuffer
 * Reads <BytesToWrite> into the given user-buffer from the given buffer-object. 
 * It performs indexed reads, so the read will be from the current position */
CRTDECL( 
OsStatus_t,
ReadBuffer(
    _In_      DmaBuffer_t*      BufferObject,
    _Out_     const void*       Buffer,
    _In_      size_t            BytesToRead,
    _Out_Opt_ size_t*           BytesRead));

/* WriteBuffer
 * Writes <BytesToWrite> into the allocated buffer-object from the given user-buffer. 
 * It performs indexed writes, so the next write will be appended to the current position */
CRTDECL( 
OsStatus_t,
WriteBuffer(
    _In_      DmaBuffer_t*      BufferObject,
    _In_      const void*       Buffer,
    _In_      size_t            BytesToWrite,
    _Out_Opt_ size_t*           BytesWritten));

/* CombineBuffer
 * Writes <BytesToTransfer> into the destination from the given
 * source buffer, make sure the position in both buffers are correct.
 * The number of bytes transferred is set as output */
CRTDECL( 
OsStatus_t,
CombineBuffer(
    _In_      DmaBuffer_t*      Destination,
    _In_      DmaBuffer_t*      Source,
    _In_      size_t            BytesToTransfer,
    _Out_Opt_ size_t*           BytesTransferred));

/* GetBufferHandle
 * Retrieves the handle of the dma buffer for other processes to use. */
CRTDECL( 
UUId_t,
GetBufferHandle(
    _In_ DmaBuffer_t*           BufferObject));

/* GetBufferSize
 * Retrieves the size of the dma buffer. This might vary from the length given in
 * creation of the buffer as it may change it for performance reasons. */
CRTDECL( 
size_t,
GetBufferSize(
    _In_ DmaBuffer_t*           BufferObject));

/* GetBufferDma
 * Retrieves the dma address of the memory buffer. This address cannot be used
 * for accessing memory, but instead is a pointer to the physical memory. */
CRTDECL( 
uintptr_t,
GetBufferDma(
    _In_ DmaBuffer_t*           BufferObject));

/* GetBufferDataPointer
 * Retrieves the data pointer to the physical memory. This can be used to access
 * the physical memory as this pointer is mapped to the dma.  */
CRTDECL( 
void*,
GetBufferDataPointer(
    _In_ DmaBuffer_t*           BufferObject));

_CODE_END

#endif //!_BUFFER_INTERFACE_H_
