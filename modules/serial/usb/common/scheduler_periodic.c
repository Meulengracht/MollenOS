/* MollenOS
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
 * MollenOS MCore - USB Controller Scheduler
 * - Contains the implementation of a shared controller scheduker
 *   for all the usb drivers
 */
//#define __TRACE

/* Includes
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>
#include "scheduler.h"

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

/* UsbSchedulerLinkPeriodicElement
 * Queue's up a periodic/isochronous transfer. If it was not possible
 * to schedule the transfer with the requested bandwidth, it returns
 * OsError */
OsStatus_t
UsbSchedulerLinkPeriodicElement(
    _In_ UsbScheduler_t*    Scheduler,
    _In_ uint8_t*           Element)
{
    // Variables
    UsbSchedulerObject_t *sObject   = NULL;
    UsbSchedulerPool_t *sPool       = NULL;
    uintptr_t PhysicalAddress       = 0;
    OsStatus_t Result               = OsSuccess;
    size_t i;

    // Validate element and lookup pool
    Result                  = UsbSchedulerGetPoolFromElement(Scheduler, Element, &sPool);
    assert(Result == OsSuccess);
    sObject                 = USB_ELEMENT_OBJECT(sPool, Element);
    PhysicalAddress         = USB_ELEMENT_PHYSICAL(sPool, sObject->Index);

    // Now loop through the bandwidth-phases and link it
    for (i = sObject->StartFrame; i < Scheduler->Settings.FrameCount; i += sObject->FrameInterval) {
        // Two cases, first or not first
        if (Scheduler->VirtualFrameList[i] == 0) {
            Scheduler->VirtualFrameList[i]      = (uintptr_t)Element;
            Scheduler->Settings.FrameList[i]    = LODWORD(PhysicalAddress) | USB_ELEMENT_LINKFLAGS(sObject->Flags); //@todo if 64 bit support in xhci
        }
        else {
            // Ok, so we need to index us by Interval
            // Lowest interval must come last
            UsbSchedulerObject_t *ExistingObject    = NULL;
            UsbSchedulerPool_t *ExistingPool        = NULL;
            uint8_t *ExistingElement                = (uint8_t*)Scheduler->VirtualFrameList[i];

            // Get element and validate existance
            Result                  = UsbSchedulerGetPoolFromElement(Scheduler, ExistingElement, &ExistingPool);
            assert(Result == OsSuccess);
            ExistingObject          = USB_ELEMENT_OBJECT(ExistingPool, ExistingElement);

            // Isochronous always have first right regardless of interval
            // so if this is not isochronous loop past all isochronous
            //if (!(sObject->Flags & USB_ELEMENT_ISOCHRONOUS)) {
            //    while (ExistingObject->Flags & USB_ELEMENT_ISOCHRONOUS) {
            //        if (ExistingObject->BreathIndex == USB_ELEMENT_NO_INDEX ||
            //            ExistingObject == sObject) {
            //            break;
            //        }

            //        // Move to next object
            //        ExistingPool    = USB_ELEMENT_GET_POOL(Scheduler, ExistingObject->BreathIndex);
            //        ExistingElement = USB_ELEMENT_INDEX(ExistingPool, ExistingObject->BreathIndex);
            //        ExistingObject  = USB_ELEMENT_OBJECT(ExistingPool, ExistingElement);
            //    }
            //}
            // @todo as this will break the linkage

            // Iterate to correct spot based on interval
            while (ExistingObject->BreathIndex != USB_ELEMENT_NO_INDEX && ExistingObject != sObject) {
                if (sObject->FrameInterval > ExistingObject->FrameInterval) {
                    break;
                }

                // Move to next object
                ExistingPool    = USB_ELEMENT_GET_POOL(Scheduler, ExistingObject->BreathIndex);
                ExistingElement = USB_ELEMENT_INDEX(ExistingPool, ExistingObject->BreathIndex);
                ExistingObject  = USB_ELEMENT_OBJECT(ExistingPool, ExistingElement);
            }

            // Link us in only if we are not ourselves
            if (ExistingObject != sObject) {
                // Two insertion cases. To front or not to front
                // We must have a larger interval and existingelement must be front
                if (ExistingElement == (uint8_t*)Scheduler->VirtualFrameList[i] &&
                    sObject->FrameInterval > ExistingObject->FrameInterval) {
                    USB_ELEMENT_LINK(sPool, Element, USB_CHAIN_BREATH)    = Scheduler->Settings.FrameList[i];
                    sObject->BreathIndex                = ExistingObject->Index;
                    MemoryBarrier();
                    Scheduler->VirtualFrameList[i]      = (uintptr_t)Element;
                    Scheduler->Settings.FrameList[i]    = LODWORD(PhysicalAddress) | USB_ELEMENT_LINKFLAGS(sObject->Flags); //@todo if 64 bit support in xhci
                }
                else {
                    USB_ELEMENT_LINK(sPool, Element, USB_CHAIN_BREATH)    = USB_ELEMENT_LINK(ExistingPool, ExistingElement, USB_CHAIN_BREATH);
                    sObject->BreathIndex                  = ExistingObject->BreathIndex;
                    MemoryBarrier();
                    USB_ELEMENT_LINK(ExistingPool, ExistingElement, USB_CHAIN_BREATH) = LODWORD(PhysicalAddress) | USB_ELEMENT_LINKFLAGS(sObject->Flags);
                    ExistingObject->BreathIndex           = sObject->Index;
                }
            }
        }
    }
    return OsSuccess;
}

/* UsbSchedulerUnlinkPeriodicElement
 * Removes an already queued up periodic transfer (interrupt/isoc) from the
 * controllers scheduler. */
void
UsbSchedulerUnlinkPeriodicElement(
    _In_ UsbScheduler_t*    Scheduler,
    _In_ uint8_t*           Element)
{
    // Variables
    UsbSchedulerObject_t *sObject   = NULL;
    UsbSchedulerPool_t *sPool       = NULL;
    OsStatus_t Result               = OsSuccess;
    reg32_t NoLink                  = (Scheduler->Settings.Flags & USB_SCHEDULER_LINK_BIT_EOL) ? USB_ELEMENT_LINK_END : 0;
    size_t i;

    // Validate element and lookup pool
    Result                  = UsbSchedulerGetPoolFromElement(Scheduler, Element, &sPool);
    assert(Result == OsSuccess);
    sObject                 = USB_ELEMENT_OBJECT(sPool, Element);

    // Iterate the bandwidth phases
    for (i = sObject->StartFrame; i < Scheduler->Settings.FrameCount; i += sObject->FrameInterval) {
        UsbSchedulerObject_t *ExistingObject    = NULL;
        UsbSchedulerPool_t *ExistingPool        = NULL;
        uint8_t *ExistingElement                = (uint8_t*)Scheduler->VirtualFrameList[i];

        // Get element and validate existance
        Result                  = UsbSchedulerGetPoolFromElement(Scheduler, ExistingElement, &ExistingPool);
        assert(Result == OsSuccess);
        ExistingObject          = USB_ELEMENT_OBJECT(ExistingPool, ExistingElement);

        // Two cases, root or not
        if (ExistingElement == Element) {
            if (sObject->BreathIndex != USB_ELEMENT_NO_INDEX) {
                // Move to next object
                ExistingPool    = USB_ELEMENT_GET_POOL(Scheduler, sObject->BreathIndex);
                ExistingElement = USB_ELEMENT_INDEX(ExistingPool, sObject->BreathIndex);

                Scheduler->VirtualFrameList[i]      = (uintptr_t)ExistingElement;
                Scheduler->Settings.FrameList[i]    = USB_ELEMENT_LINK(sPool, Element, USB_CHAIN_BREATH);
            }
            else {
                Scheduler->VirtualFrameList[i]      = 0;
                Scheduler->Settings.FrameList[i]    = NoLink;
            }
        }
        else {
            while (ExistingObject->BreathIndex != USB_ELEMENT_NO_INDEX && 
                   ExistingObject->BreathIndex != sObject->Index) {
                // Move to next object
                ExistingPool    = USB_ELEMENT_GET_POOL(Scheduler, ExistingObject->BreathIndex);
                ExistingElement = USB_ELEMENT_INDEX(ExistingPool, ExistingObject->BreathIndex);
                ExistingObject  = USB_ELEMENT_OBJECT(ExistingPool, ExistingElement);
            }

            // Unlink if found
            if (ExistingObject->BreathIndex == sObject->Index) {
                ExistingObject->BreathIndex                       = sObject->BreathIndex;
                USB_ELEMENT_LINK(ExistingPool, ExistingElement, USB_CHAIN_BREATH) = USB_ELEMENT_LINK(sPool, Element, USB_CHAIN_BREATH);
            }
        }
    }
}
