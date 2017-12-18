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

/* Includes
 * - C-Library */
#include <os/osdefs.h>

/* BufferObject Type
 * This is the way to interact with transfer
 * buffers throughout the system, must be used
 * for any hardware transactions */
typedef struct _BufferObject BufferObject_t;

/* Start one of these before function prototypes */
_CODE_BEGIN

/* CreateBuffer 
 * Creates a new buffer object with the given size, 
 * this allows hardware drivers to interact with the buffer */
CRTDECL(
BufferObject_t*,
CreateBuffer(
	_In_ size_t Length));

/* DestroyBuffer 
 * Destroys the given buffer object and release resources
 * allocated with the CreateBuffer function */
CRTDECL( 
OsStatus_t,
DestroyBuffer(
	_In_ BufferObject_t *BufferObject));

/* AcquireBuffer
 * Acquires the buffer for access in this addressing space
 * It's impossible to access the data by normal means before calling this */
CRTDECL( 
OsStatus_t,
AcquireBuffer(
	_In_ BufferObject_t *BufferObject));

/* ReleaseBuffer 
 * Releases the buffer access and frees resources needed for accessing 
 * this buffer */
CRTDECL(
OsStatus_t,
ReleaseBuffer(
	_In_ BufferObject_t *BufferObject));

/* ZeroBuffer 
 * Clears the entire buffer and resets the internal indices */
CRTDECL( 
OsStatus_t,
ZeroBuffer(
	_In_ BufferObject_t *BufferObject));

/* SeekBuffer
 * Seeks the current write/read marker to a specified point
 * in the buffer */
CRTDECL(
OsStatus_t,
SeekBuffer(
	_In_ BufferObject_t *BufferObject,
	_In_ size_t Position));

/* ReadBuffer 
 * Reads <BytesToWrite> into the given user-buffer
 * from the given buffer-object. It uses indexed reads, so
 * the read will be from the current position */
CRTDECL( 
OsStatus_t,
ReadBuffer(
	_In_ BufferObject_t *BufferObject, 
	_Out_ __CONST void *Buffer, 
	_In_ size_t BytesToRead,
	_Out_Opt_ size_t *BytesRead));

/* WriteBuffer 
 * Writes <BytesToWrite> into the allocated buffer-object
 * from the given user-buffer. It uses indexed writes, so
 * the next write will be appended to the current position */
CRTDECL( 
OsStatus_t,
WriteBuffer(
	_In_ BufferObject_t *BufferObject, 
	_In_ __CONST void *Buffer, 
	_In_ size_t BytesToWrite,
	_Out_Opt_ size_t *BytesWritten));

/* CombineBuffer 
 * Writes <BytesToTransfer> into the destination from the given
 * source buffer, make sure the position in both buffers are correct.
 * The number of bytes transferred is set as output */
CRTDECL( 
OsStatus_t,
CombineBuffer(
	_Out_ BufferObject_t *Destination, 
	_In_ BufferObject_t *Source,
	_In_ size_t BytesToTransfer,
	_Out_Opt_ size_t *BytesTransferred));

/* GetBufferSize
 * Retrieves the current size of the given buffer, note that the capacity
 * and current size of the buffer may differ because of the current subsystem */
CRTDECL(
size_t,
GetBufferSize(
	_In_ BufferObject_t *BufferObject));

/* ChangeBufferSize
 * Changes the current size of the buffer, but may only be changed within the
 * limits of the capacity of the buffer. This operation resets position */
CRTDECL(
OsStatus_t,
ChangeBufferSize(
	_In_ BufferObject_t *BufferObject,
	_In_ size_t Size));

/* GetBufferCapacity
 * Retrieves the capacity of the given buffer-object */
CRTDECL(
size_t,
GetBufferCapacity(
	_In_ BufferObject_t *BufferObject));

/* GetBufferObjectSize
 * Retrieves the size of the buffer-object structure as it can be dynamic
 * in size due to the memory regions it allocates */
CRTDECL(
size_t,
GetBufferObjectSize(
	_In_ BufferObject_t *BufferObject));

/* GetBufferData
 * Returns a pointer to the raw data currently in the buffer */
CRTDECL(
uintptr_t*,
GetBufferData(
	_In_ BufferObject_t *BufferObject));

/* GetBufferAddress
 * Returns the physical address of the buffer in memory. This address
 * is not accessible by normal means. */
CRTDECL(
uintptr_t,
GetBufferAddress(
	_In_ BufferObject_t *BufferObject));

_CODE_END

#endif //!_BUFFER_INTERFACE_H_
