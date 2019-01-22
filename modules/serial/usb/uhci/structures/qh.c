/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * MollenOS MCore - Universal Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "../uhci.h"

/* Includes
 * - Library */
#include <ds/collection.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* UhciCalculateBandwidth
 * Calculates the queue for the queue-head. */
void
UhciQhCalculateQueue(
    _In_ UhciQueueHead_t*       Qh)
{
    // Variables
    int Exponent, Queue;

    // Calculate the correct index
    for (Exponent = 7; Exponent >= 0; --Exponent) {
        if ((1 << Exponent) <= (int)Qh->Object.FrameInterval) {
            break;
        }
    }

    // Update queue
    Queue       = 9 - Exponent;
    Qh->Queue   = Queue & 0xFF;
    TRACE("Queue for interrupt transfer: %i", Queue);
}

/* UhciQhInitialize
 * Initializes the queue head data-structure and the associated
 * hcd flags. Afterwards the queue head is ready for use. */
OsStatus_t
UhciQhInitialize(
    _In_ UhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
{
    // Variables
    UhciQueueHead_t *Qh = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
    OsStatus_t Status   = OsSuccess;

    // Initialize link
    Qh->Link            = UHCI_LINK_END;
    Qh->Child           = UHCI_LINK_END;

    // Initialize link flags
    Qh->Object.Flags    |= UHCI_LINK_QH;

    // Allocate bandwidth if int/isoc
    if (Transfer->Transfer.Type == InterruptTransfer || Transfer->Transfer.Type == IsochronousTransfer) {
        Status = UsbSchedulerAllocateBandwidth(Controller->Base.Scheduler, 
            &Transfer->Transfer.Endpoint, Transfer->Transfer.Transactions[0].Length,
            Transfer->Transfer.Type, Transfer->Transfer.Speed, (uint8_t*)Qh);
        if (Status != OsError) {
            UhciQhCalculateQueue(Qh);
        }
    }
    else {
        Qh->Queue       = UHCI_POOL_QH_ASYNC;
    }
    return Status;
}

/* UhciQhDump
 * Dumps the information contained in the queue-head by writing it to stdout */
void
UhciQhDump(
    _In_ UhciController_t*      Controller,
    _In_ UhciQueueHead_t*       Qh)
{
    // Variables
    uintptr_t PhysicalAddress   = 0;

    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_QH_POOL, 
        Qh->Object.Index & USB_ELEMENT_INDEX_MASK, NULL, &PhysicalAddress);
    WARNING("QH(0x%x): Flags 0x%x, Link 0x%x, Child 0x%x", 
        PhysicalAddress, Qh->Object.Flags, Qh->Link, Qh->Child);
}

/* UhciQhRestart
 * Restarts an interrupt QH by resetting it to it's start state */
void
UhciQhRestart(
    _In_ UhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
{
    // Variables
    UhciQueueHead_t *Qh             = NULL;
    uintptr_t LinkPhysical          = 0;
    
    // Setup some variables
    Qh              = (UhciQueueHead_t*)Transfer->EndpointDescriptor;

    // Do some extra processing for periodics
    Qh->BufferBase  = Transfer->Transfer.Transactions[0].BufferAddress;

    // Reinitialize the queue-head
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_TD_POOL, 
        Qh->Object.DepthIndex & USB_ELEMENT_INDEX_MASK, NULL, &LinkPhysical);
    Qh->Child       = LinkPhysical | UHCI_LINK_DEPTH;
}

/* UhciQhLink 
 * Link a given queue head into the correct queue determined by Qh->Queue.
 * This can handle linkage of async and interrupt transfers. */
void
UhciQhLink(
    _In_ UhciController_t*      Controller,
    _In_ UhciQueueHead_t*       Qh)
{
    // Variables
    UhciQueueHead_t *QueueQh        = NULL;
    uint16_t Marker                 = 0;

    // Get the queue for this queue-head
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_QH_POOL, 
        Qh->Queue, (uint8_t**)&QueueQh, NULL);

    // Handle async and interrupt a little differently
    if (Qh->Queue >= UHCI_POOL_QH_ASYNC) {
        Marker = USB_ELEMENT_NO_INDEX;
    }
    else {
        Marker = USB_ELEMENT_CREATE_INDEX(UHCI_QH_POOL, UHCI_POOL_QH_ASYNC);
    }
    
    // Link it
    UsbSchedulerChainElement(Controller->Base.Scheduler, 
        (uint8_t*)QueueQh, (uint8_t*)Qh, Marker, USB_CHAIN_BREATH);
}

/* UhciQhUnlink 
 * Unlinks a given queue head from the correct queue determined by Qh->Queue.
 * This can handle removal of async and interrupt transfers. */
void
UhciQhUnlink(
    _In_ UhciController_t*      Controller,
    _In_ UhciQueueHead_t*       Qh)
{
    // Variables
    UhciQueueHead_t *QueueQh        = NULL;

    // Get the queue for this queue-head
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_QH_POOL, 
        Qh->Queue, (uint8_t**)&QueueQh, NULL);

    // Unlink it
    UsbSchedulerUnchainElement(Controller->Base.Scheduler, 
        (uint8_t*)QueueQh, (uint8_t*)Qh, USB_CHAIN_BREATH);
}
