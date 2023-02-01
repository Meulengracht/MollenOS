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

#include <assert.h>
#include <ddk/barrier.h>
#include <usb/usb.h>
#include <ddk/utils.h>
#include "scheduler.h"

oserr_t
UsbSchedulerLinkPeriodicElement(
    _In_ UsbScheduler_t* Scheduler,
    _In_ int             ElementPool,
    _In_ uint8_t*        Element)
{
    oserr_t               oserr;
    UsbSchedulerObject_t* object;
    UsbSchedulerPool_t*   pool;
    uintptr_t             physicalAddress;
    size_t                i;
    
    assert(ElementPool < Scheduler->Settings.PoolCount);

    // Validate element and lookup pool
    pool            = &Scheduler->Settings.Pools[ElementPool];
    object          = USB_ELEMENT_OBJECT(pool, Element);
    physicalAddress = USB_ELEMENT_PHYSICAL(pool, object->Index);

    // Now loop through the bandwidth-phases and link it
    for (i = object->StartFrame; i < Scheduler->Settings.FrameCount; i += object->FrameInterval) {
        // Two cases, first or not first
        if (Scheduler->VirtualFrameList[i] == 0) {
            Scheduler->VirtualFrameList[i]   = (uintptr_t)Element;
            Scheduler->Settings.FrameList[i] = LODWORD(physicalAddress) | USB_ELEMENT_LINKFLAGS(object->Flags); //@todo if 64 bit support in xhci
        } else {
            // Ok, so we need to index us by Interval.
            // Lowest interval must come last
            UsbSchedulerObject_t* existingObject;
            UsbSchedulerPool_t*   existingPool;
            uint8_t*              existingElement = (uint8_t*)Scheduler->VirtualFrameList[i];

            // Get element and validate existance
            oserr = UsbSchedulerGetPoolFromElement(Scheduler, existingElement, &existingPool);
            if (oserr != OS_EOK) {
                WARNING("UsbSchedulerLinkPeriodicElement: cannot get object from pool");
                return oserr;
            }
            existingObject = USB_ELEMENT_OBJECT(existingPool, existingElement);

            // Isochronous always have first right regardless of interval
            // so if this is not isochronous loop past all isochronous
            //if (!(object->Flags & USB_ELEMENT_ISOCHRONOUS)) {
            //    while (existingObject->Flags & USB_ELEMENT_ISOCHRONOUS) {
            //        if (existingObject->BreathIndex == USB_ELEMENT_NO_INDEX ||
            //            existingObject == object) {
            //            break;
            //        }

            //        // Move to next object
            //        existingPool    = USB_ELEMENT_GET_POOL(Scheduler, existingObject->BreathIndex);
            //        existingElement = USB_ELEMENT_INDEX(existingPool, existingObject->BreathIndex);
            //        existingObject  = USB_ELEMENT_OBJECT(existingPool, existingElement);
            //    }
            //}
            // @todo as this will break the linkage

            // Iterate to correct spot based on interval
            while (existingObject->BreathIndex != USB_ELEMENT_NO_INDEX && existingObject != object) {
                if (object->FrameInterval > existingObject->FrameInterval) {
                    break;
                }

                // Move to next object
                existingPool    = USB_ELEMENT_GET_POOL(Scheduler, existingObject->BreathIndex);
                existingElement = USB_ELEMENT_INDEX(existingPool, existingObject->BreathIndex);
                existingObject  = USB_ELEMENT_OBJECT(existingPool, existingElement);
            }

            // Link us in only if we are not ourselves
            if (existingObject != object) {
                // Two insertion cases. To front or not to front
                // We must have a larger interval and existingelement must be front
                if (existingElement == (uint8_t*)Scheduler->VirtualFrameList[i] &&
                    object->FrameInterval > existingObject->FrameInterval) {
                    USB_ELEMENT_LINK(pool, Element, USB_CHAIN_BREATH) = Scheduler->Settings.FrameList[i];
                    object->BreathIndex                               = existingObject->Index;
                    dma_mb();
                    Scheduler->VirtualFrameList[i]   = (uintptr_t)Element;
                    Scheduler->Settings.FrameList[i] = LODWORD(physicalAddress) | USB_ELEMENT_LINKFLAGS(object->Flags); //@todo if 64 bit support in xhci
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
UsbSchedulerUnlinkPeriodicElement(
    _In_ UsbScheduler_t* Scheduler,
    _In_ int             ElementPool,
    _In_ uint8_t*        Element)
{
    oserr_t               oserr;
    UsbSchedulerObject_t* object;
    UsbSchedulerPool_t*   pool;
    reg32_t               noLink = (Scheduler->Settings.Flags & USB_SCHEDULER_LINK_BIT_EOL) ? USB_ELEMENT_LINK_END : 0;
    size_t                i;

    assert(ElementPool < Scheduler->Settings.PoolCount);

    // Validate element and lookup pool
    pool   = &Scheduler->Settings.Pools[ElementPool];
    object = USB_ELEMENT_OBJECT(pool, Element);

    // Iterate the bandwidth phases
    for (i = object->StartFrame; i < Scheduler->Settings.FrameCount; i += object->FrameInterval) {
        UsbSchedulerObject_t *ExistingObject = NULL;
        UsbSchedulerPool_t *ExistingPool     = NULL;
        uint8_t *ExistingElement             = (uint8_t*)Scheduler->VirtualFrameList[i];

        // Get element and validate existance
        oserr = UsbSchedulerGetPoolFromElement(Scheduler, ExistingElement, &ExistingPool);
        if (oserr != OS_EOK) {
            WARNING("UsbSchedulerUnlinkPeriodicElement: cannot get object from pool");
            return;
        }
        ExistingObject = USB_ELEMENT_OBJECT(ExistingPool, ExistingElement);

        // Two cases, root or not
        if (ExistingElement == Element) {
            if (object->BreathIndex != USB_ELEMENT_NO_INDEX) {
                // Move to next object
                ExistingPool    = USB_ELEMENT_GET_POOL(Scheduler, object->BreathIndex);
                ExistingElement = USB_ELEMENT_INDEX(ExistingPool, object->BreathIndex);

                Scheduler->VirtualFrameList[i]   = (uintptr_t)ExistingElement;
                Scheduler->Settings.FrameList[i] = USB_ELEMENT_LINK(pool, Element, USB_CHAIN_BREATH);
            } else {
                Scheduler->VirtualFrameList[i]   = 0;
                Scheduler->Settings.FrameList[i] = noLink;
            }
        } else {
            while (ExistingObject->BreathIndex != USB_ELEMENT_NO_INDEX &&
                   ExistingObject->BreathIndex != object->Index) {
                // Move to next object
                ExistingPool    = USB_ELEMENT_GET_POOL(Scheduler, ExistingObject->BreathIndex);
                ExistingElement = USB_ELEMENT_INDEX(ExistingPool, ExistingObject->BreathIndex);
                ExistingObject  = USB_ELEMENT_OBJECT(ExistingPool, ExistingElement);
            }

            // Unlink if found
            if (ExistingObject->BreathIndex == object->Index) {
                ExistingObject->BreathIndex = object->BreathIndex;
                USB_ELEMENT_LINK(ExistingPool, ExistingElement, USB_CHAIN_BREATH)
                    = USB_ELEMENT_LINK(pool, Element, USB_CHAIN_BREATH);
            }
        }
    }
}
