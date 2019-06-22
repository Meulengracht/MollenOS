/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Memory Buffer Interface
 * - Implementation of the memory dma buffers. This provides a transfer buffer
 *   that is not bound to any specific virtual memory area but instead are bound
 *   to fixed physical addreses.
 */

#ifndef __MEMORY_BUFFER_INTERFACE__
#define __MEMORY_BUFFER_INTERFACE__

#include <os/osdefs.h>
#include <memoryspace.h>

typedef struct {
    int       BlockCount;
    uintptr_t Blocks[1];
} BlockVector_t;

/* CreateMemoryBuffer 
 * Creates a new memory buffer instance of the given size. The allocation
 * of resources happens at this call, and reference is set to 1.  */
KERNELAPI OsStatus_t KERNELABI
CreateMemoryBuffer(
    _In_  uintptr_t* DmaVector,
    _In_  int        EntryCount,
    _Out_ UUId_t*    HandleOut);

/* DestroyMemoryBuffer
 * Cleans up the resources associated with the handle. This function is registered
 * with the handle manager. */
KERNELAPI OsStatus_t KERNELABI
DestroyMemoryBuffer(
    _In_  void* Resource);

#endif //! __MEMORY_BUFFER_INTERFACE__
