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

/* CreateBuffer 
 * Creates a new buffer object with the given size, 
 * this allows hardware drivers to interact with the buffer */
BufferObject_t *CreateBuffer(_In_ size_t Length)
{

}

/* ReadBuffer 
 * Reads <BytesToRead> into the given user-buffer
 * from the allocated buffer-object */
OsStatus_t ReadBuffer(_In_ BufferObject_t *BufferObject, 
					  _Out_ __CONST void *Buffer, 
					  _In_ size_t BytesToRead)
{

}

/* WriteBuffer 
 * Writes <BytesToWrite> into the allocated buffer-object
 * from the given user-buffer */
OsStatus_t WriteBuffer(_In_ BufferObject_t *BufferObject, 
					   _In_ __CONST void *Buffer, 
					   _In_ size_t BytesToWrite)
{

}

/* DestroyBuffer 
 * Destroys the given buffer object and release resources
 * allocated with the CreateBuffer function */
OsStatus_t DestroyBuffer(_In_ BufferObject_t *BufferObject)
{

}
