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
#include <heap.h>
#else
#include <os/mollenos.h>
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
	UUId_t					 Creator;
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
	/* Variables */
	BufferObject_t *Buffer = NULL;
	OsStatus_t Result;

	/* Sanitize the length */
	if (Length == 0) {
		return NULL;
	}

	/* Allocate a new instance */
#ifdef LIBC_KERNEL
	Buffer = (BufferObject_t*)kmalloc(sizeof(BufferObject_t));
	Buffer->Virtual = (__CONST char*)kmalloc_ap(
		DIVUP(Length, PAGE_SIZE) * PAGE_SIZE, (uintptr_t*)&Buffer->Physical);
	Buffer->Capacity = DIVUP(Length, PAGE_SIZE) * PAGE_SIZE;
	Result = OsNoError;
#else
	Buffer = (BufferObject_t*)malloc(sizeof(BufferObject_t));
	Result = MemoryAllocate(Length,
		MEMORY_COMMIT | MEMORY_CONTIGIOUS | MEMORY_LOWFIRST,
		(void**)&Buffer->Virtual, &Buffer->Physical);
	Buffer->Capacity = DIVUP(Length, 0x1000) * 0x1000;

	/* Sanitize the result and
	 * return the newly created object */
	if (Result != OsNoError) {
		free(Buffer);
		return NULL;
	}

#endif
	Buffer->Length = Length;
	Buffer->IndexWrite = 0;
	return Buffer;
}

/* DestroyBuffer 
 * Destroys the given buffer object and release resources
 * allocated with the CreateBuffer function */
OsStatus_t
DestroyBuffer(
	_In_ BufferObject_t *BufferObject)
{
	/* Variables */
	OsStatus_t Result;

	/* Sanitize the length */
	if (BufferObject == NULL) {
		return OsError;
	}

	/* Contact OS services and release buffer */
#ifdef LIBC_KERNEL
	kfree((void*)BufferObject->Virtual);
	kfree(BufferObject);
	Result = OsNoError;
#else
	Result = MemoryFree((void*)BufferObject->Virtual, BufferObject->Length);
	free(BufferObject);
#endif
	return Result;
}

/* AcquireBuffer
 * Acquires the buffer for access in this addressing space
 * It's impossible to access the data by normal means before calling this */
MOSAPI 
OsStatus_t
MOSABI
AcquireBuffer(
	_In_ BufferObject_t *BufferObject);

/* ReleaseBuffer 
 * Releases the buffer access and frees resources needed for accessing 
 * this buffer */
MOSAPI
OsStatus_t
MOSABI
ReleaseBuffer(
	_In_ BufferObject_t *BufferObject);

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
	return OsNoError;
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
	return OsNoError;
}

/* ReadBuffer 
 * Reads <BytesToWrite> into the given user-buffer
 * from the given buffer-object. It uses indexed reads, so
 * the read will be from the current position */
OsStatus_t 
ReadBuffer(
	_In_ BufferObject_t *BufferObject, 
	_Out_ __CONST void *Buffer, 
	_In_ size_t BytesToRead)
{
	// Variables
	size_t BytesNormalized = 0;

	// Sanitize all in-params
	if (BufferObject == NULL || Buffer == NULL
		|| BytesToRead == 0) {
		return OsError;
	}

	// Normalize and read
	BytesNormalized = MIN(BytesToRead, BufferObject->Length);
	memcpy((void*)Buffer, BufferObject->Virtual, BytesNormalized);
	return OsNoError;
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
	/* Variables */
	size_t Bytes = 0;

	/* Sanitize all in-params */
	if (BufferObject == NULL || Buffer == NULL
		|| BytesToWrite == 0) {
		return OsError;
	}

	/* Calculate bytes we are going to write */
	Bytes = MIN(BytesToWrite, BufferObject->Length - BufferObject->IndexWrite);
	*BytesWritten = Bytes;

	/* Do the copy operation */
	memcpy((void*)BufferObject->Virtual, Buffer, Bytes);

	/* Increase Index */
	if (Bytes != BufferObject->Length) {
		BufferObject->IndexWrite += Bytes;
		if (BufferObject->IndexWrite == BufferObject->Length) {
			BufferObject->IndexWrite = 0;
		}
	}

	return OsNoError;
}

/* CombineBuffer 
 * Writes <BytesToTransfer> into the destination from the given
 * source buffer, make sure the position in both buffers are correct.
 * The number of bytes transferred is set as output */
MOSAPI 
OsStatus_t
MOSABI
CombineBuffer(
	_Out_ BufferObject_t *Destination,
	_In_ BufferObject_t *Source,
	_In_ size_t BytesToTransfer,
	_Out_Opt_ size_t *BytesTransferred)
{

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
