/**
 * MollenOS
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
 *
 * USB Controller Scheduler
 * - Contains the implementation of a shared controller scheduker
 *   for all the usb drivers
 */
//#define __TRACE
#define __COMPILE_ASSERT

#include <os/mollenos.h>
#include <ddk/utils.h>
#include "scheduler.h"
#include <assert.h>
#include <stdlib.h>

// Data assertions
COMPILE_TIME_ASSERT(sizeof(UsbSchedulerObject_t) == 18);

OsStatus_t
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
    return OsSuccess;
}

static OsStatus_t
AllocateMemoryForPool(
    _In_ UsbSchedulerPool_t* Pool)
{
    size_t                 ElementBytes = Pool->ElementCount * Pool->ElementAlignedSize;
    struct dma_buffer_info DmaInfo;
    OsStatus_t             Status;
        
    // Setup required memory allocation flags
    // Require low memory as most usb controllers don't work with physical memory above 2GB
    // Require uncacheable memory as it's hardware accessible memory.
    // Require contigious memory to make allocation/address conversion easier
    DmaInfo.length   = ElementBytes;
    DmaInfo.capacity = ElementBytes;
    DmaInfo.flags    = DMA_UNCACHEABLE | DMA_CLEAN;

    TRACE("... allocating element pool memory (%u bytes)", ElementBytes);
    Status = dma_create(&DmaInfo, &Pool->ElementPoolDMA);
    if (Status != OsSuccess) {
        ERROR("... failed! %u", Status);
        return Status;
    }
    dma_get_sg_table(&Pool->ElementPoolDMA, &Pool->ElementPoolDMATable, -1);

    Pool->ElementPool = Pool->ElementPoolDMA.buffer;
    return OsSuccess;
}

static OsStatus_t
AllocateMemoryForFrameList(
    _In_ UsbScheduler_t* Scheduler)
{
    size_t                 FrameListBytes = Scheduler->Settings.FrameCount * 4;
    struct dma_buffer_info DmaInfo;
    OsStatus_t             Status;
    
    // Setup required memory allocation flags
    // Require low memory as most usb controllers don't work with physical memory above 2GB
    // Require uncacheable memory as it's hardware accessible memory.
    // Require contigious memory to make allocation/address conversion easier
    DmaInfo.length   = FrameListBytes;
    DmaInfo.capacity = FrameListBytes;
    DmaInfo.flags    = DMA_UNCACHEABLE | DMA_CLEAN;

    TRACE("... allocating frame list memory (%u bytes)", FrameListBytes);
    Status = dma_create(&DmaInfo, &Scheduler->Settings.FrameListDMA);
    if (Status != OsSuccess) {
        ERROR("... failed! %u", Status);
        return Status;
    }
    dma_get_sg_table(&Scheduler->Settings.FrameListDMA, 
        &Scheduler->Settings.FrameListDMATable, -1);
    
    Scheduler->Settings.FrameList = (reg32_t*)Scheduler->Settings.FrameListDMA.buffer;
    return OsSuccess;
}

OsStatus_t
UsbSchedulerInitialize(
    _In_  UsbSchedulerSettings_t* Settings,
    _Out_ UsbScheduler_t**        SchedulerOut)
{
    UsbScheduler_t* Scheduler;
    OsStatus_t      Status;
    int             i;

    TRACE("UsbSchedulerInitialize()");

    assert(Settings->FrameCount > 0);
    assert(Settings->SubframeCount > 0);
    assert(Settings->PoolCount > 0);

    Scheduler = (UsbScheduler_t*)malloc(sizeof(UsbScheduler_t));
    if (!Scheduler) {
        return OsOutOfMemory;
    }

    memset((void*)Scheduler, 0, sizeof(UsbScheduler_t));
    spinlock_init(&Scheduler->Lock, spinlock_plain);
    memcpy((void*)&Scheduler->Settings, Settings, sizeof(UsbSchedulerSettings_t));

    // Start out by allocating the frame list unless it has been provided for us
    if (Scheduler->Settings.Flags & USB_SCHEDULER_FRAMELIST) {
        Status = AllocateMemoryForFrameList(Scheduler);
        if (Status != OsSuccess) {
            UsbSchedulerDestroy(Scheduler);
            return Status;
        }
    }

    // Validate the physical location of the framelist, on most usb controllers
    // it is not supported that its located above 32 bit memory space. Unless
    // we have been told to ignore this then assert on it.
    if (!(Scheduler->Settings.Flags & USB_SCHEDULER_FL64)) {
        if ((Scheduler->Settings.FrameListDMATable.entries[0].address + 
                (Scheduler->Settings.FrameCount * 4)) > 0xFFFFFFFF) {
            ERROR("Failed to allocate memory below 4gb memory for usb resources");
            UsbSchedulerDestroy(Scheduler);
            return OsError;
        }
    }
    
    // Initialize all the requested pools. Memory resources must be allocated
    // for them.
    for (i = 0; i < Scheduler->Settings.PoolCount; i++) {
        Status = AllocateMemoryForPool(&Scheduler->Settings.Pools[i]);
        if (Status != OsSuccess) {
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
        return OsOutOfMemory;
    }
    
    *SchedulerOut = Scheduler;
    return UsbSchedulerResetInternalData(Scheduler, 1, 1);
}

static void
FreePoolMemory(
    _In_ UsbSchedulerPool_t* Pool)
{
    if (!Pool->ElementPoolDMA.buffer) {
        return;
    }
    
    (void)dma_attachment_unmap(&Pool->ElementPoolDMA);
    (void)dma_detach(&Pool->ElementPoolDMA);
    free(Pool->ElementPoolDMATable.entries);
}

static void
FreeFrameListMemory(
    _In_ UsbScheduler_t* Scheduler)
{
    if (!Scheduler->Settings.FrameListDMA.buffer) {
        return;
    }
    
    (void)dma_attachment_unmap(&Scheduler->Settings.FrameListDMA);
    (void)dma_detach(&Scheduler->Settings.FrameListDMA);
    free(Scheduler->Settings.FrameListDMATable.entries);
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
    _In_ UsbSpeed_t        Speed, 
    _In_ int               Direction,
    _In_ UsbTransferType_t Type,
    _In_ size_t            Length)
{
    long Result = 0;

    // The bandwidth calculations are based entirely
    // on the speed of the transfer
    switch (Speed) {
    case LowSpeed:
        if (Direction == USB_ENDPOINT_IN) {
            Result = (67667L * (31L + 10L * BitTime(Length))) / 1000L;
            return 64060L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + Result;
        }
        else {
            Result = (66700L * (31L + 10L * BitTime(Length))) / 1000L;
            return 64107L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + Result;
        }
    case FullSpeed:
        if (Type == IsochronousTransfer) {
            Result = (8354L * (31L + 10L * BitTime(Length))) / 1000L;
            return ((Direction == USB_ENDPOINT_IN) ? 7268L : 6265L) + BW_HOST_DELAY + Result;
        }
        else {
            Result = (8354L * (31L + 10L * BitTime(Length))) / 1000L;
            return 9107L + BW_HOST_DELAY + Result;
        }
    case SuperSpeed:
    case HighSpeed:
        if (Type == IsochronousTransfer)
            Result = HS_NSECS_ISO(Length);
        else
            Result = HS_NSECS(Length);
    }
    return Result;
}

OsStatus_t
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
    return OsSuccess;
}

OsStatus_t
UsbSchedulerGetPoolFromElement(
    _In_  UsbScheduler_t*      Scheduler,
    _In_  uint8_t*             Element,
    _Out_ UsbSchedulerPool_t** Pool)
{
    for (int i = 0; i < Scheduler->Settings.PoolCount; i++) {
        uintptr_t PoolStart = (uintptr_t)Scheduler->Settings.Pools[i].ElementPool;
        uintptr_t PoolEnd   = PoolStart + (Scheduler->Settings.Pools[i].ElementAlignedSize * Scheduler->Settings.Pools[i].ElementCount) - 1;
        if (ISINRANGE((uintptr_t)Element, PoolStart, PoolEnd)) {
            *Pool = &Scheduler->Settings.Pools[i];
            return OsSuccess;
        }
    }
    return OsError;
}

OsStatus_t
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
    return (i == sPool->ElementCount) ? OsError : OsSuccess;
}

OsStatus_t
UsbSchedulerAllocateBandwidthSubframe(
    _In_  UsbScheduler_t*       Scheduler,
    _In_  UsbSchedulerObject_t* sObject,
    _In_  size_t                Frame,
    _In_  int                   NumberOfTransactions,
    _In_  int                   Validate,
    _Out_ reg32_t*              FrameMask)
{
    OsStatus_t Result = OsSuccess;
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
            Result = OsError;
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

OsStatus_t
UsbSchedulerTryAllocateBandwidth(
    _In_ UsbScheduler_t*       Scheduler,
    _In_ UsbSchedulerObject_t* sObject,
    _In_ int                   NumberOfTransactions)
{
    OsStatus_t Result     = OsSuccess;
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
                    Result = OsError;
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
        if (Validated == 0 && Result == OsSuccess) {
            Validated = 1;
            continue;
        }
        break;
    }
    spinlock_release(&Scheduler->Lock);
    if (Result != OsSuccess) {
        return Result;
    }

    // Update members
    sObject->StartFrame = (uint16_t)(StartFrame & 0xFFFF);
    sObject->FrameMask  = (uint16_t)(FrameMask & 0xFFFF);
    return Result;
}

OsStatus_t
UsbSchedulerAllocateBandwidth(
    _In_ UsbScheduler_t*            Scheduler,
    _In_ UsbHcEndpointDescriptor_t* Endpoint,
    _In_ size_t                     BytesToTransfer,
    _In_ UsbTransferType_t          Type,
    _In_ UsbSpeed_t                 Speed,
    _In_ uint8_t*                   Element)
{
    UsbSchedulerObject_t* sObject              = NULL;
    UsbSchedulerPool_t*   sPool                = NULL;
    OsStatus_t            Result               = OsSuccess;
    int                   NumberOfTransactions = 0;
    int                   Exponent             = 0;

    // Validate element and lookup pool
    Result = UsbSchedulerGetPoolFromElement(Scheduler, Element, &sPool);
    assert(Result == OsSuccess);
    sObject = USB_ELEMENT_OBJECT(sPool, Element);

    // Calculate the required number of transactions based on the MPS
    NumberOfTransactions = DIVUP(BytesToTransfer, Endpoint->MaxPacketSize);

    // Calculate the number of microseconds the transfer will take
    sObject->Bandwidth = (uint16_t)NS_TO_US(UsbSchedulerCalculateBandwidth(Speed, Endpoint->Direction, Type, BytesToTransfer));
    
    // If highspeed calculate period as 2^(Interval-1)
    if (Speed == HighSpeed) {
        sObject->FrameInterval = (1 << LOWORD(Endpoint->Interval));
    }
    else {
        sObject->FrameInterval = LOWORD(Endpoint->Interval);
    }
    
    // Sanitize some bounds for period
    // to be considered if it should fail instead
    if (sObject->FrameInterval == 0)                             sObject->FrameInterval = 1;
    if (sObject->FrameInterval > Scheduler->Settings.FrameCount) sObject->FrameInterval = Scheduler->Settings.FrameCount;

    // Try to fit it in, we try lesser intervals if possible too
    for (Exponent = 7; Exponent >= 0; --Exponent) {
        if ((1 << Exponent) <= (int)sObject->FrameInterval) {
            break;
        }
    }

    // Sanitize that the exponent is valid
    if (Exponent < 0) {
        ERROR("Invalid usb-endpoint interval %u", Endpoint->Interval);
        Exponent = 0;
    }

    // If we don't bandwidth for transfers with 1 interval, then try 2, 4, 8, 16, 32 etc
    if (Exponent > 0) {
        while (Result != OsSuccess && --Exponent >= 0) {
            sObject->FrameInterval = 1 << Exponent;
            Result                 = UsbSchedulerTryAllocateBandwidth(Scheduler, sObject, NumberOfTransactions);
        }
    }
    else {
        sObject->FrameInterval = 1 << Exponent;
        Result                 = UsbSchedulerTryAllocateBandwidth(Scheduler, sObject, NumberOfTransactions);
    }

    if (Result == OsSuccess) {
        sObject->Flags |= USB_ELEMENT_BANDWIDTH;
    }
    return Result;
}

OsStatus_t
UsbSchedulerFreeBandwidth(
    _In_  UsbScheduler_t* Scheduler,
    _In_  uint8_t*        Element)
{
    UsbSchedulerObject_t* sObject = NULL;
    UsbSchedulerPool_t*   sPool   = NULL;
    OsStatus_t            Result  = OsSuccess;
    size_t                i, j;

    // Validate element and lookup pool
    Result = UsbSchedulerGetPoolFromElement(Scheduler, Element, &sPool);
    assert(Result == OsSuccess);
    sObject = USB_ELEMENT_OBJECT(sPool, Element);

    // Iterate the requested period and clean up
    spinlock_acquire(&Scheduler->Lock);
    for (i = sObject->StartFrame; i < Scheduler->Settings.FrameCount; i += (sObject->FrameInterval * Scheduler->Settings.SubframeCount)) {
        // Reduce allocated bandwidth
        Scheduler->Bandwidth[i] -= MIN(sObject->Bandwidth, Scheduler->Bandwidth[i]);

        // For EHCI we must support bandwidth sizes
        // instead of having a scheduler with multiple
        // levels, we have it flattened
        if (sObject->FrameMask != 0 && Scheduler->Settings.SubframeCount > 1) {
            // Free bandwidth in 'sub' schedule
            for (j = 1; j < Scheduler->Settings.SubframeCount; j++) {
                if (sObject->FrameMask & (1 << j)) {
                    Scheduler->Bandwidth[i + j] -= sObject->Bandwidth;
                }
            }
        }
    }
    spinlock_release(&Scheduler->Lock);
    return Result;
}

void
UsbSchedulerFreeElement(
    _In_ UsbScheduler_t* Scheduler,
    _In_ uint8_t*        Element)
{
    UsbSchedulerObject_t* sObject = NULL;
    UsbSchedulerPool_t*   sPool   = NULL;
    OsStatus_t            Result  = OsSuccess;
    
    // Validate element and lookup pool
    Result = UsbSchedulerGetPoolFromElement(Scheduler, Element, &sPool);
    assert(Result == OsSuccess);
    sObject = USB_ELEMENT_OBJECT(sPool, Element);

    // Should we free bandwidth?
    if (sObject->Flags & USB_ELEMENT_BANDWIDTH) {
        UsbSchedulerFreeBandwidth(Scheduler, Element);
    }
    memset((void*)Element, 0, sPool->ElementAlignedSize);
}

uintptr_t
UsbSchedulerGetDma(
    _In_ UsbSchedulerPool_t* Pool,
    _In_ uint8_t*            ElementPointer)
{
    size_t Offset = (uintptr_t)ElementPointer - (uintptr_t)Pool->ElementPoolDMA.buffer;
    int    i;
    
    for (i = 0; i < Pool->ElementPoolDMATable.count; i++) {
        if (Offset < Pool->ElementPoolDMATable.entries[i].length) {
            return Pool->ElementPoolDMATable.entries[i].address + Offset;
        }
        Offset -= Pool->ElementPoolDMATable.entries[i].length;
    }
    return 0;
}
