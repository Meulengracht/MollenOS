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
 * Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */

//#define __TRACE
//#define __DIAGNOSE

#include <assert.h>
#include "ohci.h"
#include <os/mollenos.h>
#include <ddk/utils.h>

UsbTransferStatus_t
OhciTransactionDispatch(
    _In_ OhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
{
#ifdef __TRACE
    UsbManagerDumpChain(&Controller->Base, Transfer, 
        (uint8_t*)Transfer->EndpointDescriptor, USB_CHAIN_DEPTH);
#ifdef __DIAGNOSE
    for(;;);
#endif
#endif

    // Set the schedule flag on ED and
    // enable SOF, ED is not scheduled before this interrupt
    Transfer->Status  = TransferQueued;
    Transfer->Flags  |= TransferFlagSchedule;
    WRITE_VOLATILE(Controller->Registers->HcInterruptStatus, OHCI_SOF_EVENT);
    WRITE_VOLATILE(Controller->Registers->HcInterruptEnable, OHCI_SOF_EVENT);
    return TransferQueued;
}

void
OhciReloadAsynchronous(
    _In_ OhciController_t* Controller, 
    _In_ UsbTransferType_t TransferType)
{
    OhciQueueHead_t* Qh        = NULL;
    uintptr_t        QhAddress = 0;
    uint16_t         Index     = USB_ELEMENT_NO_INDEX;

    // Get correct new index
    if (TransferType == ControlTransfer)   Index = Controller->TransactionQueueControlIndex;
    else if (TransferType == BulkTransfer) Index = Controller->TransactionQueueBulkIndex;
    assert(Index != USB_ELEMENT_NO_INDEX);

    // Get the element data
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, 
        (Index >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK, 
        Index & USB_ELEMENT_INDEX_MASK, (uint8_t**)&Qh, &QhAddress);

    // Handle the different queues
    if (TransferType == ControlTransfer) {
        WRITE_VOLATILE(Controller->Registers->HcControlHeadED, LODWORD(QhAddress));
        WRITE_VOLATILE(Controller->Registers->HcCommandStatus, 
            READ_VOLATILE(Controller->Registers->HcCommandStatus) | OHCI_COMMAND_CONTROL_FILLED);

        // Reset control queue
        Controller->TransactionQueueControlIndex = USB_ELEMENT_NO_INDEX;
        Controller->TransactionsWaitingControl   = 0;
    }
    else if (TransferType == BulkTransfer) {
        WRITE_VOLATILE(Controller->Registers->HcBulkHeadED, LODWORD(QhAddress));
        WRITE_VOLATILE(Controller->Registers->HcCommandStatus, 
            READ_VOLATILE(Controller->Registers->HcCommandStatus) | OHCI_COMMAND_BULK_FILLED);

        // Reset bulk queue
        Controller->TransactionQueueBulkIndex = USB_ELEMENT_NO_INDEX;
        Controller->TransactionsWaitingBulk   = 0;
    }
}

OsStatus_t
HciTransactionFinalize(
    _In_ UsbManagerController_t* Controller,
    _In_ UsbManagerTransfer_t*   Transfer,
    _In_ int                     Reset)
{
    OhciController_t* OhciCtrl = (OhciController_t*)Controller;

    // Debug
    TRACE("OhciTransactionFinalize()");

    // If it's an asynchronous transfer check for end of link, then we should
    // reload the asynchronous queue
    if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
        // Check if the link of the QH is eol
        reg32_t Status  = READ_VOLATILE(OhciCtrl->Registers->HcCommandStatus);
        reg32_t Control = OhciCtrl->QueuesActive;
        if ((Status & OHCI_COMMAND_CONTROL_FILLED) == 0) {
            if (OhciCtrl->TransactionsWaitingControl != 0) {
                // Conditions for control fulfilled
                OhciReloadAsynchronous(OhciCtrl, ControlTransfer);
                Status  |= OHCI_COMMAND_CONTROL_FILLED;
                Control |= OHCI_CONTROL_CONTROL_ACTIVE;
            }
            else {
                if (READ_VOLATILE(OhciCtrl->Registers->HcControlHeadED) == 0) {
                    Control &= ~(OHCI_CONTROL_CONTROL_ACTIVE);
                }
            }
        }
        if ((Status & OHCI_COMMAND_BULK_FILLED) == 0) {
            if (OhciCtrl->TransactionsWaitingBulk != 0) {
                // Conditions for bulk fulfilled
                OhciReloadAsynchronous(OhciCtrl, BulkTransfer);
                Status  |= OHCI_COMMAND_BULK_FILLED;
                Control |= OHCI_CONTROL_BULK_ACTIVE;
            }
            else {
                if (READ_VOLATILE(OhciCtrl->Registers->HcBulkHeadED) == 0) {
                    Control &= ~(OHCI_CONTROL_BULK_ACTIVE);
                }
            }
        }

        // Update
        WRITE_VOLATILE(OhciCtrl->Registers->HcCommandStatus, Status);
        OhciCtrl->QueuesActive = Control;
    }

    // Don't unlink asynchronous transfers
    if (Transfer->Transfer.Type == InterruptTransfer || Transfer->Transfer.Type == IsochronousTransfer) {
        UsbManagerIterateChain(Controller, Transfer->EndpointDescriptor, 
            USB_CHAIN_DEPTH, USB_REASON_UNLINK, HciProcessElement, Transfer);

        // Only do cleanup if reset is requested
        if (Reset != 0) {
            UsbManagerIterateChain(Controller, Transfer->EndpointDescriptor,
                USB_CHAIN_DEPTH, USB_REASON_CLEANUP, HciProcessElement, Transfer);
        }
    }
    else {
        // Always cleanup asynchronous transfers
        UsbManagerIterateChain(Controller, Transfer->EndpointDescriptor,
            USB_CHAIN_DEPTH, USB_REASON_CLEANUP, HciProcessElement, Transfer);
    }
    return OsSuccess;
}

OsStatus_t
HciDequeueTransfer(
    _In_ UsbManagerTransfer_t* Transfer)
{
    OhciController_t* Controller;

    Controller = (OhciController_t*)UsbManagerGetController(Transfer->DeviceId);
    if (!Controller) {
        return OsInvalidParameters;
    }

    // Mark for unscheduling and
    // enable SOF, ED is not scheduled before
    Transfer->Flags |= TransferFlagUnschedule;
    WRITE_VOLATILE(Controller->Registers->HcInterruptStatus, OHCI_SOF_EVENT);
    WRITE_VOLATILE(Controller->Registers->HcInterruptEnable, OHCI_SOF_EVENT);
    return OsSuccess;
}
