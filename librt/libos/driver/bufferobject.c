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
#include <os/syscall.h>

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
	Buffer = (BufferObject_t*)malloc(sizeof(BufferObject_t));
	Buffer->Length = Length;
	Result = (OsStatus_t)Syscall1(SYSCALL_BUFFERCREATE, SYSCALL_PARAM(Buffer));

	/* Sanitize the result and 
	 * return the newly created object */
	if (Result != OsNoError) {
		free(Buffer);
		return NULL;
	}
	else {
		return Buffer;
	}
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
	/* Variables */
	size_t BytesNormalized = 0;

	/* Sanitize all in-params */
	if (BufferObject == NULL || Buffer == NULL
		|| BytesToRead == 0) {
		return OsError;
	}

	/* Normalize and read */
	BytesNormalized = MIN(BytesToRead, BufferObject->Length);
	memcpy((void*)Buffer, BufferObject->Virtual, BytesNormalized);
	return OsNoError;
}

/* WriteBuffer 
 * Writes <BytesToWrite> into the allocated buffer-object
 * from the given user-buffer */
OsStatus_t 
WriteBuffer(
	_In_ BufferObject_t *BufferObject, 
	_In_ __CONST void *Buffer, 
	_In_ size_t BytesToWrite)
{
	/* Sanitize all in-params */
	if (BufferObject == NULL || Buffer == NULL
		|| BytesToWrite == 0 || BytesToWrite > BufferObject->Length) {
		return OsError;
	}

	/* Do the copy operation */
	memcpy((void*)BufferObject->Virtual, Buffer, BytesToWrite);
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
	BufferObject_t *Buffer = NULL;
	OsStatus_t Result;

	/* Sanitize the length */
	if (BufferObject == NULL) {
		return OsError;
	}

	/* Contact OS services and release buffer */
	Result = (OsStatus_t)Syscall1(SYSCALL_BUFFERDESTROY, SYSCALL_PARAM(Buffer));
	free(Buffer);
	return Result;
}
