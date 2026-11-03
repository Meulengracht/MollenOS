/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

//#define __TRACE
#define __need_static_assert

#include <assert.h>
#include <ddk/utils.h>
#include <os/mollenos.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler.h"

COMPILE_TIME_ASSERT(sizeof(UsbSchedulerObject_t) == 18);

static oserr_t
__AllocatePoolMemory(
    _In_ struct DMAContext*   context,
    _In_ UsbSchedulerPool_t*  pool,
    _In_ bool                 softwareElements)
{
    struct DMARequirements   requirements = { 0 };
    const struct DMASegment* segments;
    uint32_t                 count;
    oserr_t                  status;
    size_t                   bytes;

    // get number of bytes required for the pool
    bytes = pool->ElementCount * pool->ElementAlignedSize;

    if (softwareElements) {
        // These descriptors are never read by hardware. Keep metadata cached
        // and page aligned without creating a SHM object or a DMA mapping.
        // TODO: read the actual page size from the system instead of hardcoding 4096
        pool->ElementPool = aligned_alloc(4096, (bytes + 4095) & ~(size_t)4095);
        return pool->ElementPool != NULL ? OS_EOK : OS_EOOM;
    }

    // Keep the pool in one DMA segment so each descriptor address can be
    // calculated from the pool base and its offset.
    requirements.Length = bytes;
    // TODO: read the actual page size from the system instead of hardcoding 4096
    requirements.Alignment = 4096;
    requirements.Contiguous = true;

    status = DMAAllocate(context, &requirements, &pool->ElementPoolDMA);
    if (status != OS_EOK) {
        ERROR("USB scheduler: descriptor allocation failed (%u)", status);
        return status;
    }

    segments = DMABufferSegments(pool->ElementPoolDMA, &count);
    pool->ElementPool = DMABufferData(pool->ElementPoolDMA);
    pool->ElementPoolDeviceAddress = (uintptr_t)segments[0].Address;
    return OS_EOK;
}

static oserr_t
__AllocateFrameMemory(
    _In_ UsbScheduler_t* scheduler)
{
    struct DMARequirements requirements = {
        .Length = scheduler->Settings.FrameCount * sizeof(reg32_t),
        // TODO: read the actual page size from the system instead of hardcoding 4096
        .Alignment = 4096,
        .Contiguous = true
    };
    const struct DMASegment* segments;
    uint32_t                 count;
    oserr_t                  status;

    status = DMAAllocate(scheduler->Settings.DmaContext, &requirements, &scheduler->Settings.FrameListDMA);
    if (status != OS_EOK) {
        ERROR("USB scheduler: frame-list allocation failed (%u)", status);
        return status;
    }

    segments = DMABufferSegments(scheduler->Settings.FrameListDMA, &count);
    scheduler->Settings.FrameList = DMABufferData(scheduler->Settings.FrameListDMA);
    scheduler->Settings.FrameListPhysical = (uintptr_t)segments[0].Address;
    return OS_EOK;
}

oserr_t
UsbSchedulerResetInternalData(
    _In_ UsbScheduler_t* Scheduler,
    _In_ int             ResetElements,
    _In_ int             ResetFramelist)
{
    oserr_t oserr;

    oserr = UsbTransferArenaResetInternalData(
        UsbSchedulerGetTransferArena(Scheduler),
        ResetElements
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    return UsbPeriodicSchedulerResetInternalData(
        UsbSchedulerGetPeriodicScheduler(Scheduler),
        ResetFramelist
    );
}

oserr_t
UsbSchedulerInitialize(
    _In_  UsbSchedulerSettings_t* Settings,
    _Out_ UsbScheduler_t**        SchedulerOut)
{
    UsbScheduler_t* scheduler;
    oserr_t         status;

    assert(Settings->PoolCount > 0);
    if (Settings->Flags & USB_SCHEDULER_PERIODIC) {
        assert(Settings->FrameCount > 0);
        assert(Settings->SubframeCount > 0);
    }

    scheduler = malloc(sizeof(UsbScheduler_t));
    if (!scheduler) {
        return OS_EOOM;
    }

    memset(scheduler, 0, sizeof(UsbScheduler_t));
    spinlock_init(&scheduler->Lock);
    memcpy(&scheduler->Settings, Settings, sizeof(UsbSchedulerSettings_t));

    // Set the output before allocating DMA buffers so the controller can clean
    // up after a setup failure and retry cleanup if DMAFree fails.
    *SchedulerOut = scheduler;

    scheduler->TransferArena.Settings = &scheduler->Settings;
    scheduler->TransferArena.Lock     = &scheduler->Lock;
    scheduler->Periodic.Settings      = &scheduler->Settings;

    if (scheduler->Settings.Flags & USB_SCHEDULER_FRAMELIST) {
        status = __AllocateFrameMemory(scheduler);
        if (status != OS_EOK) {
            return status;
        }
    }

    if ((scheduler->Settings.Flags & (USB_SCHEDULER_PERIODIC | USB_SCHEDULER_FL64)) == USB_SCHEDULER_PERIODIC) {
        if ((scheduler->Settings.FrameListPhysical + (scheduler->Settings.FrameCount * sizeof(reg32_t))) > 0xFFFFFFFF) {
            ERROR("Failed to allocate memory below 4gb memory for usb resources");
            return OS_EUNKNOWN;
        }
    }

    for (int i = 0; i < scheduler->Settings.PoolCount; i++) {
        status = __AllocatePoolMemory(
            scheduler->Settings.DmaContext,
            &scheduler->Settings.Pools[i],
            (scheduler->Settings.Flags & USB_SCHEDULER_SOFTWARE_ELEMENTS) != 0
        );
        if (status != OS_EOK) {
            return status;
        }
    }

    if (scheduler->Settings.Flags & USB_SCHEDULER_PERIODIC) {
        scheduler->Periodic.VirtualFrameList = malloc(Settings->FrameCount * sizeof(uintptr_t));
        scheduler->Periodic.Bandwidth        = malloc(Settings->FrameCount * Settings->SubframeCount * sizeof(size_t));
        if (!scheduler->Periodic.VirtualFrameList || !scheduler->Periodic.Bandwidth) {
            return OS_EOOM;
        }
    }

    return UsbSchedulerResetInternalData(scheduler, 1, 1);
}

oserr_t
UsbSchedulerDestroy(
    _InOut_ UsbScheduler_t** schedulerSlot)
{
    UsbScheduler_t*     scheduler = *schedulerSlot;
    UsbSchedulerPool_t* pool;
    oserr_t             status;

    if (scheduler == NULL) {
        return OS_EOK;
    }

    // Hardware must already be stopped. DMAFree clears each buffer pointer on
    // success, so cleanup can be retried. Free the scheduler after all buffers.
    status = DMAFree(&scheduler->Settings.FrameListDMA);
    if (status != OS_EOK) {
        return status;
    }
    
    for (int i = 0; i < scheduler->Settings.PoolCount; i++) {
        pool = &scheduler->Settings.Pools[i];
        if (scheduler->Settings.Flags & USB_SCHEDULER_SOFTWARE_ELEMENTS) {
            free(pool->ElementPool);
        } else {
            status = DMAFree(&pool->ElementPoolDMA);
            if (status != OS_EOK) {
                return status;
            }
        }
        pool->ElementPool = NULL;
        pool->ElementPoolDeviceAddress = 0;
    }
    
    free(scheduler->Periodic.VirtualFrameList);
    free(scheduler->Periodic.Bandwidth);
    free(scheduler);
    
    *schedulerSlot = NULL;
    return OS_EOK;
}
