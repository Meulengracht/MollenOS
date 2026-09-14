/**
 * Copyright 2026, Philip Meulengracht
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

//#define __TRACE

#include <assert.h>
#include <ddk/utils.h>
#include <os/mollenos.h>
#include <os/shm.h>
#include <string.h>
#include "scheduler.h"

oserr_t
UsbTransferArenaResetInternalData(
    _In_ UsbTransferArena_t* arena,
    _In_ int                 resetElements)
{
    if (!resetElements) {
        return OS_EOK;
    }

    for (int i = 0; i < arena->Settings->PoolCount; i++) {
        memset(
                arena->Settings->Pools[i].ElementPool,
                0,
                arena->Settings->Pools[i].ElementCount * arena->Settings->Pools[i].ElementAlignedSize
        );

        for (int j = 0; j < arena->Settings->Pools[i].ElementCountReserved; j++) {
            uint8_t*              element = USB_ELEMENT_INDEX((&arena->Settings->Pools[i]), j);
            UsbSchedulerObject_t* object  = USB_ELEMENT_OBJECT((&arena->Settings->Pools[i]), element);

            object->Index       = USB_ELEMENT_CREATE_INDEX(i, j);
            object->BreathIndex = USB_ELEMENT_NO_INDEX;
            object->DepthIndex  = USB_ELEMENT_NO_INDEX;
            object->Flags       = USB_ELEMENT_ALLOCATED;
        }
    }
    return OS_EOK;
}

uintptr_t
UsbTransferArenaGetDma(
    _In_ UsbSchedulerPool_t* arenaPool,
    _In_ const uint8_t*      elementPointer)
{
    size_t offset = (uintptr_t)elementPointer - (uintptr_t)SHMBuffer(&arenaPool->ElementPoolDMA);

    for (int i = 0; i < arenaPool->ElementPoolDMATable.Count; i++) {
        if (offset < arenaPool->ElementPoolDMATable.Entries[i].Length) {
            return arenaPool->ElementPoolDMATable.Entries[i].Address + offset;
        }
        offset -= arenaPool->ElementPoolDMATable.Entries[i].Length;
    }
    return 0;
}

oserr_t
UsbTransferArenaGetPoolElement(
    _In_  UsbTransferArena_t* arena,
    _In_  int                 Pool,
    _In_  int                 Index,
    _Out_ uint8_t**           ElementOut,
    _Out_ uintptr_t*          ElementPhysicalOut)
{
    assert(Pool < arena->Settings->PoolCount);
    if (ElementOut != NULL) {
        *ElementOut = USB_ELEMENT_INDEX((&arena->Settings->Pools[Pool]), Index);
    }
    if (ElementPhysicalOut != NULL) {
        *ElementPhysicalOut = USB_ELEMENT_PHYSICAL((&arena->Settings->Pools[Pool]), Index);
    }
    return OS_EOK;
}

oserr_t
UsbTransferArenaGetPoolFromElement(
    _In_  UsbTransferArena_t*  arena,
    _In_  const uint8_t*       element,
    _Out_ UsbSchedulerPool_t** poolOut)
{
    for (int i = 0; i < arena->Settings->PoolCount; i++) {
        uintptr_t poolStart = (uintptr_t)arena->Settings->Pools[i].ElementPool;
        uintptr_t poolEnd   = poolStart + (arena->Settings->Pools[i].ElementAlignedSize * arena->Settings->Pools[i].ElementCount);
        if (ISINRANGE((uintptr_t)element, poolStart, poolEnd)) {
            *poolOut = &arena->Settings->Pools[i];
            return OS_EOK;
        }
    }
    return OS_EUNKNOWN;
}

oserr_t
UsbTransferArenaAllocateElement(
    _In_  UsbTransferArena_t* arena,
    _In_  int                 Pool,
    _Out_ uint8_t**           ElementOut)
{
    UsbSchedulerObject_t* object = NULL;
    UsbSchedulerPool_t*   pool   = NULL;
    size_t                i;

    assert(ElementOut != NULL);
    assert(Pool < arena->Settings->PoolCount);
    pool = &arena->Settings->Pools[Pool];
    *ElementOut = NULL;

    spinlock_acquire(arena->Lock);
    for (i = pool->ElementCountReserved; i < pool->ElementCount; i++) {
        uint8_t* element = USB_ELEMENT_INDEX(pool, i);
        object           = USB_ELEMENT_OBJECT(pool, element);
        if (object->Flags & USB_ELEMENT_ALLOCATED) {
            continue;
        }

        memset(element, 0, pool->ElementAlignedSize);
        object->Index       = USB_ELEMENT_CREATE_INDEX(Pool, i);
        object->BreathIndex = USB_ELEMENT_NO_INDEX;
        object->DepthIndex  = USB_ELEMENT_NO_INDEX;
        object->Flags       = USB_ELEMENT_ALLOCATED;
        *ElementOut         = element;
        break;
    }
    spinlock_release(arena->Lock);
    return (i == pool->ElementCount) ? OS_ENOENT : OS_EOK;
}

void
UsbTransferArenaFreeElement(
    _In_ UsbTransferArena_t* arena,
    _In_ uint8_t*            element)
{
    UsbSchedulerPool_t* pool  = NULL;
    oserr_t            oserr = UsbTransferArenaGetPoolFromElement(arena, element, &pool);
    if (oserr != OS_EOK) {
        WARNING("UsbTransferArenaFreeElement: cannot get object from pool");
        return;
    }
    memset(element, 0, pool->ElementAlignedSize);
}

uintptr_t
UsbSchedulerGetDma(
    _In_ UsbSchedulerPool_t* schedulerPool,
    _In_ const uint8_t*      elementPointer)
{
    return UsbTransferArenaGetDma(schedulerPool, elementPointer);
}

oserr_t
UsbSchedulerGetPoolElement(
    _In_  UsbScheduler_t* Scheduler,
    _In_  int             Pool,
    _In_  int             Index,
    _Out_ uint8_t**       ElementOut,
    _Out_ uintptr_t*      ElementPhysicalOut)
{
    return UsbTransferArenaGetPoolElement(UsbSchedulerGetTransferArena(Scheduler), Pool, Index, ElementOut, ElementPhysicalOut);
}

oserr_t
UsbSchedulerGetPoolFromElement(
    _In_  UsbScheduler_t*      scheduler,
    _In_  const uint8_t*       element,
    _Out_ UsbSchedulerPool_t** poolOut)
{
    return UsbTransferArenaGetPoolFromElement(UsbSchedulerGetTransferArena(scheduler), element, poolOut);
}

oserr_t
UsbSchedulerAllocateElement(
    _In_  UsbScheduler_t* Scheduler,
    _In_  int             Pool,
    _Out_ uint8_t**       ElementOut)
{
    return UsbTransferArenaAllocateElement(UsbSchedulerGetTransferArena(Scheduler), Pool, ElementOut);
}
