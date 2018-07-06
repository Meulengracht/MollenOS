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
 * MollenOS System Component Infrastructure 
 * - The Memory component. This component has the task of managing
 *   different memory regions that map to physical components
 */

#include <machine.h>

/* AllocateSystemMemory 
 * Allocates a block of system memory with the given parameters. It's possible
 * to allocate low memory, local memory, global memory or standard memory. */
uintptr_t
AllocateSystemMemory(
    _In_ size_t     Size,
    _In_ uintptr_t  Mask,
    _In_ Flags_t    Flags)
{
    // NUMA domain specific memory?
    if (Flags & MEMORY_DOMAIN) {
        // GetCurrentDomain()->Memory.MemoryRange
    }
    return AllocateBlocksInBlockmap(&GetMachine()->PhysicalMemory, Mask, Size);
}

/* FreeSystemMemory
 * Releases the given memory address of the given size. This can return OsError
 * if the memory was not already allocated or address is invalid. */
OsStatus_t
FreeSystemMemory(
    _In_ uintptr_t  Address,
    _In_ size_t     Size)
{
    // No need to handle domain spaces as we free the same place.
    return ReleaseBlockmapRegion(&GetMachine()->PhysicalMemory, Address, Size);
}
