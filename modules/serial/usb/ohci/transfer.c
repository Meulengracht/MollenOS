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
#include <os/utils.h>
#include "ohci.h"

/* Includes
 * - Library */
#include <ds/collection.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* OhciTransactionInitialize
 * Initializes a transaction by allocating a new endpoint-descriptor
 * and preparing it for usage */
OhciQueueHead_t*
OhciTransactionInitialize(
    _In_ OhciController_t*      Controller, 
    _In_ UsbTransfer_t*         Transfer)
{
    // Variables
    OhciQueueHead_t *Qh         = NULL;
    size_t TransactionsPerFrame = 0;

    // Allocate a new descriptor
    Qh                          = OhciQhAllocate(Controller);
    if (Qh == NULL) {
        return USB_OUT_OF_RESOURCES;
    }

    // Mark it inactive untill ready
    Qh->Flags           |= OHCI_QH_SKIP;
    if (Transfer->Type == InterruptTransfer || Transfer->Type == IsochronousTransfer) {
        TransactionsPerFrame = DIVUP(Transfer->Transactions[0].Length, Transfer->Endpoint.MaxPacketSize);
        Qh->Bandwidth        = (reg32_t)NS_TO_US(UsbCalculateBandwidth(Transfer->Speed, 
            Transfer->Endpoint.Direction, Transfer->Type, Transfer->Transactions[0].Length));
        if (Transfer->Speed == FullSpeed && Transfer->Type == IsochronousTransfer) {
            Qh->Interval    = (1 << Transfer->Endpoint.Interval);
        }
        else {
            Qh->Interval    = Transfer->Endpoint.Interval;
        }
    }
    return Qh;
}

/* OhciTransactionDispatch
 * Queues the transfer up in the controller hardware, after finalizing the
 * transactions and preparing them. */
UsbTransferStatus_t
OhciTransactionDispatch(
    _In_ OhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
{
    // Variables
    OhciQueueHead_t *Qh     = NULL;
    uintptr_t QhAddress     = 0;

    // Initiate some variables
    Qh          = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
    QhAddress   = OHCI_POOL_QHINDEX(Controller, Qh->Index);

    // Clear pauses
    Qh->Flags   &= ~OHCI_QH_SKIP;
    Qh->Current &= ~OHCI_LINK_HALTED;

    // Trace
    TRACE("Qh Address 0x%x, Flags 0x%x, Tail 0x%x, Current 0x%x, Link 0x%x", 
        QhAddress, Qh->Flags, Qh->EndPointer, Qh->Current, Qh->LinkPointer);

    // Set the schedule flag on ED and
    // enable SOF, ED is not scheduled before this interrupt
    Qh->HcdInformation                      |= OHCI_QH_SCHEDULE;
    Controller->Registers->HcInterruptStatus = OHCI_SOF_EVENT;
    Controller->Registers->HcInterruptEnable = OHCI_SOF_EVENT;
    
    Transfer->Status                         = TransferQueued;
    return TransferQueued;
}

/* OhciTransactionFinalize
 * Cleans up the transfer, deallocates resources and validates the td's */
OsStatus_t
OhciTransactionFinalize(
    _In_ OhciController_t       *Controller,
    _In_ UsbManagerTransfer_t   *Transfer,
    _In_ int                     Validate)
{
    // Variables
    OhciQueueHead_t *Qh             = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
    OhciTransferDescriptor_t *Td    = NULL;
    CollectionItem_t *Node          = NULL;
    int BytesLeft                   = 0;
    UsbTransferResult_t Result;

    // Debug
    TRACE("OhciTransactionFinalize()");

    // Step one is to unallocate the td's
    // Get first td
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    while (Td) {
        // Save link-index before resetting
        int LinkIndex = Td->LinkIndex;
        memset((void*)Td, 0, sizeof(OhciTransferDescriptor_t));
        if (LinkIndex != OHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[LinkIndex];
        }
        else {
            break;
        }
    }

    // Is the transfer done?
    if ((Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer)
        && Transfer->Status == TransferFinished
        && Transfer->TransactionsExecuted != Transfer->TransactionsTotal
        && !(Qh->HcdInformation & OHCI_QH_SHORTTRANSFER)) {
        BytesLeft = 1;
    }

    // We don't allocate the queue head before the transfer
    // is done, we might not be done yet
    if (BytesLeft == 1 && Validate == 1) {
        HciQueueTransferGeneric(Transfer);
        return OsError;
    }
    else {
        // Now unallocate the qh by zeroing that
        memset((void*)Qh, 0, sizeof(OhciQueueHead_t));
        Transfer->EndpointDescriptor = NULL;

        // Should we notify the user here?...
        if (Transfer->Requester != UUID_INVALID && 
            (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer)) {
            Result.Id               = Transfer->Id;
            Result.BytesTransferred = Transfer->BytesTransferred[0];
            Result.BytesTransferred += Transfer->BytesTransferred[1];
            Result.BytesTransferred += Transfer->BytesTransferred[2];
            Result.Status           = Transfer->Status;
            PipeSend(Transfer->Requester, Transfer->ResponsePort, (void*)&Result, sizeof(UsbTransferResult_t));
        }

        // Cleanup the transfer
        free(Transfer);

        // Now run through transactions and check if any are ready to run
        _foreach(Node, Controller->Base.TransactionList) {
            UsbManagerTransfer_t *NextTransfer = (UsbManagerTransfer_t*)Node->Data;
            if (NextTransfer->Status == TransferNotProcessed) {
                if (NextTransfer->Transfer.Type == IsochronousTransfer) {
                    HciQueueTransferIsochronous(NextTransfer);
                }
                else {
                    HciQueueTransferGeneric(NextTransfer);
                }
                break;
            }
        }
        return OsSuccess;
    }
}

/* HciDequeueTransfer 
 * Removes a queued transfer from the controller's transfer list */
UsbTransferStatus_t
HciDequeueTransfer(
    _In_ UsbManagerTransfer_t*      Transfer)
{
    // Variables
    OhciQueueHead_t *Qh             = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
    OhciController_t *Controller    = NULL;

    // Get Controller
    Controller  = (OhciController_t*)UsbManagerGetController(Transfer->DeviceId);

    // Mark for unscheduling and
    // enable SOF, ED is not scheduled before
    Qh->HcdInformation                      |= OHCI_QH_UNSCHEDULE;
    Controller->Registers->HcInterruptStatus = OHCI_SOF_EVENT;
    Controller->Registers->HcInterruptEnable = OHCI_SOF_EVENT;
    return TransferFinished;
}
