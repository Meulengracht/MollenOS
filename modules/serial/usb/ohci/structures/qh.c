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
 * MollenOS MCore - Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "../ohci.h"

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* OhciQhInitialize
 * Initializes the queue head data-structure and the associated
 * hcd flags. Afterwards the queue head is ready for use. */
OsStatus_t
OhciQhInitialize(
    _In_ OhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer,
    _In_ size_t                 Address,
    _In_ size_t                 Endpoint)
{
    // Variables
    OhciQueueHead_t *Qh = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
    OsStatus_t Status   = OsSuccess;
    uintptr_t LinkEnd   = 0;
    uint16_t LastIndex  = 0;

    // Initialize link
    Qh->LinkPointer     = 0;

    if (Transfer->Transfer.Type == IsochronousTransfer) LastIndex = USB_ELEMENT_CREATE_INDEX(OHCI_iTD_POOL, OHCI_iTD_NULL);
    else                                                LastIndex = USB_ELEMENT_CREATE_INDEX(OHCI_TD_POOL, OHCI_TD_NULL);
    
    // Get last pointer
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler,
        (LastIndex >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK, 
        LastIndex & USB_ELEMENT_INDEX_MASK, NULL, &LinkEnd);
    Qh->Current         = (reg32_t)LinkEnd;
    Qh->EndPointer      = (reg32_t)LinkEnd;
    Qh->Object.DepthIndex = LastIndex;

    // Initialize flags
    Qh->Flags           = OHCI_QH_ADDRESS(Address);
    Qh->Flags           |= OHCI_QH_ENDPOINT(Endpoint);
    Qh->Flags           |= OHCI_QH_DIRECTIONTD; // Retrieve from TD
    Qh->Flags           |= OHCI_QH_LENGTH(Transfer->Transfer.Endpoint.MaxPacketSize);
    Qh->Flags           |= OHCI_QH_TYPE(Transfer->Transfer.Type);

    // Set conditional flags
    if (Transfer->Transfer.Speed == LowSpeed) {
        Qh->Flags       |= OHCI_QH_LOWSPEED;
    }
    if (Transfer->Transfer.Type == IsochronousTransfer) {
        Qh->Flags       |= OHCI_QH_ISOCHRONOUS;
    }

    // Allocate bandwidth if int/isoc
    if (Transfer->Transfer.Type == InterruptTransfer || Transfer->Transfer.Type == IsochronousTransfer) {
        Status          = UsbSchedulerAllocateBandwidth(Controller->Base.Scheduler, 
            &Transfer->Transfer.Endpoint, Transfer->Transfer.Transactions[0].Length,
            Transfer->Transfer.Type, Transfer->Transfer.Speed, (uint8_t*)Qh);
    }
    return Status;
}

/* OhciQhDump
 * Dumps the information contained in the queue-head by writing it to stdout */
void
OhciQhDump(
    _In_ OhciController_t*      Controller,
    _In_ OhciQueueHead_t*       Qh)
{
    // Variables
    uintptr_t PhysicalAddress   = 0;

    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_QH_POOL, 
        Qh->Object.Index & USB_ELEMENT_INDEX_MASK, NULL, &PhysicalAddress);
    WARNING("QH(0x%x): Flags 0x%x, Link 0x%x, Current 0x%x, End 0x%x", 
        PhysicalAddress, Qh->Object.Flags, Qh->LinkPointer, Qh->Current, Qh->EndPointer);
}

/* OhciQhRestart
 * Restarts an interrupt QH by resetting it to it's start state */
void
OhciQhRestart(
    _In_ OhciController_t*          Controller,
    _In_ UsbManagerTransfer_t*      Transfer)
{
    // Variables
    OhciQueueHead_t *Qh     = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
    uintptr_t LinkAddress   = 0;

    // Reset the current link
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler,
        (Qh->Object.DepthIndex >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK, 
        Qh->Object.DepthIndex & USB_ELEMENT_INDEX_MASK, NULL, &LinkAddress);
    Qh->Current             = LinkAddress;
    assert(Qh->Current != 0);
}

/* OhciQhLink 
 * Link a given queue head into the correct queue determined by Qh->Queue.
 * This can handle linkage of async and interrupt transfers. */
void
OhciQhLink(
    _In_ OhciController_t*          Controller,
    _In_ UsbTransferType_t          Type,
    _In_ OhciQueueHead_t*           Qh)
{
    // Variables
    OhciQueueHead_t *RootQh = NULL;

    // Switch based on type of transfer
    if (Type == ControlTransfer) {
        // Is there anyone waiting?
        if (Controller->TransactionsWaitingControl > 0) {
            UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_QH_POOL, 
                Controller->TransactionQueueControlIndex & USB_ELEMENT_INDEX_MASK, 
                (uint8_t**)&RootQh, NULL);
            UsbSchedulerChainElement(Controller->Base.Scheduler, (uint8_t*)RootQh, 
                (uint8_t*)Qh, 0, USB_CHAIN_BREATH);
        }
        else {
            Controller->TransactionQueueControlIndex = Qh->Object.Index;
        }
        Controller->TransactionsWaitingControl++;

        // Enable?
        if (!(Controller->Registers->HcCommandStatus & OHCI_COMMAND_CONTROL_FILLED)) {
            OhciReloadAsynchronous(Controller, ControlTransfer);
            Controller->QueuesActive |= OHCI_CONTROL_CONTROL_ACTIVE;
        }
    }
    else if (Type == BulkTransfer) {
        // Is there anyone waiting?
        if (Controller->TransactionsWaitingBulk > 0) {
            UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_QH_POOL, 
                Controller->TransactionQueueBulkIndex & USB_ELEMENT_INDEX_MASK, 
                (uint8_t**)&RootQh, NULL);
            UsbSchedulerChainElement(Controller->Base.Scheduler, (uint8_t*)RootQh, 
                (uint8_t*)Qh, 0, USB_CHAIN_BREATH);
        }
        else {
            Controller->TransactionQueueBulkIndex = Qh->Object.Index;
        }
        Controller->TransactionsWaitingBulk++;

        // Enable?
        if (!(Controller->Registers->HcCommandStatus & OHCI_COMMAND_BULK_FILLED)) {
            OhciReloadAsynchronous(Controller, BulkTransfer);
            Controller->QueuesActive |= OHCI_CONTROL_BULK_ACTIVE;
        }
    }
}
