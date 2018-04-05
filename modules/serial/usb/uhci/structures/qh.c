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
#include <os/utils.h>
#include "../uhci.h"

/* Includes
 * - Library */
#include <ds/collection.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* UhciQhAllocate
 * Allocates a new ED for usage with the transaction. If this returns
 * NULL we are out of ED's and we should wait till next transfer. */
UhciQueueHead_t*
UhciQhAllocate(
    _In_ UhciController_t*  Controller,
    _In_ UsbTransferType_t  Type,
    _In_ UsbSpeed_t         Speed)
{
    // Variables
    UhciQueueHead_t *Qh = NULL;
    int i;

    // Now, we usually allocated new endpoints for interrupts
    // and isoc, but it doesn't make sense for us as we keep one
    // large pool of QH's, just allocate from that in any case
    SpinlockAcquire(&Controller->Base.Lock);
    for (i = UHCI_POOL_START; i < UHCI_POOL_QHS; i++) {
        // Skip in case already allocated
        if (Controller->QueueControl.QHPool[i].Flags & UHCI_QH_ALLOCATED) {
            continue;
        }

        // We found a free qh - mark it allocated and end
        // but reset the QH first
        memset(&Controller->QueueControl.QHPool[i], 0, sizeof(UhciQueueHead_t));
        Controller->QueueControl.QHPool[i].Flags        = UHCI_QH_ALLOCATED | UHCI_QH_TYPE((reg32_t)Type);
        Controller->QueueControl.QHPool[i].Index        = (int16_t)i;
        if (Type == InterruptTransfer || Type == IsochronousTransfer) {
            Controller->QueueControl.QHPool[i].Flags    |= UHCI_QH_BANDWIDTH_ALLOC;
        }

        // Determine which queue-priority
        if (Speed == LowSpeed && Type == ControlTransfer) {
            Controller->QueueControl.QHPool[i].Queue    = UHCI_QH_LCTRL;
        }
        else if (Speed == FullSpeed && Type == ControlTransfer) {
            Controller->QueueControl.QHPool[i].Queue    = UHCI_QH_FCTRL;
        }
        else if (Type == BulkTransfer) {
            Controller->QueueControl.QHPool[i].Queue    = UHCI_QH_FBULK;
        }
        Qh                                              = &Controller->QueueControl.QHPool[i];
        break;
    }
    SpinlockRelease(&Controller->Base.Lock);
    return Qh;
}

/* UhciQhInitialize
 * Initializes the queue head data-structure and the associated
 * hcd flags. Afterwards the queue head is ready for use. */
void
UhciQhInitialize(
    _In_ UhciController_t*  Controller,
    _In_ UhciQueueHead_t*   Qh, 
    _In_ int16_t            HeadIndex)
{
    // Initialize link
    Qh->Link            = UHCI_LINK_END;
    Qh->LinkIndex       = UHCI_NO_INDEX;

    // Initialize children
    if (HeadIndex != UHCI_NO_INDEX) {
        Qh->Child       = (reg32_t)UHCI_POOL_TDINDEX(Controller, HeadIndex);
        Qh->ChildIndex  = HeadIndex;
    }
    else {
        Qh->Child       = 0;
        Qh->ChildIndex  = UHCI_NO_INDEX;
    }
}

/* UhciQhValidate
 * Iterates the queue head and the attached transfer descriptors
 * for errors and updates the transfer that is attached. */
void
UhciQhValidate(
    _In_ UhciController_t*      Controller, 
    _In_ UsbManagerTransfer_t*  Transfer,
    _In_ UhciQueueHead_t*       Qh)
{
    // Variables
    UhciTransferDescriptor_t *Td    = NULL;
    int ShortTransfer               = 0;

    // The initial state of the transfer will be complete
    Transfer->Status                = TransferFinished;
    Td                              = &Controller->QueueControl.TDPool[Qh->ChildIndex];

    // Iterate all the td's in the queue head, skip null td
    while (Td) {
        UhciTdValidate(Controller, Transfer, Td, &ShortTransfer);
        if (ShortTransfer == 1) {
            Qh->Flags |= UHCI_QH_SHORTTRANSFER;
        }

        // Go to next td
        if (Td->LinkIndex != UHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
        }
        else {
            break;
        }
    }
}