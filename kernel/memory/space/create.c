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
#include <handles/global.h>
#include <handles/handle.h>
#include <heap.h>
#include <string.h>
#include "private.h"

oserr_t
CreateMemorySpace(
    _In_  unsigned int flags,
    _Out_ uuid_t*      msHandleOut,
    _Out_ uuid_t*      securityGroupOut)
{
    MemorySpace_t* memorySpace;
    MemorySpace_t* parent = NULL;
    uuid_t         parentHandle;
    oserr_t        oserr;

    TRACE("CreateMemorySpace(flags=0x%x)", flags);

    memorySpace = MemorySpaceNew(flags);
    if (!memorySpace) {
        return OS_EOOM;
    }

    // Clone the kernel address space so each thread has its own TLS area.
    // User address spaces can be created or inherited.
    if (flags == MEMORY_SPACE_INHERIT) {
        // Kernel clones own their page tables and TLS. They need their own
        // handle too: an AP has no current thread and therefore no parent handle.
        oserr = MmuCloneVirtualSpace(NULL, memorySpace, 0);
        if (oserr != OS_EOK) {
            goto error;
        }
    } else if (flags & MEMORY_SPACE_APPLICATION) {
        // Use the process's root address space as the parent.
        // The kernel address space cannot be a parent here.
        if (flags & MEMORY_SPACE_INHERIT) {
            parent = GetCurrentMemorySpace();
            if (parent != GetDomainMemorySpace()) {
                if (parent->ParentHandle != UUID_INVALID) {
                    parentHandle = parent->ParentHandle;
                } else {
                    parentHandle = GetCurrentMemorySpaceHandle();
                }

                oserr = AcquireHandle(
                    parentHandle,
                    HandleTypeMemorySpace,
                    (void**)&parent
                );
                if (oserr != OS_EOK) {
                    goto error;
                }
                memorySpace->ParentHandle = parentHandle;

                oserr = AcquireGlobalHandle(parent->GroupGlobalHandleID, HandleTypeMSContext, NULL);
                if (oserr != OS_EOK) {
                    goto error;
                }
                memorySpace->GroupGlobalHandleID = parent->GroupGlobalHandleID;
                memcpy(&memorySpace->PlatformData, &parent->PlatformData, sizeof(PlatformMemoryBlock_t));
            } else {
                parent = NULL;
            }
        }

        // The new group supplies this root space's global reference.
        if (memorySpace->ParentHandle == UUID_INVALID) {
            memorySpace->GroupGlobalHandleID = MSContextNew();
            if (memorySpace->GroupGlobalHandleID == UUID_INVALID) {
                oserr = OS_EOOM;
                goto error;
            }
        }

        oserr = MmuCloneVirtualSpace(parent, memorySpace, (flags & MEMORY_SPACE_INHERIT) ? 1 : 0);
        if (oserr != OS_EOK) {
            goto error;
        }
    } else {
        FATAL(FATAL_SCOPE_KERNEL, "Invalid flags parsed in CreateMemorySpace 0x%" PRIxIN "", flags);
    }

    // Creating the handle makes the memory space visible.
    *msHandleOut = CreateHandle(
        HandleTypeMemorySpace,
        NULL,
        (HandleDestructorFn)MemorySpaceDelete,
        memorySpace
    );
    if (*msHandleOut == UUID_INVALID) {
        MemorySpaceDelete(memorySpace);
        return OS_EOOM;
    }

    *securityGroupOut = memorySpace->GroupGlobalHandleID;
    return OS_EOK;

error:
    // A failed clone frees its page tables. Release the references held by this space.
    if (memorySpace->GroupGlobalHandleID != UUID_INVALID) {
        ReleaseGlobalHandle(memorySpace->GroupGlobalHandleID);
    }
    
    if (memorySpace->ParentHandle != UUID_INVALID) {
        DestroyHandle(memorySpace->ParentHandle);
    }

    DynamicMemoryPoolDestroy(&memorySpace->ThreadMemory);
    kfree(memorySpace);
    return oserr;
}
