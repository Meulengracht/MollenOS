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
	UUId_t					Id;
	__CRT_CONST char		*Virtual;
	__CRT_CONST char		*Physical;
	size_t					Length;
	int						Pages;
} BufferObject_t;

/* CreateBuffer 
 * Creates a new buffer object with the given size, 
 * this allows hardware drivers to interact with the buffer */
_MOS_API BufferObject_t *CreateBuffer(_In_ size_t Length);

/* ReadBuffer 
 * Reads <BytesToRead> into the given user-buffer
 * from the allocated buffer-object */
_MOS_API OsStatus_t ReadBuffer(_In_ BufferObject_t *BufferObject, 
							   _Out_ __CRT_CONST void *Buffer, 
							   _In_ size_t BytesToRead);

/* WriteBuffer 
 * Writes <BytesToWrite> into the allocated buffer-object
 * from the given user-buffer */
_MOS_API OsStatus_t WriteBuffer(_In_ BufferObject_t *BufferObject, 
							    _In_ __CRT_CONST void *Buffer, 
							    _In_ size_t BytesToWrite);

/* DestroyBuffer 
 * Destroys the given buffer object and release resources
 * allocated with the CreateBuffer function */
_MOS_API OsStatus_t DestroyBuffer(_In_ BufferObject_t *BufferObject);

#endif //!_BUFFER_INTERFACE_H_
