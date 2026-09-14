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
    UsbTransferArenaFreeElement(UsbSchedulerGetTransferArena(usbScheduler), element);
}
