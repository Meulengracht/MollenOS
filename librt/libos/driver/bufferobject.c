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

/* Includes
 * - System */
#include <os/driver/buffer.h>

#ifdef LIBC_KERNEL
#define __MODULE		"IOBF"
#define __TRACE
#include <arch.h>
#include <debug.h>
#include <heap.h>
#else
#define __TRACE
#include <os/mollenos.h>
#include <os/syscall.h>
#include <os/utils.h>
#endif

/* Includes
 * - Library */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* BufferObject Structure (Private)
 * This is the way to interact with transfer
 * buffers throughout the system, must be used
 * for any hardware transactions */
typedef struct _BufferObject {
	__CONST char			*Virtual;
	uintptr_t				 Physical;
	size_t					 Length;
	size_t					 Capacity;
	size_t					 Position;
} BufferObject_t;

/* CreateBuffer 
 * Creates a new buffer object with the given size, 
 * this allows hardware drivers to interact with the buffer */
BufferObject_t *
CreateBuffer(
	_In_ size_t Length)
{
	// Variables
	BufferObject_t *Buffer = NULL;
	OsStatus_t Result;

	// Make sure the length is positive
	if (Length == 0) {
		return NULL;
	}

	// Since we need this support in both kernel and
	// library space - two versions below
#ifdef LIBC_KERNEL
	Buffer = (BufferObject_t*)kmalloc(sizeof(BufferObject_t));
	Buffer->Virtual = (__CONST char*)kmalloc_ap(
        DIVUP(Length, PAGE_SIZE) * PAGE_SIZE, (uintptr_t*)&Buffer->Physical);
	Buffer->Capacity = DIVUP(Length, PAGE_SIZE) * PAGE_SIZE;
    memset((void*)Buffer->Virtual, 0, Buffer->Capacity);
	Result = OsSuccess;
#else
	Buffer = (BufferObject_t*)malloc(sizeof(BufferObject_t));
    Result = MemoryAllocate(Length, MEMORY_COMMIT 
        | MEMORY_CONTIGIOUS | MEMORY_LOWFIRST | MEMORY_CLEAN,
		(void**)&Buffer->Virtual, &Buffer->Physical);
	Buffer->Capacity = DIVUP(Length, 0x1000) * 0x1000;

	// Sanitize the result and
	// return the newly created object
	if (Result != OsSuccess) {
		free(Buffer);
		return NULL;
	}
#endif

	// Set rest of variables and return
	Buffer->Length = Length;
	Buffer->Position = 0;
	return Buffer;
}

/* DestroyBuffer 
 * Destroys the given buffer object and release resources
 * allocated with the CreateBuffer function */
OsStatus_t
DestroyBuffer(
	_In_ BufferObject_t *BufferObject)
{
	// Variables
	OsStatus_t Result;

	// Sanitize the parameter
	if (BufferObject == NULL) {
		return OsError;
	}

	// Depending on os-version or user-version
#ifdef LIBC_KERNEL
	// Just free buffers
	kfree((void*)BufferObject->Virtual);
	kfree(BufferObject);
	Result = OsSuccess;
#else
	// Call memory services
	Result = MemoryFree((void*)BufferObject->Virtual, BufferObject->Length);
	free(BufferObject);
#endif

	// Done
	return Result;
}

/* AcquireBuffer
 * Acquires the buffer for access in this addressing space
 * It's impossible to access the data by normal means before calling this */
OsStatus_t
AcquireBuffer(
	_In_ BufferObject_t *BufferObject)
{
	// Sanitize
	if (BufferObject == NULL) {
		return OsError;
	}

#ifndef LIBC_KERNEL
	// Map in address space
	// Two params (Physical -> Capacity)
	return Syscall_MemoryAcquire(BufferObject->Physical, 
        BufferObject->Capacity, &BufferObject->Virtual);
#else
	return OsError;
#endif
}

/* ReleaseBuffer 
 * Releases the buffer access and frees resources needed for accessing 
 * this buffer */
OsStatus_t
ReleaseBuffer(
	_In_ BufferObject_t *BufferObject)
{
	// Sanitize
	if (BufferObject == NULL) {
		return OsError;
	}

#ifndef LIBC_KERNEL
	// Unmap from addressing space
	// Two params (Physical -> Capacity)
	return Syscall_MemoryRelease(BufferObject->Virtual, BufferObject->Capacity);
#else
return OsError;
#endif
}

/* ZeroBuffer 
 * Clears the entire buffer and resets the internal indexes */
OsStatus_t
ZeroBuffer(
	_In_ BufferObject_t *BufferObject)
{
	// Reset buffer
	memset((void*)BufferObject->Virtual, 0, BufferObject->Length);

	// Reset counters
	BufferObject->Position = 0;

	// Done
	return OsSuccess;
}

/* SeekBuffer
 * Seeks the current write/read marker to a specified point
 * in the buffer */
OsStatus_t
SeekBuffer(
	_In_ BufferObject_t *BufferObject,
	_In_ size_t Position)
{
	// Sanitize parameters
	if (BufferObject == NULL || BufferObject->Length < Position) {
		return OsError;
	}

	// Update position
	BufferObject->Position = Position;
	return OsSuccess;
}

/* ReadBuffer 
 * Reads <BytesToWrite> into the given user-buffer
 * from the given buffer-object. It uses indexed reads, so
 * the read will be from the current position */
OsStatus_t 
ReadBuffer(
	_In_ BufferObject_t *BufferObject, 
	_Out_ __CONST void *Buffer, 
	_In_ size_t BytesToRead,
	_Out_Opt_ size_t *BytesRead)
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
	BytesNormalized = MIN(BytesToRead, BufferObject->Length - BufferObject->Position);
	if (BytesNormalized == 0) {
		WARNING("ReadBuffer::BytesNormalized == 0");
		return OsError;
	}
	memcpy((void*)Buffer, (BufferObject->Virtual + BufferObject->Position), BytesNormalized);

	// Increase position
	BufferObject->Position += BytesNormalized;

	// Update out
	if (BytesRead != NULL) {
		*BytesRead = BytesNormalized;
	}
	return OsSuccess;
}

/* WriteBuffer 
 * Writes <BytesToWrite> into the allocated buffer-object
 * from the given user-buffer. It uses indexed writes, so
 * the next write will be appended to the current position */
OsStatus_t 
WriteBuffer(
	_In_ BufferObject_t *BufferObject, 
	_In_ __CONST void *Buffer, 
	_In_ size_t BytesToWrite,
	_Out_Opt_ size_t *BytesWritten)
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
	BytesNormalized = MIN(BytesToWrite, BufferObject->Length - BufferObject->Position);
	if (BytesNormalized == 0) {
		WARNING("WriteBuffer::BytesNormalized == 0");
		return OsError;
	}
	memcpy((void*)(BufferObject->Virtual + BufferObject->Position), Buffer, BytesNormalized);

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
	_Out_ BufferObject_t *Destination,
	_In_ BufferObject_t *Source,
	_In_ size_t BytesToTransfer,
	_Out_Opt_ size_t *BytesTransferred)
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
		MIN(Destination->Length - Destination->Position, Source->Length - Source->Position));
	if (BytesNormalized == 0) {
		WARNING("CombineBuffer::BytesNormalized == 0");
		return OsError;
	}
	memcpy((void*)(Destination->Virtual + Destination->Position), 
		(Source->Virtual + Source->Position), BytesNormalized);

	// Increase positions
	Destination->Position += BytesNormalized;
	Source->Position += BytesNormalized;

	// Update out
	if (BytesTransferred != NULL) {
		*BytesTransferred = BytesNormalized;
	}
	return OsSuccess;
}

/* GetBufferSize
 * Retrieves the current size of the given buffer, note that the capacity
 * and current size of the buffer may differ because of the current subsystem */
size_t
GetBufferSize(
	_In_ BufferObject_t *BufferObject)
{
	// Sanitize
	if (BufferObject == NULL) {
		return 0;
	}

	// Return the buffer length
	return BufferObject->Length;
}

/* ChangeBufferSize
 * Changes the current size of the buffer, but may only be changed within the
 * limits of the capacity of the buffer. This operation resets position */
OsStatus_t
ChangeBufferSize(
	_In_ BufferObject_t *BufferObject,
	_In_ size_t Size)
{
	// Sanitize
	if (BufferObject == NULL
		|| (BufferObject->Capacity < Size)) {
		return OsError;
	}

	// Update the new size and reset position
	BufferObject->Length = Size;
	BufferObject->Position = 0;
	return OsSuccess;
}

/* GetBufferCapacity
 * Retrieves the capacity of the given buffer-object */
size_t
GetBufferCapacity(
	_In_ BufferObject_t *BufferObject)
{
	// Sanitize
	if (BufferObject == NULL) {
		return 0;
	}

	// Return the buffer capacity
	return BufferObject->Capacity;
}

/* GetBufferObjectSize
 * Retrieves the size of the buffer-object structure as it can be dynamic
 * in size due to the memory regions it allocates */
size_t
GetBufferObjectSize(
	_In_ BufferObject_t *BufferObject)
{
	// Sanitize parameters
	if (BufferObject == NULL) {
		return 0;
	}

	// Return the size of the structure
	return sizeof(BufferObject_t);
}

/* GetBufferData
 * Returns a pointer to the raw data currently in the buffer */
uintptr_t*
GetBufferData(
	_In_ BufferObject_t *BufferObject)
{
	// Sanitize
	if (BufferObject == NULL) {
		return NULL;
	}

	// Return the virtual address
	return (uintptr_t*)BufferObject->Virtual;
}

/* GetBufferAddress
 * Returns the physical address of the buffer in memory. This address
 * is not accessible by normal means. */
uintptr_t
GetBufferAddress(
	_In_ BufferObject_t *BufferObject)
{
	// Sanitize
	if (BufferObject == NULL) {
		return 0;
	}

	// Return the physical address
	return BufferObject->Physical;
}
