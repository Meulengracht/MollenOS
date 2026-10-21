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

#include <handle.h>
#include <heap.h>
#include <machine.h>
#include <memoryspace.h>
#include <mutex.h>
#include "private.h"

static void
__CleanupMemoryAllocation(
    _In_ element_t* element,
    _In_ void*      context)
{
    struct MSAllocation* allocation = element->value;
    struct MSContext*    msContext = context;

    DynamicMemoryPoolFree(&msContext->Heap, allocation->Address);
    kfree(allocation);
}

static void
__MSContextDelete(
    _In_ void* resource)
{
    struct MSContext* context = resource;

    MutexDestruct(&context->SyncObject);
    MutexDestruct(&context->UserCopyLock);
    list_clear(&context->Allocations, __CleanupMemoryAllocation, context);
    DynamicMemoryPoolDestroy(&context->Heap);
    kfree(context);
}

uuid_t
MSContextNew(void)
{
    struct MSContext* context;
    uuid_t            handle;

    context = kmalloc(sizeof(struct MSContext));
    if (!context) {
        return UUID_INVALID;
    }

    handle = CreateHandle(HandleTypeMSContext, __MSContextDelete, context);
    if (handle == UUID_INVALID) {
        kfree(context);
        return UUID_INVALID;
    }

    MutexConstruct(&context->SyncObject, MUTEX_FLAG_PLAIN);
    MutexConstruct(&context->UserCopyLock, MUTEX_FLAG_PLAIN);
    DynamicMemoryPoolConstruct(
        &context->Heap,
        GetMachine()->MemoryMap.UserHeap.Start,
        GetMachine()->MemoryMap.UserHeap.Length,
        GetMachine()->MemoryGranularity
    );
    list_construct(&context->Allocations);
    context->SignalHandler = 0;
    context->Security.Handle = handle;
    context->Security.Capabilities = 0;
    return handle;
}

void
MSContextAddAllocation(
    _In_ struct MSContext*    context,
    _In_ struct MSAllocation* allocation)
{
    MutexLock(&context->SyncObject);
    list_append(&context->Allocations, &allocation->Header);
    MutexUnlock(&context->SyncObject);
}

MSContextSecurity_t*
MemorySpaceGetSecurityContext(void)
{
    MemorySpace_t*    memorySpace = GetCurrentMemorySpace();
    struct MSContext* context;

    if (!memorySpace) {
        return NULL;
    }

    context = __MSContextGet(memorySpace);
    return context != NULL ? &context->Security : NULL;
}
