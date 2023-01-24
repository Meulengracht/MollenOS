/**
 * Copyright 2023, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <arch/mmu.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <machine.h>
#include <string.h>
#include "private.h"

MemorySpace_t*
MemorySpaceNew(
        _In_ unsigned int flags)
{
    uintptr_t      threadRegionStart;
    size_t         threadRegionSize;
    MemorySpace_t* memorySpace;
    TRACE("__NewMemorySpace(flags=0x%x)", flags);

    memorySpace = (MemorySpace_t*)kmalloc(sizeof(MemorySpace_t));
    if (!memorySpace) {
        return NULL;
    }
    memset((void*)memorySpace, 0, sizeof(MemorySpace_t));

    threadRegionStart = GetMachine()->MemoryMap.ThreadLocal.Start;
    threadRegionSize  = GetMachine()->MemoryMap.ThreadLocal.Length + 1;

    memorySpace->Flags        = flags;
    memorySpace->ParentHandle = UUID_INVALID;
    DynamicMemoryPoolConstruct(
            &memorySpace->ThreadMemory,
            threadRegionStart,
            threadRegionSize,
            GetMemorySpacePageSize()
    );
    return memorySpace;
}

void
MemorySpaceDelete(
        _In_ MemorySpace_t* memorySpace)
{
    if (!memorySpace) {
        return;
    }

    if (memorySpace->Flags & MEMORY_SPACE_APPLICATION) {
        DynamicMemoryPoolDestroy(&memorySpace->ThreadMemory);
        MmuDestroyVirtualSpace(memorySpace);
    }
    if (memorySpace->ParentHandle == UUID_INVALID) {
        MSContextDelete(memorySpace->Context);
    }
    if (memorySpace->ParentHandle != UUID_INVALID) {
        DestroyHandle(memorySpace->ParentHandle);
    }
    kfree(memorySpace);
}
