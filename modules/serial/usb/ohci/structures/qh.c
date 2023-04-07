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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

#include <os/mollenos.h>
#include <ddk/utils.h>
#include "../ohci.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

oserr_t
OhciQhInitialize(
    _In_ OhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer,
    _In_ size_t                 Address,
    _In_ size_t                 Endpoint)
{
    OhciQueueHead_t* Qh        = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
    oserr_t       Status    = OS_EOK;
    uintptr_t        LinkEnd   = 0;
    uint16_t         LastIndex;

    // Initialize link
    Qh->LinkPointer = 0;

    if (Transfer->Base.Type == USBTRANSFER_TYPE_ISOC) LastIndex = USB_ELEMENT_CREATE_INDEX(OHCI_iTD_POOL, OHCI_iTD_NULL);
    else                                              LastIndex = USB_ELEMENT_CREATE_INDEX(OHCI_TD_POOL, OHCI_TD_NULL);
    
    // Get last pointer
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler,
        (LastIndex >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK, 
        LastIndex & USB_ELEMENT_INDEX_MASK, NULL, &LinkEnd);
    
    Qh->Current           = (reg32_t)LinkEnd;
    Qh->EndPointer        = (reg32_t)LinkEnd;
    Qh->Object.DepthIndex = LastIndex;

    // Initialize flags
    Qh->Flags  = OHCI_QH_ADDRESS(Address);
    Qh->Flags |= OHCI_QH_ENDPOINT(Endpoint);
    Qh->Flags |= OHCI_QH_DIRECTIONTD; // Retrieve from TD
    Qh->Flags |= OHCI_QH_LENGTH(Transfer->Base.MaxPacketSize);
    Qh->Flags |= OHCI_QH_TYPE(Transfer->Base.Type);

    // Set conditional flags
    if (Transfer->Base.Speed == USB_SPEED_LOW) {
        Qh->Flags |= OHCI_QH_LOWSPEED;
    }
    if (Transfer->Base.Type == USBTRANSFER_TYPE_ISOC) {
        Qh->Flags |= OHCI_QH_ISOCHRONOUS;
    }

    // Allocate bandwidth if int/isoc
    if (Transfer->Base.Type == USB_TRANSFER_INTERRUPT || Transfer->Base.Type == USBTRANSFER_TYPE_ISOC) {
        Status = UsbSchedulerAllocateBandwidth(Controller->Base.Scheduler,
                                               Transfer->Base.PeriodicInterval, Transfer->Base.MaxPacketSize,
                                               Transfer->Base.Transactions[0].Type,
                                               Transfer->Base.Transactions[0].Length,
                                               Transfer->Base.Type, Transfer->Base.Speed, (uint8_t*)Qh);
    }
    return Status;
}

void
OHCIQHDump(
    _In_ OhciController_t* Controller,
    _In_ OhciQueueHead_t*  Qh)
{
    uintptr_t PhysicalAddress = 0;

    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_QH_POOL, 
        Qh->Object.Index & USB_ELEMENT_INDEX_MASK, NULL, &PhysicalAddress);
    WARNING("QH(0x%x): Flags 0x%x, Link 0x%x, Current 0x%x, End 0x%x", 
        PhysicalAddress, Qh->Object.Flags, Qh->LinkPointer, Qh->Current, Qh->EndPointer);
}

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

void
OhciQhLink(
    _In_ OhciController_t* Controller,
    _In_ uint8_t Type,
    _In_ OhciQueueHead_t*  Qh)
{
    OhciQueueHead_t* RootQh        = NULL;
    reg32_t          CommandStatus = READ_VOLATILE(Controller->Registers->HcCommandStatus);

    // Switch based on type of transfer
    if (Type == USBTRANSFER_TYPE_CONTROL) {
        // Is there anyone waiting?
        if (Controller->TransactionsWaitingControl > 0) {
            UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_QH_POOL, 
                Controller->TransactionQueueControlIndex & USB_ELEMENT_INDEX_MASK, 
                (uint8_t**)&RootQh, NULL);
            UsbSchedulerChainElement(Controller->Base.Scheduler, OHCI_QH_POOL, (uint8_t*)RootQh, 
                OHCI_QH_POOL, (uint8_t*)Qh, 0, USB_CHAIN_BREATH);
        }
        else {
            Controller->TransactionQueueControlIndex = Qh->Object.Index;
        }
        Controller->TransactionsWaitingControl++;

        // Enable?
        if (!(CommandStatus & OHCI_COMMAND_CONTROL_FILLED)) {
            OhciReloadAsynchronous(Controller, USBTRANSFER_TYPE_CONTROL);
            Controller->QueuesActive |= OHCI_CONTROL_CONTROL_ACTIVE;
        }
    }
    else if (Type == USB_TRANSFER_BULK) {
        // Is there anyone waiting?
        if (Controller->TransactionsWaitingBulk > 0) {
            UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_QH_POOL, 
                Controller->TransactionQueueBulkIndex & USB_ELEMENT_INDEX_MASK, 
                (uint8_t**)&RootQh, NULL);
            UsbSchedulerChainElement(Controller->Base.Scheduler, OHCI_QH_POOL, (uint8_t*)RootQh, 
                OHCI_QH_POOL, (uint8_t*)Qh, 0, USB_CHAIN_BREATH);
        }
        else {
            Controller->TransactionQueueBulkIndex = Qh->Object.Index;
        }
        Controller->TransactionsWaitingBulk++;

        // Enable?
        if (!(CommandStatus & OHCI_COMMAND_BULK_FILLED)) {
            OhciReloadAsynchronous(Controller, USB_TRANSFER_BULK);
            Controller->QueuesActive |= OHCI_CONTROL_BULK_ACTIVE;
        }
    }
}
