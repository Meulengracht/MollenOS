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
        // When cloning kernel memory spaces, we initially just copy the platform block
        // for the new memory space, and then let the Platfrom call correct anything.
        if (GetCurrentMemorySpace() != NULL) {
            memcpy(
                    &memorySpace->PlatformData,
                    &GetCurrentMemorySpace()->PlatformData,
                    sizeof(PlatformMemoryBlock_t)
            );
        }

        // It doesn't matter which parent we take, they all map the exact same kernel segments
        // so just pass in the current memory space
        oserr = MmuCloneVirtualSpace(
                GetCurrentMemorySpace(),
                memorySpace,
                (flags & MEMORY_SPACE_INHERIT) ? 1 : 0
        );
        if (oserr != OS_EOK) {
            return oserr;
        }

        *handleOut = GetCurrentMemorySpaceHandle();
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
