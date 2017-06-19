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
 * MollenOS MCore - BufferPool Support Definitions & Structures
 * - This header describes the base bufferpool-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _BUFFERPOOL_INTERFACE_H_
#define _BUFFERPOOL_INTERFACE_H_

/* Includes
 * - System */
#include <os/driver/buffer.h>
#include <os/osdefs.h>

/* BufferPool 
 * */
typedef struct _BufferPool BufferPool_t;

// Cpp guard begin
_CODE_BEGIN

/* BufferPoolCreate
 * Creates a new buffer-pool from the given buffer object. 
 * This allows sub-allocations from a buffer-object. */
MOSAPI
OsStatus_t
MOSABI
BufferPoolCreate(
    _In_ BufferObject_t *Buffer,
    _Out_ BufferPool_t **Pool);

/* BufferPoolDestroy
 * Cleans up the buffer-pool and deallocates resources previously
 * allocated. This does not destroy the buffer-object. */
MOSAPI
OsStatus_t
MOSABI
BufferPoolDestroy(
    _In_ BufferPool_t *Pool);

/* BufferPoolAllocate
 * Allocates the requested size and outputs two addresses. The
 * virtual pointer to the accessible data, and the address of its 
 * corresponding physical address for hardware. */
MOSAPI
OsStatus_t
MOSABI
BufferPoolAllocate(
    _In_ BufferPool_t *Pool,
    _In_ size_t Size,
    _Out_ uintptr_t **VirtualPointer,
    _Out_ uintptr_t *PhysicalAddress);

/* BufferPoolFree
 * Frees previously allocations made by the buffer-pool. The virtual
 * address must be the one passed back. */
MOSAPI
OsStatus_t
MOSABI
BufferPoolFree(
    _In_ BufferPool_t *Pool,
    _In_ uintptr_t *VirtualPointer);

// Cpp guard end
_CODE_END

#endif //!_BUFFERPOOL_INTERFACE_H_
