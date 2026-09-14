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
#include <os/handle.h>
#include <os/mollenos.h>
#include <os/shm.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler.h"

COMPILE_TIME_ASSERT(sizeof(UsbSchedulerObject_t) == 18);

static oserr_t
__AllocatePoolMemory(
    _In_ UsbSchedulerPool_t* schedulerPool)
{
    size_t  elementBytes = schedulerPool->ElementCount * schedulerPool->ElementAlignedSize;
    oserr_t oserr;

    oserr = SHMCreate(
            &(SHM_t) {
                .Flags = SHM_DEVICE | SHM_PRIVATE,
                .Conformity = OSMEMORYCONFORMITY_LOW,
                .Size = elementBytes,
                .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE
            },
            &schedulerPool->ElementPoolDMA
    );
    if (oserr != OS_EOK) {
        ERROR("__AllocatePoolMemory: cannot allocate memory region for pool: %u", oserr);
        return oserr;
    }

    oserr = SHMGetSGTable(&schedulerPool->ElementPoolDMA, &schedulerPool->ElementPoolDMATable, -1);
    if (oserr != OS_EOK) {
        ERROR("__AllocatePoolMemory: cannot retrieve SG table for memory region: %u", oserr);
        return oserr;
    }

    schedulerPool->ElementPool = SHMBuffer(&schedulerPool->ElementPoolDMA);
    return OS_EOK;
}

static oserr_t
__AllocateFrameMemory(
    _In_ UsbScheduler_t* scheduler)
{
    size_t  frameSize = scheduler->Settings.FrameCount * sizeof(reg32_t);
    oserr_t oserr;

    oserr = SHMCreate(
            &(SHM_t) {
                .Flags = SHM_DEVICE | SHM_PRIVATE | SHM_CLEAN,
                .Conformity = OSMEMORYCONFORMITY_LOW,
                .Size = frameSize,
                .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE
            },
            &scheduler->Settings.FrameListDMA
    );
    if (oserr != OS_EOK) {
        ERROR("__AllocateFrameMemory: cannot allocate memory region: %u", oserr);
        return oserr;
    }

    oserr = SHMGetSGTable(&scheduler->Settings.FrameListDMA, &scheduler->Settings.FrameListDMATable, -1);
    if (oserr != OS_EOK) {
        ERROR("__AllocateFrameMemory: cannot retrieve SG table for memory region: %u", oserr);
        return oserr;
    }

    scheduler->Settings.FrameList         = (reg32_t*)SHMBuffer(&scheduler->Settings.FrameListDMA);
    scheduler->Settings.FrameListPhysical = scheduler->Settings.FrameListDMATable.Entries[0].Address;
    return OS_EOK;
}

oserr_t
UsbSchedulerResetInternalData(
    _In_ UsbScheduler_t* Scheduler,
    _In_ int             ResetElements,
    _In_ int             ResetFramelist)
{
    oserr_t oserr;

    oserr = UsbTransferArenaResetInternalData(UsbSchedulerGetTransferArena(Scheduler), ResetElements);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return UsbPeriodicSchedulerResetInternalData(UsbSchedulerGetPeriodicScheduler(Scheduler), ResetFramelist);
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

    scheduler->TransferArena.Settings = &scheduler->Settings;
    scheduler->TransferArena.Lock     = &scheduler->Lock;
    scheduler->Periodic.Settings      = &scheduler->Settings;

    if (scheduler->Settings.Flags & USB_SCHEDULER_FRAMELIST) {
        status = __AllocateFrameMemory(scheduler);
        if (status != OS_EOK) {
            UsbSchedulerDestroy(scheduler);
            return status;
        }
    }

    if ((scheduler->Settings.Flags & (USB_SCHEDULER_PERIODIC | USB_SCHEDULER_FL64)) == USB_SCHEDULER_PERIODIC) {
        if ((scheduler->Settings.FrameListPhysical + (scheduler->Settings.FrameCount * sizeof(reg32_t))) > 0xFFFFFFFF) {
            ERROR("Failed to allocate memory below 4gb memory for usb resources");
            UsbSchedulerDestroy(scheduler);
            return OS_EUNKNOWN;
        }
    }

    for (int i = 0; i < scheduler->Settings.PoolCount; i++) {
        status = __AllocatePoolMemory(&scheduler->Settings.Pools[i]);
        if (status != OS_EOK) {
            UsbSchedulerDestroy(scheduler);
            return status;
        }
    }

    if (scheduler->Settings.Flags & USB_SCHEDULER_PERIODIC) {
        scheduler->Periodic.VirtualFrameList = malloc(Settings->FrameCount * sizeof(uintptr_t));
        scheduler->Periodic.Bandwidth        = malloc(Settings->FrameCount * Settings->SubframeCount * sizeof(size_t));
        if (!scheduler->Periodic.VirtualFrameList || !scheduler->Periodic.Bandwidth) {
            UsbSchedulerDestroy(scheduler);
            return OS_EOOM;
        }
    }

    *SchedulerOut = scheduler;
    return UsbSchedulerResetInternalData(scheduler, 1, 1);
}

static void
__FreePoolMemory(
    _In_ UsbSchedulerPool_t* Pool)
{
    OSHandleDestroy(&Pool->ElementPoolDMA);
    free(Pool->ElementPoolDMATable.Entries);
}

static void
__FreeFrameListMemory(
    _In_ UsbScheduler_t* Scheduler)
{
    OSHandleDestroy(&Scheduler->Settings.FrameListDMA);
    free(Scheduler->Settings.FrameListDMATable.Entries);
}

void
UsbSchedulerDestroy(
    _In_ UsbScheduler_t* Scheduler)
{
    if (Scheduler == NULL) {
        return;
    }

    if (Scheduler->Settings.Flags & USB_SCHEDULER_FRAMELIST) {
        __FreeFrameListMemory(Scheduler);
    }

    for (int i = 0; i < Scheduler->Settings.PoolCount; i++) {
        __FreePoolMemory(&Scheduler->Settings.Pools[i]);
    }

    free(Scheduler->Periodic.VirtualFrameList);
    free(Scheduler->Periodic.Bandwidth);
    free(Scheduler);
}
