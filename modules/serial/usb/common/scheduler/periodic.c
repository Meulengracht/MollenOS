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
#define __need_minmax

#include <assert.h>
#include <ddk/barrier.h>
#include <ddk/utils.h>
#include <os/mollenos.h>
#include <string.h>
#include <usb/usb.h>
#include "scheduler.h"

static UsbSchedulerPool_t*
__GetPoolByIndex(
    _In_ UsbSchedulerSettings_t* settings,
    _In_ uint16_t                index)
{
    return &settings->Pools[(index >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK];
}

static long
__CalculateBandwidth(
    _In_ enum USBSpeed             speed,
    _In_ enum USBTransferDirection direction,
    _In_ enum USBTransferType      type,
    _In_ size_t                    length)
{
    long result = 0;

    switch (speed) {
        case USBSPEED_LOW:
            if (direction == USBTRANSFER_DIRECTION_IN) {
                result = (67667L * (31L + 10L * BitTime(length))) / 1000L;
                return 64060L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + result;
            } else {
                result = (66700L * (31L + 10L * BitTime(length))) / 1000L;
                return 64107L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + result;
            }
        case USBSPEED_FULL:
            if (type == USBTRANSFER_TYPE_ISOC) {
                result = (8354L * (31L + 10L * BitTime(length))) / 1000L;
                return ((direction == USBTRANSFER_DIRECTION_IN) ? 7268L : 6265L) + BW_HOST_DELAY + result;
            } else {
                result = (8354L * (31L + 10L * BitTime(length))) / 1000L;
                return 9107L + BW_HOST_DELAY + result;
            }
        case USBSPEED_SUPER_PLUS:
        case USBSPEED_SUPER:
        case USBSPEED_HIGH:
            if (type == USBTRANSFER_TYPE_ISOC) {
                result = HS_NSECS_ISO(length);
            } else {
                result = HS_NSECS(length);
            }
        default:
            break;
    }
    return result;
}

size_t
UsbSchedulerCalculateBandwidth(
    _In_ uint8_t speed,
    _In_ uint8_t direction,
    _In_ uint8_t transferType,
    _In_ size_t  bytesToTransfer)
{
    // Keep the bandwidth calculation in the common scheduler so all HCI
    // implementations use the same USB timing model. EHCI only changes how
    // the resulting cost is distributed across start and complete slots.
    return (size_t)NS_TO_US(
        __CalculateBandwidth(
            speed,
            direction,
            transferType,
            bytesToTransfer
        )
    );
}

oserr_t
UsbSchedulerAllocateSplitBandwidth(
    _In_  UsbScheduler_t* scheduler,
    _In_  uint8_t         interval,
    _In_  size_t          startBandwidth,
    _In_  size_t          completeBandwidth,
    _In_  uint8_t         startSplitCount,
    _In_  bool            needsCompleteSplit,
    _In_  uint8_t*        element,
    _Out_ uint8_t*        startMaskOut,
    _Out_ uint8_t*        completeMaskOut,
    _Out_ uint8_t*        completionFrameOffsetOut)
{
    // Split transactions consume two distinct scheduling phases: a start
    // split in one microframe and complete splits in later microframes. The
    // reservation is made under one lock and committed only after every
    // occurrence of the periodic interval fits.
    UsbPeriodicScheduler_t* periodic = UsbSchedulerGetPeriodicScheduler(scheduler);
    UsbSchedulerObject_t*   object;
    UsbSchedulerPool_t*     pool;
    uint8_t                 startMask = 0;
    uint8_t                 completeMask = 0;
    uint8_t                 completionOffset = 0;
    size_t                  frame;
    size_t                  selectedFrame = 0;
    size_t                  frameCount = periodic->Settings->FrameCount;
    size_t                  subframeCount = periodic->Settings->SubframeCount;
    size_t                  frameInterval = MAX(1, interval);
    bool                    found = false;

    // The common scheduler currently models eight microframes per frame. Do
    // not silently truncate masks if a controller is configured differently.
    if ((periodic->Settings->Flags & USB_SCHEDULER_PERIODIC) == 0 ||
        subframeCount < 2 || subframeCount > 8 || startMaskOut == NULL ||
        completeMaskOut == NULL || completionFrameOffsetOut == NULL ||
        startSplitCount == 0 || startSplitCount > subframeCount) {
        return OS_ENOTSUPPORTED;
    }
    if (UsbTransferArenaGetPoolFromElement(
            UsbSchedulerGetTransferArena(scheduler), element, &pool) != OS_EOK) {
        return OS_EINVALPARAMS;
    }
    object = USB_ELEMENT_OBJECT(pool, element);

    // Search for a start microframe and a legal completion window without
    // modifying accounting. This makes failure atomic and avoids rollback
    // races with another periodic transfer being queued concurrently.
    spinlock_acquire(&scheduler->Lock);
    for (frame = 0; frame < frameCount && !found; frame++) {
        // OUT splits reserve startSplitCount consecutive start microframes
        // and never a complete-split, so the run must fit inside this frame.
        for (uint8_t start = 0; (start + startSplitCount) <= subframeCount && !found; start++) {
            uint8_t startRun = 0;
            uint8_t complete = 0;
            uint8_t candidateCompletionOffset = 0;

            for (uint8_t bit = 0; bit < startSplitCount; bit++) {
                startRun |= (uint8_t)(1u << (start + bit));
            }

            if (needsCompleteSplit) {
                for (uint8_t candidate = (uint8_t)(start + startSplitCount + 1); candidate < subframeCount; candidate++) {
                    complete |= (uint8_t)(1u << candidate);
                }
                // The siTD links only in its start frame and its C-mask is read
                // against that same frame, so a completion window that spills
                // into the next frame would require an FSTN, which is not
                // supported here. Reject the start position instead of handing
                // back a schedule the hardware cannot honor.
                if (complete == 0) {
                    continue;
                }
            }

            bool available = true;
            for (size_t occurrence = frame; occurrence < frameCount; occurrence += frameInterval) {
                size_t startBase = occurrence * subframeCount;
                size_t completeSlot = occurrence * subframeCount + start +
                    (candidateCompletionOffset * subframeCount);
                size_t completeFrame = (completeSlot / subframeCount) % frameCount;
                size_t completeBase = completeFrame * subframeCount;
                for (uint8_t bit = 0; bit < startSplitCount; bit++) {
                    if (periodic->Bandwidth[startBase + start + bit] + startBandwidth > periodic->Settings->MaxBandwidthPerFrame) {
                        available = false;
                        break;
                    }
                }
                if (!available) {
                    break;
                }
                if (needsCompleteSplit) {
                    for (uint8_t candidate = 0; candidate < subframeCount; candidate++) {
                        if ((complete & (1u << candidate)) &&
                            periodic->Bandwidth[completeBase + candidate] + completeBandwidth > periodic->Settings->MaxBandwidthPerFrame) {
                            available = false;
                            break;
                        }
                    }
                }
                if (!available) {
                    break;
                }
            }
            if (!available) {
                continue;
            }

            // The candidate fits in every frame occurrence, so commit both
            // phases while still holding the scheduler lock.
            for (size_t occurrence = frame; occurrence < frameCount; occurrence += frameInterval) {
                size_t startBase = occurrence * subframeCount;
                size_t completeSlot = occurrence * subframeCount + start +
                    (candidateCompletionOffset * subframeCount);
                size_t completeFrame = (completeSlot / subframeCount) % frameCount;
                size_t completeBase = completeFrame * subframeCount;
                for (uint8_t bit = 0; bit < startSplitCount; bit++) {
                    periodic->Bandwidth[startBase + start + bit] += startBandwidth;
                }
                if (needsCompleteSplit) {
                    for (uint8_t candidate = 0; candidate < subframeCount; candidate++) {
                        if (complete & (1u << candidate)) {
                            periodic->Bandwidth[completeBase + candidate] += completeBandwidth;
                        }
                    }
                }
            }
            startMask = startRun;
            completeMask = complete;
            completionOffset = candidateCompletionOffset;
            selectedFrame = frame;
            found = true;
        }
    }
    spinlock_release(&scheduler->Lock);

    if (!found) {
        return OS_EUNKNOWN;
    }
    // Store the start phase in the common object. EHCI retains the completion
    // mask and cost in its descriptor because other HCI implementations do
    // not need a second periodic phase.
    object->FrameInterval = (uint16_t)frameInterval;
    object->StartFrame = (uint16_t)selectedFrame;
    object->FrameMask = startMask;
    object->Bandwidth = (uint16_t)startBandwidth;
    object->Flags |= USB_ELEMENT_BANDWIDTH;
    *startMaskOut = startMask;
    *completeMaskOut = completeMask;
    *completionFrameOffsetOut = completionOffset;
    return OS_EOK;
}

void
UsbSchedulerFreeSplitBandwidth(
    _In_ UsbScheduler_t* scheduler,
    _In_ uint8_t*        element,
    _In_ uint8_t         startMask,
    _In_ uint8_t         completeMask,
    _In_ uint8_t         completionFrameOffset,
    _In_ size_t          startBandwidth,
    _In_ size_t          completeBandwidth)
{
    // Release exactly the start and complete reservations made by the matching
    // allocation call. This function is used before descriptor-pool cleanup,
    // so no hardware link should remain to the element being released.
    UsbPeriodicScheduler_t* periodic = UsbSchedulerGetPeriodicScheduler(scheduler);
    UsbSchedulerPool_t*     pool = NULL;
    UsbSchedulerObject_t*   object;

    if (UsbTransferArenaGetPoolFromElement(
            UsbSchedulerGetTransferArena(scheduler), (uint8_t*)element, &pool) != OS_EOK) {
        return;
    }
    object = USB_ELEMENT_OBJECT(pool, (uint8_t*)element);
    spinlock_acquire(&scheduler->Lock);
    for (size_t occurrence = object->StartFrame;
         occurrence < periodic->Settings->FrameCount;
         occurrence += object->FrameInterval) {
        size_t  startBase = occurrence * periodic->Settings->SubframeCount;
        uint8_t startSubframe = 0;
        size_t  completeSlot;
        size_t  completeFrame;
        size_t  completeBase;
        
        // The start mask may contain several consecutive bits for OUT splits.
        // Only the first is needed to locate the (possibly unused) complete
        // window, so find it defensively rather than indexing out of range.
        while (startSubframe < periodic->Settings->SubframeCount &&
               !(startMask & (1u << startSubframe))) {
            startSubframe++;
        }
        
        completeSlot = occurrence * periodic->Settings->SubframeCount + startSubframe +
            (completionFrameOffset * periodic->Settings->SubframeCount);
        completeFrame = (completeSlot / periodic->Settings->SubframeCount) % periodic->Settings->FrameCount;
        completeBase = completeFrame * periodic->Settings->SubframeCount;
        
        for (uint8_t subframe = 0; subframe < periodic->Settings->SubframeCount; subframe++) {
            if (startMask & (1u << subframe)) {
                periodic->Bandwidth[startBase + subframe] -= MIN(startBandwidth, periodic->Bandwidth[startBase + subframe]);
            }
            if (completeMask & (1u << subframe)) {
                periodic->Bandwidth[completeBase + subframe] -= MIN(completeBandwidth, periodic->Bandwidth[completeBase + subframe]);
            }
        }
    }
    spinlock_release(&scheduler->Lock);
}

oserr_t
UsbPeriodicSchedulerResetInternalData(
    _In_ UsbPeriodicScheduler_t* periodicScheduler,
    _In_ int                     resetFramelist)
{
    if ((periodicScheduler->Settings->Flags & USB_SCHEDULER_PERIODIC) == 0) {
        return OS_EOK;
    }

    if (!resetFramelist) {
        return OS_EOK;
    }

    reg32_t noLink = (periodicScheduler->Settings->Flags & USB_SCHEDULER_LINK_BIT_EOL) ? USB_ELEMENT_LINK_END : 0;
    memset(periodicScheduler->Settings->FrameList, 0, periodicScheduler->Settings->FrameCount * sizeof(reg32_t));
    memset(periodicScheduler->VirtualFrameList, 0, periodicScheduler->Settings->FrameCount * sizeof(uintptr_t));
    memset(periodicScheduler->Bandwidth, 0, periodicScheduler->Settings->FrameCount * periodicScheduler->Settings->SubframeCount * sizeof(size_t));

    for (size_t i = 0; i < periodicScheduler->Settings->FrameCount; i++) {
        periodicScheduler->Settings->FrameList[i] = noLink;
    }
    periodicScheduler->TotalBandwidth = 0;
    return OS_EOK;
}

static oserr_t
__AllocateBandwidthSubframe(
    _In_ UsbTransferArena_t*     arena,
    _In_ UsbPeriodicScheduler_t* periodicScheduler,
    _In_ UsbSchedulerObject_t*   object,
    _In_ size_t                  frame,
    _In_ int                     numberOfTransactions,
    _In_ int                     validate,
    _Out_ reg32_t*               frameMask)
{
    oserr_t result = OS_EOK;

    if (*frameMask == 0) {
        int counter = numberOfTransactions;
        for (size_t j = 1; j < periodicScheduler->Settings->SubframeCount && counter; j++) {
            if ((periodicScheduler->Bandwidth[frame + j] + object->Bandwidth) <= periodicScheduler->Settings->MaxBandwidthPerFrame) {
                if (validate == 0) {
                    periodicScheduler->Bandwidth[frame + j] += object->Bandwidth;
                }
                *frameMask |= (1 << j);
                counter--;
            }
        }

        if (counter != 0) {
            result = OS_EUNKNOWN;
        }
    } else if (validate != 0) {
        for (size_t j = 1; j < periodicScheduler->Settings->SubframeCount; j++) {
            if (*frameMask & (1 << j)) {
                periodicScheduler->Bandwidth[frame + j] += object->Bandwidth;
            }
        }
    }
    _CRT_UNUSED(arena);
    return result;
}

static oserr_t
__TryAllocateBandwidth(
    _In_ UsbTransferArena_t*     arena,
    _In_ UsbPeriodicScheduler_t* periodicScheduler,
    _In_ UsbSchedulerObject_t*   object,
    _In_ int                     numberOfTransactions)
{
    oserr_t result     = OS_EOK;
    reg32_t startFrame = (reg32_t)-1;
    reg32_t frameMask  = 0;
    int     validated  = 0;

    spinlock_acquire(arena->Lock);
    while (numberOfTransactions) {
        for (size_t i = 0; i < periodicScheduler->Settings->FrameCount; ) {
            if ((periodicScheduler->Bandwidth[i] + object->Bandwidth) > periodicScheduler->Settings->MaxBandwidthPerFrame) {
                if (object->FrameInterval == 1 || startFrame != (reg32_t)-1) {
                    result = OS_EUNKNOWN;
                    break;
                } else {
                    i += periodicScheduler->Settings->SubframeCount;
                    continue;
                }
            }

            if (startFrame == (reg32_t)-1) {
                startFrame = i;
            }

            if (numberOfTransactions > 1 && periodicScheduler->Settings->SubframeCount > 1) {
                if (validated == 0) {
                    result = __AllocateBandwidthSubframe(arena, periodicScheduler, object, i, numberOfTransactions, 1, &frameMask);
                } else {
                    result = __AllocateBandwidthSubframe(arena, periodicScheduler, object, i, numberOfTransactions, 0, &frameMask);
                }
            }

            if (validated != 0) {
                periodicScheduler->Bandwidth[i] += object->Bandwidth;
            }
            i += (object->FrameInterval * periodicScheduler->Settings->SubframeCount);
        }

        if (validated == 0 && result == OS_EOK) {
            validated = 1;
            continue;
        }
        break;
    }
    spinlock_release(arena->Lock);

    if (result != OS_EOK) {
        return result;
    }

    object->StartFrame = (uint16_t)(startFrame & 0xFFFF);
    object->FrameMask  = (uint16_t)(frameMask & 0xFFFF);
    return result;
}

static oserr_t
__FreeBandwidth(
    _In_ UsbTransferArena_t*     arena,
    _In_ UsbPeriodicScheduler_t* periodicScheduler,
    _In_ uint8_t*                element)
{
    UsbSchedulerObject_t* object = NULL;
    UsbSchedulerPool_t*   pool   = NULL;
    oserr_t               oserr;

    oserr = UsbTransferArenaGetPoolFromElement(arena, element, &pool);
    if (oserr != OS_EOK) {
        WARNING("UsbPeriodicSchedulerFreeBandwidth: cannot get object from pool");
        return oserr;
    }
    object = USB_ELEMENT_OBJECT(pool, element);

    spinlock_acquire(arena->Lock);
    for (size_t i = object->StartFrame; i < periodicScheduler->Settings->FrameCount; i += (object->FrameInterval * periodicScheduler->Settings->SubframeCount)) {
        periodicScheduler->Bandwidth[i] -= MIN(object->Bandwidth, periodicScheduler->Bandwidth[i]);

        if (object->FrameMask != 0 && periodicScheduler->Settings->SubframeCount > 1) {
            for (size_t j = 1; j < periodicScheduler->Settings->SubframeCount; j++) {
                if (object->FrameMask & (1 << j)) {
                    periodicScheduler->Bandwidth[i + j] -= object->Bandwidth;
                }
            }
        }
    }
    spinlock_release(arena->Lock);
    return OS_EOK;
}

oserr_t
UsbPeriodicSchedulerAllocateBandwidth(
    _In_ UsbTransferArena_t*     arena,
    _In_ UsbPeriodicScheduler_t* periodicScheduler,
    _In_ uint8_t                 interval,
    _In_ uint16_t                mps,
    _In_ uint8_t                 transactionType,
    _In_ size_t                  bytesToTransfer,
    _In_ uint8_t                 transferType,
    _In_ uint8_t                 speed,
    _In_ uint8_t*                element)
{
    int                   numberOfTransactions;
    int                   exponent;
    oserr_t               result;
    UsbSchedulerObject_t* object;
    UsbSchedulerPool_t*   pool = NULL;

    if ((periodicScheduler->Settings->Flags & USB_SCHEDULER_PERIODIC) == 0) {
        return OS_ENOTSUPPORTED;
    }

    result = UsbTransferArenaGetPoolFromElement(arena, element, &pool);
    if (result != OS_EOK) {
        return result;
    }
    object = USB_ELEMENT_OBJECT(pool, element);

    numberOfTransactions = DIVUP(bytesToTransfer, mps);
    object->Bandwidth = (uint16_t)NS_TO_US(__CalculateBandwidth(speed, transactionType, transferType, bytesToTransfer));

    if (speed == USBSPEED_LOW || speed == USBSPEED_FULL) {
        object->FrameInterval = interval;
    } else {
        object->FrameInterval = (1 << interval);
    }

    if (object->FrameInterval == 0) {
        object->FrameInterval = 1;
    } else if (object->FrameInterval > periodicScheduler->Settings->FrameCount) {
        object->FrameInterval = periodicScheduler->Settings->FrameCount;
    }

    for (exponent = 7; exponent >= 0; --exponent) {
        if ((1 << exponent) <= (int)object->FrameInterval) {
            break;
        }
    }

    if (exponent < 0) {
        ERROR("Invalid usb-endpoint interval %u", interval);
        exponent = 0;
    }

    if (exponent > 0) {
        do {
            object->FrameInterval = 1 << exponent;
            result = __TryAllocateBandwidth(arena, periodicScheduler, object, numberOfTransactions);
        } while (result != OS_EOK && --exponent >= 0);
    } else {
        object->FrameInterval = 1 << exponent;
        result = __TryAllocateBandwidth(arena, periodicScheduler, object, numberOfTransactions);
    }

    if (result == OS_EOK) {
        object->Flags |= USB_ELEMENT_BANDWIDTH;
    }
    return result;
}

oserr_t
UsbPeriodicSchedulerLinkElement(
    _In_ UsbTransferArena_t*     arena,
    _In_ UsbPeriodicScheduler_t* periodicScheduler,
    _In_ int                     ElementPool,
    _In_ uint8_t*                Element)
{
    oserr_t               oserr;
    UsbSchedulerObject_t* object;
    UsbSchedulerPool_t*   pool;
    uintptr_t             physicalAddress;

    assert(ElementPool < periodicScheduler->Settings->PoolCount);
    pool            = &periodicScheduler->Settings->Pools[ElementPool];
    object          = USB_ELEMENT_OBJECT(pool, Element);
    physicalAddress = UsbTransferArenaGetDma(pool, Element);

    for (size_t i = object->StartFrame; i < periodicScheduler->Settings->FrameCount; i += object->FrameInterval) {
        if (periodicScheduler->VirtualFrameList[i] == 0) {
            periodicScheduler->VirtualFrameList[i]   = (uintptr_t)Element;
            periodicScheduler->Settings->FrameList[i] = LODWORD(physicalAddress) | USB_ELEMENT_LINKFLAGS(object->Flags);
        } else {
            UsbSchedulerObject_t* existingObject;
            UsbSchedulerPool_t*   existingPool;
            uint8_t*              existingElement = (uint8_t*)periodicScheduler->VirtualFrameList[i];

            oserr = UsbTransferArenaGetPoolFromElement(arena, existingElement, &existingPool);
            if (oserr != OS_EOK) {
                WARNING("UsbPeriodicSchedulerLinkElement: cannot get object from pool");
                return oserr;
            }
            existingObject = USB_ELEMENT_OBJECT(existingPool, existingElement);

            while (existingObject->BreathIndex != USB_ELEMENT_NO_INDEX && existingObject != object) {
                if (object->FrameInterval > existingObject->FrameInterval) {
                    break;
                }

                existingPool    = __GetPoolByIndex(periodicScheduler->Settings, existingObject->BreathIndex);
                existingElement = USB_ELEMENT_INDEX(existingPool, existingObject->BreathIndex);
                existingObject  = USB_ELEMENT_OBJECT(existingPool, existingElement);
            }

            if (existingObject != object) {
                if (existingElement == (uint8_t*)periodicScheduler->VirtualFrameList[i] &&
                    object->FrameInterval > existingObject->FrameInterval) {
                    USB_ELEMENT_LINK(pool, Element, USB_CHAIN_BREATH) = periodicScheduler->Settings->FrameList[i];
                    object->BreathIndex                               = existingObject->Index;
                    dma_mb();
                    periodicScheduler->VirtualFrameList[i]    = (uintptr_t)Element;
                    periodicScheduler->Settings->FrameList[i] = LODWORD(physicalAddress) | USB_ELEMENT_LINKFLAGS(object->Flags);
                    dma_wmb();
                } else {
                    USB_ELEMENT_LINK(pool, Element, USB_CHAIN_BREATH)
                            = USB_ELEMENT_LINK(existingPool, existingElement, USB_CHAIN_BREATH);
                    object->BreathIndex = existingObject->BreathIndex;
                    dma_mb();

                    USB_ELEMENT_LINK(existingPool, existingElement, USB_CHAIN_BREATH)
                            = LODWORD(physicalAddress) | USB_ELEMENT_LINKFLAGS(object->Flags);
                    existingObject->BreathIndex = object->Index;
                    dma_wmb();
                }
            }
        }
    }
    return OS_EOK;
}

void
UsbPeriodicSchedulerUnlinkElement(
    _In_ UsbTransferArena_t*     arena,
    _In_ UsbPeriodicScheduler_t* periodicScheduler,
    _In_ int                     ElementPool,
    _In_ uint8_t*                Element)
{
    oserr_t               oserr;
    UsbSchedulerObject_t* object;
    UsbSchedulerPool_t*   pool;
    reg32_t               noLink = (periodicScheduler->Settings->Flags & USB_SCHEDULER_LINK_BIT_EOL) ? USB_ELEMENT_LINK_END : 0;

    assert(ElementPool < periodicScheduler->Settings->PoolCount);
    pool   = &periodicScheduler->Settings->Pools[ElementPool];
    object = USB_ELEMENT_OBJECT(pool, Element);

    for (size_t i = object->StartFrame; i < periodicScheduler->Settings->FrameCount; i += object->FrameInterval) {
        UsbSchedulerObject_t* existingObject = NULL;
        UsbSchedulerPool_t*   existingPool   = NULL;
        uint8_t*              existingElement = (uint8_t*)periodicScheduler->VirtualFrameList[i];

        oserr = UsbTransferArenaGetPoolFromElement(arena, existingElement, &existingPool);
        if (oserr != OS_EOK) {
            WARNING("UsbPeriodicSchedulerUnlinkElement: cannot get object from pool");
            return;
        }
        existingObject = USB_ELEMENT_OBJECT(existingPool, existingElement);

        if (existingElement == Element) {
            if (object->BreathIndex != USB_ELEMENT_NO_INDEX) {
                existingPool    = __GetPoolByIndex(periodicScheduler->Settings, object->BreathIndex);
                existingElement = USB_ELEMENT_INDEX(existingPool, object->BreathIndex);

                periodicScheduler->VirtualFrameList[i]    = (uintptr_t)existingElement;
                periodicScheduler->Settings->FrameList[i] = USB_ELEMENT_LINK(pool, Element, USB_CHAIN_BREATH);
            } else {
                periodicScheduler->VirtualFrameList[i]    = 0;
                periodicScheduler->Settings->FrameList[i] = noLink;
            }
        } else {
            while (existingObject->BreathIndex != USB_ELEMENT_NO_INDEX &&
                   existingObject->BreathIndex != object->Index) {
                existingPool    = __GetPoolByIndex(periodicScheduler->Settings, existingObject->BreathIndex);
                existingElement = USB_ELEMENT_INDEX(existingPool, existingObject->BreathIndex);
                existingObject  = USB_ELEMENT_OBJECT(existingPool, existingElement);
            }

            if (existingObject->BreathIndex == object->Index) {
                existingObject->BreathIndex = object->BreathIndex;
                USB_ELEMENT_LINK(existingPool, existingElement, USB_CHAIN_BREATH)
                        = USB_ELEMENT_LINK(pool, Element, USB_CHAIN_BREATH);
            }
        }
    }
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
    return UsbPeriodicSchedulerAllocateBandwidth(
        UsbSchedulerGetTransferArena(scheduler),
        UsbSchedulerGetPeriodicScheduler(scheduler),
        interval,
        mps,
        transactionType,
        bytesToTransfer,
        transferType,
        speed,
        element
    );
}

oserr_t
UsbSchedulerLinkPeriodicElement(
    _In_ UsbScheduler_t* Scheduler,
    _In_ int             ElementPool,
    _In_ uint8_t*        Element)
{
    return UsbPeriodicSchedulerLinkElement(
        UsbSchedulerGetTransferArena(Scheduler),
        UsbSchedulerGetPeriodicScheduler(Scheduler),
        ElementPool,
        Element
    );
}

void
UsbSchedulerUnlinkPeriodicElement(
    _In_ UsbScheduler_t* Scheduler,
    _In_ int             ElementPool,
    _In_ uint8_t*        Element)
{
    UsbPeriodicSchedulerUnlinkElement(
        UsbSchedulerGetTransferArena(Scheduler),
        UsbSchedulerGetPeriodicScheduler(Scheduler),
        ElementPool,
        Element
    );
}

void
UsbSchedulerFreeElement(
    _In_ UsbScheduler_t* usbScheduler,
    _In_ uint8_t*        element)
{
    UsbSchedulerPool_t*   pool   = NULL;
    UsbSchedulerObject_t* object = NULL;
    oserr_t               oserr;

    oserr = UsbTransferArenaGetPoolFromElement(UsbSchedulerGetTransferArena(usbScheduler), element, &pool);
    if (oserr != OS_EOK) {
        WARNING("UsbSchedulerFreeElement: cannot get object from pool");
        return;
    }
    object = USB_ELEMENT_OBJECT(pool, element);

    if ((object->Flags & USB_ELEMENT_BANDWIDTH) && UsbSchedulerHasPeriodicSchedule(usbScheduler)) {
        __FreeBandwidth(
                UsbSchedulerGetTransferArena(usbScheduler),
                UsbSchedulerGetPeriodicScheduler(usbScheduler),
                element
        );
    }
    UsbTransferArenaFreeElement(
        UsbSchedulerGetTransferArena(usbScheduler),
        element
    );
}
