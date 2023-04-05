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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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

void
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
    Transfer->Status  = TransferInProgress;
    Transfer->Flags  |= TransferFlagSchedule;
    WRITE_VOLATILE(Controller->Registers->HcInterruptStatus, OHCI_SOF_EVENT);
    WRITE_VOLATILE(Controller->Registers->HcInterruptEnable, OHCI_SOF_EVENT);
}

void
OhciReloadAsynchronous(
        _In_ OhciController_t*    controller,
        _In_ enum USBTransferType transferType)
{
    OhciQueueHead_t* qh;
    uintptr_t        qhAddress;
    uint16_t         index = USB_ELEMENT_NO_INDEX;
    oserr_t          oserr;

    // Get correct new index
    if (transferType == USBTRANSFER_TYPE_CONTROL) index = controller->TransactionQueueControlIndex;
    else if (transferType == USBTRANSFER_TYPE_BULK) index = controller->TransactionQueueBulkIndex;
    assert(index != USB_ELEMENT_NO_INDEX);

    // Get the element data
    oserr = UsbSchedulerGetPoolElement(
            controller->Base.Scheduler,
            (index >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK,
            index & USB_ELEMENT_INDEX_MASK,
            (uint8_t**)&qh,
            &qhAddress
    );
    assert(oserr == OS_EOK);

    // Handle the different queues
    if (transferType == USBTRANSFER_TYPE_CONTROL) {
        WRITE_VOLATILE(controller->Registers->HcControlHeadED, LODWORD(qhAddress));
        WRITE_VOLATILE(controller->Registers->HcCommandStatus,
                       READ_VOLATILE(controller->Registers->HcCommandStatus) | OHCI_COMMAND_CONTROL_FILLED);

        // Reset control queue
        controller->TransactionQueueControlIndex = USB_ELEMENT_NO_INDEX;
        controller->TransactionsWaitingControl   = 0;
    } else if (transferType == USB_TRANSFER_BULK) {
        WRITE_VOLATILE(controller->Registers->HcBulkHeadED, LODWORD(qhAddress));
        WRITE_VOLATILE(controller->Registers->HcCommandStatus,
                       READ_VOLATILE(controller->Registers->HcCommandStatus) | OHCI_COMMAND_BULK_FILLED);

        // Reset bulk queue
        controller->TransactionQueueBulkIndex = USB_ELEMENT_NO_INDEX;
        controller->TransactionsWaitingBulk   = 0;
    }
}

oserr_t
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
    if (__Transfer_IsAsync(Transfer)) {
        // Check if the link of the QH is eol
        reg32_t Status  = READ_VOLATILE(OhciCtrl->Registers->HcCommandStatus);
        reg32_t Control = OhciCtrl->QueuesActive;
        if ((Status & OHCI_COMMAND_CONTROL_FILLED) == 0) {
            if (OhciCtrl->TransactionsWaitingControl != 0) {
                // Conditions for control fulfilled
                OhciReloadAsynchronous(OhciCtrl, USBTRANSFER_TYPE_CONTROL);
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
                OhciReloadAsynchronous(OhciCtrl, USBTRANSFER_TYPE_BULK);
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
    if (__Transfer_IsPeriodic(Transfer)) {
        UsbManagerChainEnumerate(Controller, Transfer->EndpointDescriptor,
            USB_CHAIN_DEPTH, HCIPROCESS_REASON_UNLINK, HCIProcessElement, Transfer);

        // Only do cleanup if reset is requested
        if (Reset != 0) {
            UsbManagerChainEnumerate(Controller, Transfer->EndpointDescriptor,
                USB_CHAIN_DEPTH, HCIPROCESS_REASON_CLEANUP, HCIProcessElement, Transfer);
        }
    } else {
        // Always cleanup asynchronous transfers
        UsbManagerChainEnumerate(Controller, Transfer->EndpointDescriptor,
            USB_CHAIN_DEPTH, HCIPROCESS_REASON_CLEANUP, HCIProcessElement, Transfer);
    }
    return OS_EOK;
}

oserr_t
HciDequeueTransfer(
    _In_ UsbManagerTransfer_t* Transfer)
{
    OhciController_t* Controller;

    Controller = (OhciController_t*) UsbManagerGetController(Transfer->DeviceID);
    if (!Controller) {
        return OS_EINVALPARAMS;
    }

    // Mark for unscheduling and
    // enable SOF, ED is not scheduled before
    Transfer->Flags |= TransferFlagUnschedule;
    WRITE_VOLATILE(Controller->Registers->HcInterruptStatus, OHCI_SOF_EVENT);
    WRITE_VOLATILE(Controller->Registers->HcInterruptEnable, OHCI_SOF_EVENT);
    return OS_EOK;
}
