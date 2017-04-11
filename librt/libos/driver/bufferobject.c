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

/* ZeroBuffer 
 * Clears the entire buffer and resets the internal indexes */
OsStatus_t
ZeroBuffer(
	_In_ BufferObject_t *BufferObject)
{
	// Reset buffer
	memset((void*)BufferObject->Virtual, 0, BufferObject->Length);

	// Reset counters
	BufferObject->IndexWrite = 0;

	// Done
	return OsNoError;
}

/* ReadBuffer 
 * Reads <BytesToRead> into the given user-buffer
 * from the allocated buffer-object */
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
 * the next write will be appended unless BytesToWrite == Size.
 * Index is reset once it returns less bytes written than requested */
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
