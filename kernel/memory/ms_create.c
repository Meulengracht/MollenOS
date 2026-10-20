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
#include <string.h>
#include "private.h"

oserr_t
CreateMemorySpace(
        _In_  unsigned int flags,
        _Out_ uuid_t*      handleOut)
{
    MemorySpace_t* memorySpace;
    oserr_t        oserr;
    TRACE("CreateMemorySpace(flags=0x%x)", flags);

    memorySpace = MemorySpaceNew(flags);
    if (!memorySpace) {
        return OS_EOOM;
    }

    // We must handle two cases here, either we inherit the kernels address-space, or we are
    // inheritting/creating a new userspace address-space. If we are inheritting the kernel
    // address-space, we clone it as we still need TLS data-areas in each kernel thread.
    if (flags == MEMORY_SPACE_INHERIT) {
        // Kernel clones own their page tables and TLS. They need their own
        // handle too: an AP has no current thread and therefore no parent handle.
        oserr = MmuCloneVirtualSpace(NULL, memorySpace, 0);
        if (oserr != OS_EOK) {
            DynamicMemoryPoolDestroy(&memorySpace->ThreadMemory);
            kfree(memorySpace);
            return oserr;
        }

        *handleOut = CreateHandle(
            HandleTypeMemorySpace,
            (HandleDestructorFn)MemorySpaceDelete,
            memorySpace
        );
        if (*handleOut == UUID_INVALID) {
            MemorySpaceDelete(memorySpace);
            return OS_EOOM;
        }
    } else if (flags & MEMORY_SPACE_APPLICATION) {
        MemorySpace_t* parent = NULL;

        // Parent must be the uppermost instance of the address-space
        // of the process. Only to the point of not having kernel as parent
        if (flags & MEMORY_SPACE_INHERIT) {
            parent = GetCurrentMemorySpace();
            if (parent != GetDomainMemorySpace()) {
                if (parent->ParentHandle != UUID_INVALID) {
                    memorySpace->ParentHandle = parent->ParentHandle;
                    memorySpace->Context      = parent->Context;
                    parent = (MemorySpace_t*)LookupHandleOfType(
                            parent->ParentHandle, HandleTypeMemorySpace);
                } else {
                    memorySpace->ParentHandle = GetCurrentMemorySpaceHandle();
                    memorySpace->Context      = parent->Context;
                }

                // Add a reference and copy data
                AcquireHandleOfType(
                        memorySpace->ParentHandle,
                        HandleTypeMemorySpace,
                        NULL
                );
                memcpy(&memorySpace->PlatformData, &parent->PlatformData, sizeof(PlatformMemoryBlock_t));
            } else {
                parent = NULL;
            }
        }

        // If we are root, create the memory bitmaps
        if (memorySpace->ParentHandle == UUID_INVALID) {
            memorySpace->Context = MSContextNew();
        }

        oserr = MmuCloneVirtualSpace(parent, memorySpace, (flags & MEMORY_SPACE_INHERIT) ? 1 : 0);
        if (oserr != OS_EOK) {
            return oserr;
        }

        *handleOut = CreateHandle(
                HandleTypeMemorySpace,
                (HandleDestructorFn)MemorySpaceDelete,
                memorySpace
        );
    } else {
        FATAL(FATAL_SCOPE_KERNEL, "Invalid flags parsed in CreateMemorySpace 0x%" PRIxIN "", flags);
    }
    return OS_EOK;
}
