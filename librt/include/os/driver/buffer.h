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

/* BufferObject Structure
 * This is the way to interact with transfer
 * buffers throughout the system, must be used
 * for any hardware transactions */
typedef struct _BufferObject {
	__CONST char			*Virtual;
	uintptr_t				 Physical;
	size_t					 Length;
	size_t					 Capacity;
	size_t					 IndexWrite;
} BufferObject_t;

/* Start one of these before function prototypes */
_CODE_BEGIN

/* CreateBuffer 
 * Creates a new buffer object with the given size, 
 * this allows hardware drivers to interact with the buffer */
MOSAPI 
BufferObject_t*
MOSABI
CreateBuffer(
	_In_ size_t Length);

/* ZeroBuffer 
 * Clears the entire buffer and resets the internal indexes */
MOSAPI 
OsStatus_t
MOSABI
ZeroBuffer(
	_In_ BufferObject_t *BufferObject);

/* ReadBuffer 
 * Reads <BytesToRead> into the given user-buffer
 * from the allocated buffer-object */
MOSAPI 
OsStatus_t
MOSABI
ReadBuffer(
	_In_ BufferObject_t *BufferObject, 
	_Out_ __CONST void *Buffer, 
	_In_ size_t BytesToRead);

/* WriteBuffer 
 * Writes <BytesToWrite> into the allocated buffer-object
 * from the given user-buffer. It uses indexed writes, so
 * the next write will be appended unless BytesToWrite == Size.
 * Index is reset once it returns less bytes written than requested */
MOSAPI 
OsStatus_t
MOSABI
WriteBuffer(
	_In_ BufferObject_t *BufferObject, 
	_In_ __CONST void *Buffer, 
	_In_ size_t BytesToWrite,
	_Out_Opt_ size_t *BytesWritten);

/* DestroyBuffer 
 * Destroys the given buffer object and release resources
 * allocated with the CreateBuffer function */
MOSAPI 
OsStatus_t
MOSABI
DestroyBuffer(
	_In_ BufferObject_t *BufferObject);

_CODE_END

#endif //!_BUFFER_INTERFACE_H_
