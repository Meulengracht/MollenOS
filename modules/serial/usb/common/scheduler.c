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
 *
 *
 * USB Controller Scheduler
 * - Contains the implementation of a shared controller scheduker
 *   for all the usb drivers
 */

//#define __TRACE
#define __need_static_assert
#define __need_minmax
#include <assert.h>
#include <usb/usb.h>
#include <ddk/utils.h>
#include <os/mollenos.h>
#include <os/handle.h>
#include <os/shm.h>
#include "scheduler.h"
#include <stdlib.h>
#include <string.h>

// Data assertions
COMPILE_TIME_ASSERT(sizeof(UsbSchedulerObject_t) == 18);

oserr_t
UsbSchedulerResetInternalData(
    _In_ UsbScheduler_t* Scheduler,
    _In_ int             ResetElements,
    _In_ int             ResetFramelist)
{
    int i, j;

    // Debug
    TRACE("UsbSchedulerInitialize()");

    // Start out by zeroing out memory
    if (ResetElements) {
        for (i = 0; i < Scheduler->Settings.PoolCount; i++) {
            memset((void*)Scheduler->Settings.Pools[i].ElementPool, 0, (Scheduler->Settings.Pools[i].ElementCount * Scheduler->Settings.Pools[i].ElementAlignedSize));
            
            // Allocate and initialze all the reserved elements
            for (j = 0; j < Scheduler->Settings.Pools[i].ElementCountReserved; j++) {
                uint8_t *Element              = USB_ELEMENT_INDEX((&Scheduler->Settings.Pools[i]), j);
                UsbSchedulerObject_t *sObject = USB_ELEMENT_OBJECT((&Scheduler->Settings.Pools[i]), Element);
                sObject->Index                = USB_ELEMENT_CREATE_INDEX(i, j);
                sObject->BreathIndex          = USB_ELEMENT_NO_INDEX;
                sObject->DepthIndex           = USB_ELEMENT_NO_INDEX;
                sObject->Flags                = USB_ELEMENT_ALLOCATED;
            }
        }
    }
    if (ResetFramelist) {
        reg32_t NoLink = (Scheduler->Settings.Flags & USB_SCHEDULER_LINK_BIT_EOL) ? USB_ELEMENT_LINK_END : 0;
        memset((void*)Scheduler->Settings.FrameList, 0, (Scheduler->Settings.FrameCount * 4));
        memset((void*)Scheduler->VirtualFrameList, 0, (Scheduler->Settings.FrameCount * sizeof(uintptr_t)));
        memset((void*)Scheduler->Bandwidth, 0, (Scheduler->Settings.FrameCount * Scheduler->Settings.SubframeCount * sizeof(size_t)));
        for (i = 0; i < Scheduler->Settings.FrameCount; i++) {
            Scheduler->Settings.FrameList[i] = NoLink;
        }
    }
    return OS_EOK;
}

static oserr_t
__AllocatePoolMemory(
    _In_ UsbSchedulerPool_t* schedulerPool)
{
    size_t  elementBytes = schedulerPool->ElementCount * schedulerPool->ElementAlignedSize;
    oserr_t oserr;
    TRACE("__AllocatePoolMemory(size=%" PRIuIN ")", elementBytes);
        
    // Setup required memory allocation flags
    // Require low memory as most usb controllers don't work with physical memory above 2GB
    // Require uncacheable memory as it's hardware accessible memory.
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

    oserr = SHMGetSGTable(
            &schedulerPool->ElementPoolDMA,
            &schedulerPool->ElementPoolDMATable,
            -1
    );
    if (oserr != OS_EOK) {
        ERROR("__AllocatePoolMemory: cannot retrieve SG table for memory region: %u", oserr);
        return oserr;
    }

    schedulerPool->ElementPool = SHMBuffer(&schedulerPool->ElementPoolDMA);
    return OS_EOK;
}

static oserr_t
__AllocateFrameMemory(
    _In_ UsbScheduler_t* usbScheduler)
{
    size_t  frameSize = usbScheduler->Settings.FrameCount * 4;
    oserr_t oserr;
    TRACE("__AllocateFrameMemory(size=%" PRIuIN ")", frameSize);
    
    // Setup required memory allocation flags
    // Require low memory as most usb controllers don't work with physical memory above 2GB
    // Require uncacheable memory as it's hardware accessible memory.
    oserr = SHMCreate(
            &(SHM_t) {
                    .Flags = SHM_DEVICE | SHM_PRIVATE | SHM_CLEAN,
                    .Conformity = OSMEMORYCONFORMITY_LOW,
                    .Size = frameSize,
                    .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE
            },
            &usbScheduler->Settings.FrameListDMA
    );
    if (oserr != OS_EOK) {
        ERROR("__AllocateFrameMemory: cannot allocate memory region: %u", oserr);
        return oserr;
    }

    oserr = SHMGetSGTable(
            &usbScheduler->Settings.FrameListDMA,
            &usbScheduler->Settings.FrameListDMATable,
            -1
    );
    if (oserr != OS_EOK) {
        ERROR("__AllocateFrameMemory: cannot retrieve SG table for memory region: %u", oserr);
        return oserr;
    }

    usbScheduler->Settings.FrameList = (reg32_t*)SHMBuffer(&usbScheduler->Settings.FrameListDMA);
    usbScheduler->Settings.FrameListPhysical = usbScheduler->Settings.FrameListDMATable.Entries[0].Address;
    return OS_EOK;
}

oserr_t
UsbSchedulerInitialize(
    _In_  UsbSchedulerSettings_t* Settings,
    _Out_ UsbScheduler_t**        SchedulerOut)
{
    UsbScheduler_t* Scheduler;
    oserr_t      Status;
    int             i;

    TRACE("UsbSchedulerInitialize()");

    assert(Settings->FrameCount > 0);
    assert(Settings->SubframeCount > 0);
    assert(Settings->PoolCount > 0);

    Scheduler = (UsbScheduler_t*)malloc(sizeof(UsbScheduler_t));
    if (!Scheduler) {
        return OS_EOOM;
    }

    memset((void*)Scheduler, 0, sizeof(UsbScheduler_t));
    spinlock_init(&Scheduler->Lock);
    memcpy((void*)&Scheduler->Settings, Settings, sizeof(UsbSchedulerSettings_t));

    // Start out by allocating the frame list if requested by the user
    if (Scheduler->Settings.Flags & USB_SCHEDULER_FRAMELIST) {
        Status = __AllocateFrameMemory(Scheduler);
        if (Status != OS_EOK) {
            UsbSchedulerDestroy(Scheduler);
            return Status;
        }
    }

    // Validate the physical location of the framelist, on most usb controllers
    // it is not supported that its located above 32 bit memory space. Unless
    // we have been told to ignore this then assert on it.
    if (!(Scheduler->Settings.Flags & USB_SCHEDULER_FL64)) {
        if ((Scheduler->Settings.FrameListPhysical + 
                (Scheduler->Settings.FrameCount * 4)) > 0xFFFFFFFF) {
            ERROR("Failed to allocate memory below 4gb memory for usb resources");
            UsbSchedulerDestroy(Scheduler);
            return OS_EUNKNOWN;
        }
    }
    
    // Initialize all the requested pools. Memory resources must be allocated
    // for them.
    for (i = 0; i < Scheduler->Settings.PoolCount; i++) {
        Status = __AllocatePoolMemory(&Scheduler->Settings.Pools[i]);
        if (Status != OS_EOK) {
            UsbSchedulerDestroy(Scheduler);
            return Status;
        }
    }

    // Allocate the last resources
    TRACE(" > Allocating management resources");
    Scheduler->VirtualFrameList = (uintptr_t*)malloc(Settings->FrameCount * sizeof(uintptr_t));
    Scheduler->Bandwidth        = (size_t*)malloc((Settings->FrameCount * Settings->SubframeCount * sizeof(size_t)));
    if (!Scheduler->VirtualFrameList || !Scheduler->Bandwidth) {
        UsbSchedulerDestroy(Scheduler);
        return OS_EOOM;
    }
    
    *SchedulerOut = Scheduler;
    return UsbSchedulerResetInternalData(Scheduler, 1, 1);
}

static void
FreePoolMemory(
    _In_ UsbSchedulerPool_t* Pool)
{
    OSHandleDestroy(&Pool->ElementPoolDMA);
    free(Pool->ElementPoolDMATable.Entries);
}

static void
FreeFrameListMemory(
    _In_ UsbScheduler_t* Scheduler)
{
    OSHandleDestroy(&Scheduler->Settings.FrameListDMA);
    free(Scheduler->Settings.FrameListDMATable.Entries);
}

void
UsbSchedulerDestroy(
    _In_ UsbScheduler_t* Scheduler)
{
    int i;
    
    if (Scheduler->Settings.Flags & USB_SCHEDULER_FRAMELIST) {
        FreeFrameListMemory(Scheduler);
    }
    
    for (i = 0; i < Scheduler->Settings.PoolCount; i++) {
        FreePoolMemory(&Scheduler->Settings.Pools[i]);
    }

    if (Scheduler->VirtualFrameList != NULL) {
        free(Scheduler->VirtualFrameList);
    }
    if (Scheduler->Bandwidth != NULL) {
        free(Scheduler->Bandwidth);
    }
    free(Scheduler);
}

long
UsbSchedulerCalculateBandwidth(
    _In_ enum USBSpeed speed,
    _In_ uint8_t       transactionType,
    _In_ uint8_t       transferType,
    _In_ size_t        length)
{
    long result = 0;

    // The bandwidth calculations are based entirely
    // on the speed of the transfer
    switch (speed) {
        case USBSPEED_LOW:
            if (transactionType == USB_TRANSACTION_IN) {
                result = (67667L * (31L + 10L * BitTime(length))) / 1000L;
                return 64060L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + result;
            } else {
                result = (66700L * (31L + 10L * BitTime(length))) / 1000L;
                return 64107L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + result;
            }
        case USBSPEED_FULL:
            if (transferType == USBTRANSFER_TYPE_ISOC) {
                result = (8354L * (31L + 10L * BitTime(length))) / 1000L;
                return ((transactionType == USB_TRANSACTION_IN) ? 7268L : 6265L) + BW_HOST_DELAY + result;
            } else {
                result = (8354L * (31L + 10L * BitTime(length))) / 1000L;
                return 9107L + BW_HOST_DELAY + result;
            }
        case USBSPEED_SUPER_PLUS:
        case USBSPEED_SUPER:
        case USBSPEED_HIGH:
            if (transferType == USBTRANSFER_TYPE_ISOC) {
                result = HS_NSECS_ISO(length);
            } else {
                result = HS_NSECS(length);
            }
        
        default:
            break;
    }
    return result;
}

oserr_t
UsbSchedulerGetPoolElement(
    _In_  UsbScheduler_t* Scheduler,
    _In_  int             Pool,
    _In_  int             Index,
    _Out_ uint8_t**       ElementOut,
    _Out_ uintptr_t*      ElementPhysicalOut)
{
    assert(Pool < Scheduler->Settings.PoolCount);
    if (ElementOut != NULL) {
        *ElementOut = USB_ELEMENT_INDEX((&Scheduler->Settings.Pools[Pool]), Index);
    }
    if (ElementPhysicalOut != NULL) {
        *ElementPhysicalOut = USB_ELEMENT_PHYSICAL((&Scheduler->Settings.Pools[Pool]), Index);
    }
    return OS_EOK;
}

oserr_t
UsbSchedulerGetPoolFromElement(
    _In_  UsbScheduler_t*      scheduler,
    _In_  const uint8_t*       element,
    _Out_ UsbSchedulerPool_t** poolOut)
{
    for (int i = 0; i < scheduler->Settings.PoolCount; i++) {
        uintptr_t PoolStart = (uintptr_t)scheduler->Settings.Pools[i].ElementPool;
        uintptr_t PoolEnd   = PoolStart + (scheduler->Settings.Pools[i].ElementAlignedSize * scheduler->Settings.Pools[i].ElementCount);
        if (ISINRANGE((uintptr_t)element, PoolStart, PoolEnd)) {
            *poolOut = &scheduler->Settings.Pools[i];
            return OS_EOK;
        }
    }
    return OS_EUNKNOWN;
}

oserr_t
UsbSchedulerAllocateElement(
    _In_  UsbScheduler_t* Scheduler,
    _In_  int             Pool,
    _Out_ uint8_t**       ElementOut)
{
    UsbSchedulerObject_t* sObject = NULL;
    UsbSchedulerPool_t*   sPool   = NULL;
    size_t                i;

    // Get pool
    assert(ElementOut != NULL);
    assert(Pool < Scheduler->Settings.PoolCount);
    sPool = &Scheduler->Settings.Pools[Pool];
    
    // Reset output value
    *ElementOut = NULL;

    // Now, we usually allocated new descriptors for interrupts
    // and isoc, but it doesn't make sense for us as we keep one
    // large pool of TDs, just allocate from that in any case
    spinlock_acquire(&Scheduler->Lock);
    for (i = sPool->ElementCountReserved; i < sPool->ElementCount; i++) {
        uint8_t *Element = USB_ELEMENT_INDEX(sPool, i);
        sObject          = USB_ELEMENT_OBJECT(sPool, Element);
        if (sObject->Flags & USB_ELEMENT_ALLOCATED) {
            continue;
        }

        // Found one, reset
        memset((void*)Element, 0, sPool->ElementAlignedSize);
        sObject->Index       = USB_ELEMENT_CREATE_INDEX(Pool, i);
        sObject->BreathIndex = USB_ELEMENT_NO_INDEX;
        sObject->DepthIndex  = USB_ELEMENT_NO_INDEX;
        sObject->Flags       = USB_ELEMENT_ALLOCATED;
        *ElementOut          = Element;
        break;
    }
    spinlock_release(&Scheduler->Lock);
    return (i == sPool->ElementCount) ? OS_EUNKNOWN : OS_EOK;
}

oserr_t
UsbSchedulerAllocateBandwidthSubframe(
    _In_  UsbScheduler_t*       Scheduler,
    _In_  UsbSchedulerObject_t* sObject,
    _In_  size_t                Frame,
    _In_  int                   NumberOfTransactions,
    _In_  int                   Validate,
    _Out_ reg32_t*              FrameMask)
{
    oserr_t Result = OS_EOK;
    size_t     j;

    // Either we create a mask
    // or we allocate a mask
    if (*FrameMask == 0) {
        int Counter = NumberOfTransactions;
        // Find space in 'sub' schedule
        for (j = 1; j < Scheduler->Settings.SubframeCount && Counter; j++) {
            if ((Scheduler->Bandwidth[Frame + j] + sObject->Bandwidth) <= Scheduler->Settings.MaxBandwidthPerFrame) {
                if (Validate == 0) {
                    Scheduler->Bandwidth[Frame + j] += sObject->Bandwidth;
                }
                *FrameMask |= (1 << j);
                Counter--;
            }
        }

        if (Counter != 0) {
            Result = OS_EUNKNOWN;
        }
    }
    else if (Validate != 0) {
        // Allocate bandwidth in 'sub' schedule
        for (j = 1; j < Scheduler->Settings.SubframeCount; j++) {
            if (*FrameMask & (1 << j)) {
                Scheduler->Bandwidth[Frame + j] += sObject->Bandwidth;
            }
        }
    }
    return Result;
}

oserr_t
UsbSchedulerTryAllocateBandwidth(
    _In_ UsbScheduler_t*       Scheduler,
    _In_ UsbSchedulerObject_t* sObject,
    _In_ int                   NumberOfTransactions)
{
    oserr_t Result     = OS_EOK;
    reg32_t    StartFrame = (reg32_t)-1;
    reg32_t    FrameMask  = 0;
    int        Validated  = 0;
    size_t     i;

    // Iterate the requested period and make sure there is actually room
    spinlock_acquire(&Scheduler->Lock);
    while (NumberOfTransactions) { // Run untill cancel
        for (i = 0; i < Scheduler->Settings.FrameCount; ) {
            if ((Scheduler->Bandwidth[i] + sObject->Bandwidth) > Scheduler->Settings.MaxBandwidthPerFrame) {
                // Try to allocate on uneven frames if period is not 1
                if (sObject->FrameInterval == 1 || StartFrame != (reg32_t)-1) {
                    Result = OS_EUNKNOWN;
                    break;
                }
                else {
                    i += Scheduler->Settings.SubframeCount;
                    continue;
                }
            }

            // Lock this frame-period
            if (StartFrame == (reg32_t)-1) {
                StartFrame = i;
            }

            // For EHCI we must support bandwidth sizes instead of having a scheduler with multiple
            // levels, we have it flattened
            if (NumberOfTransactions > 1 && Scheduler->Settings.SubframeCount > 1) {
                if (Validated == 0) {
                    Result = UsbSchedulerAllocateBandwidthSubframe(Scheduler, sObject, i, NumberOfTransactions, 1, &FrameMask);
                }
                else {
                    Result = UsbSchedulerAllocateBandwidthSubframe(Scheduler, sObject, i, NumberOfTransactions, 0, &FrameMask);
                }
            }

            // Increase bandwidth and update index by sObject->FrameInterval
            if (Validated != 0) {
                Scheduler->Bandwidth[i] += sObject->Bandwidth;
            }
            i += (sObject->FrameInterval * Scheduler->Settings.SubframeCount);
        }

        // Perform another iteration?
        if (Validated == 0 && Result == OS_EOK) {
            Validated = 1;
            continue;
        }
        break;
    }
    spinlock_release(&Scheduler->Lock);
    if (Result != OS_EOK) {
        return Result;
    }

    // Update members
    sObject->StartFrame = (uint16_t)(StartFrame & 0xFFFF);
    sObject->FrameMask  = (uint16_t)(FrameMask & 0xFFFF);
    return Result;
}

oserr_t
UsbSchedulerAllocateBandwidth(
    _In_ UsbScheduler_t* scheduler,
    _In_ uint8_t         interval,
    _In_ uint16_t        mps,
    _In_ uint8_t         transactionType,
    _In_ size_t          bytesToTransfer,
    _In_ uint8_t         transferType,
    _In_ uint8_t         speed,
    _In_ uint8_t*        element)
{
    int                   numberOfTransactions;
    int                   exponent;
    oserr_t            result;
    UsbSchedulerObject_t* schedulerObject;
    UsbSchedulerPool_t*   schedulerPool = NULL;
    TRACE("UsbSchedulerAllocateBandwidth(scheduler=0x%" PRIxIN ", interval=%u, mps=%u, transactionType=%u,",
          scheduler, interval, mps, transactionType);
    TRACE("         bytesToTransfer=%" PRIuIN ", transferType=%u, speed=%u, element=0x%" PRIxIN ")",
          bytesToTransfer, transferType, speed, element);

    // Validate element and lookup pool
    result = UsbSchedulerGetPoolFromElement(scheduler, element, &schedulerPool);
    if (result != OS_EOK) {
        return result;
    }

    schedulerObject = USB_ELEMENT_OBJECT(schedulerPool, element);

    // Calculate the required number of transactions based on the MPS
    numberOfTransactions = DIVUP(bytesToTransfer, mps);

    // Calculate the number of microseconds the transfer will take
    schedulerObject->Bandwidth = (uint16_t)NS_TO_US(UsbSchedulerCalculateBandwidth(
        speed, transactionType, transferType, bytesToTransfer));
    
    // If highspeed calculate period as 2^(Interval-1)
    if (speed == USBSPEED_LOW || speed == USBSPEED_FULL) {
        // low and full speed deal in 1ms frames
        schedulerObject->FrameInterval = interval;
    } else {
        // high and up only deal in subms frames
        schedulerObject->FrameInterval = (1 << interval);
    }
    
    // Sanitize some bounds for period
    // to be considered if it should fail instead
    if (schedulerObject->FrameInterval == 0)
        schedulerObject->FrameInterval = 1;
    else if (schedulerObject->FrameInterval > scheduler->Settings.FrameCount)
        schedulerObject->FrameInterval = scheduler->Settings.FrameCount;

    // Try to fit it in, we try lesser intervals if possible too
    for (exponent = 7; exponent >= 0; --exponent) {
        if ((1 << exponent) <= (int)schedulerObject->FrameInterval) {
            break;
        }
    }

    // Sanitize that the exponent is valid
    if (exponent < 0) {
        ERROR("Invalid usb-endpoint interval %u", interval);
        exponent = 0;
    }

    // If we don't bandwidth for transfers with 1 interval, then try 2, 4, 8, 16, 32 etc
    if (exponent > 0) {
        do {
            schedulerObject->FrameInterval = 1 << exponent;
            result = UsbSchedulerTryAllocateBandwidth(scheduler, schedulerObject, numberOfTransactions);
        } while (result != OS_EOK && --exponent >= 0);
    }
    else {
        schedulerObject->FrameInterval = 1 << exponent;
        result = UsbSchedulerTryAllocateBandwidth(scheduler, schedulerObject, numberOfTransactions);
    }

    if (result == OS_EOK) {
        schedulerObject->Flags |= USB_ELEMENT_BANDWIDTH;
    }
    return result;
}

oserr_t
UsbSchedulerFreeBandwidth(
    _In_  UsbScheduler_t* usbScheduler,
    _In_  uint8_t*        element)
{
    UsbSchedulerObject_t* object = NULL;
    UsbSchedulerPool_t*   pool   = NULL;
    oserr_t               oserr;
    size_t                i, j;

    // Validate element and lookup pool
    oserr = UsbSchedulerGetPoolFromElement(usbScheduler, element, &pool);
    if (oserr != OS_EOK) {
        WARNING("UsbSchedulerFreeBandwidth: cannot get object from pool");
        return oserr;
    }
    object = USB_ELEMENT_OBJECT(pool, element);

    // Iterate the requested period and clean up
    spinlock_acquire(&usbScheduler->Lock);
    for (i = object->StartFrame; i < usbScheduler->Settings.FrameCount; i += (object->FrameInterval * usbScheduler->Settings.SubframeCount)) {
        // Reduce allocated bandwidth
        usbScheduler->Bandwidth[i] -= MIN(object->Bandwidth, usbScheduler->Bandwidth[i]);

        // For EHCI we must support bandwidth sizes
        // instead of having a scheduler with multiple
        // levels, we have it flattened
        if (object->FrameMask != 0 && usbScheduler->Settings.SubframeCount > 1) {
            // Free bandwidth in 'sub' schedule
            for (j = 1; j < usbScheduler->Settings.SubframeCount; j++) {
                if (object->FrameMask & (1 << j)) {
                    usbScheduler->Bandwidth[i + j] -= object->Bandwidth;
                }
            }
        }
    }
    spinlock_release(&usbScheduler->Lock);
    return oserr;
}

void
UsbSchedulerFreeElement(
    _In_ UsbScheduler_t* usbScheduler,
    _In_ uint8_t*        element)
{
    UsbSchedulerObject_t* object = NULL;
    UsbSchedulerPool_t*   pool   = NULL;
    oserr_t               oserr;
    
    // Validate element and lookup pool
    oserr = UsbSchedulerGetPoolFromElement(
            usbScheduler,
            element,
            &pool
    );
    if (oserr != OS_EOK) {
        WARNING("UsbSchedulerFreeElement: cannot get object from pool");
        return;
    }
    object = USB_ELEMENT_OBJECT(pool, element);

    // Should we free bandwidth?
    if (object->Flags & USB_ELEMENT_BANDWIDTH) {
        UsbSchedulerFreeBandwidth(usbScheduler, element);
    }
    memset((void*)element, 0, pool->ElementAlignedSize);
}

uintptr_t
UsbSchedulerGetDma(
    _In_ UsbSchedulerPool_t* schedulerPool,
    _In_ const uint8_t*      elementPointer)
{
    size_t offset = (uintptr_t)elementPointer - (uintptr_t)SHMBuffer(&schedulerPool->ElementPoolDMA);
    int    i;
    
    for (i = 0; i < schedulerPool->ElementPoolDMATable.Count; i++) {
        if (offset < schedulerPool->ElementPoolDMATable.Entries[i].Length) {
            return schedulerPool->ElementPoolDMATable.Entries[i].Address + offset;
        }
        offset -= schedulerPool->ElementPoolDMATable.Entries[i].Length;
    }
    return 0;
}
