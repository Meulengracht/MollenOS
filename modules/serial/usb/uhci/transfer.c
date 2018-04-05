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
#include "uhci.h"

/* Includes
 * - Library */
#include <ds/collection.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* UhciCalculateBandwidth
 * Calculates and allocates bandwidth for the queue-head. */
OsStatus_t
UhciCalculateBandwidth(
    _In_ UhciController_t*      Controller, 
    _In_ UsbTransfer_t*         Transfer,
    _In_ UhciQueueHead_t*       Qh)
{
    // Variables
    size_t TransactionCount = 0;
    OsStatus_t Run          = OsError;
    int Exponent, Queue;

    // Calculate transaction count
    TransactionCount = DIVUP(Transfer->Transactions[0].Length, Transfer->Endpoint.MaxPacketSize);

    // Calculate the correct index
    for (Exponent = 7; Exponent >= 0; --Exponent) {
        if ((1 << Exponent) <= (int)Transfer->Endpoint.Interval) {
            break;
        }
    }

    // Sanitize that the exponent is valid
    if (Exponent < 0) {
        ERROR("UHCI: Invalid interval %u", Transfer->Endpoint.Interval);
        Exponent = 0;
    }

    // Calculate the bandwidth
    Qh->Bandwidth = UsbCalculateBandwidth(Transfer->Speed, Transfer->Endpoint.Direction, 
        Transfer->Type, Transfer->Transactions[0].Length);

    // Make sure we have enough bandwidth for the transfer
    if (Exponent > 0) {
        while (Run != OsSuccess && --Exponent >= 0) {
            // Select queue
            Queue       = 9 - Exponent;

            // Calculate initial period
            Qh->Period  = 1 << Exponent;
            Qh->Queue   = Queue & 0xFF;

            // For now, interrupt phase is fixed by the layout
            // of the QH lists.
            Qh->Phase   = (Qh->Period / 2) & (UHCI_BANDWIDTH_PHASES - 1);

            // Validate the bandwidth
            Run         = UsbSchedulerValidate(Controller->Scheduler, 
                Qh->Period, Qh->Bandwidth, TransactionCount);
        }
    }
    else {
        // Select queue
        Queue           = 9 - Exponent;

        // Calculate initial period
        Qh->Period      = 1 << Exponent;
        Qh->Queue       = Queue & 0xFF;

        // For now, interrupt phase is fixed by the layout
        // of the QH lists.
        Qh->Phase       = (Qh->Period / 2) & (UHCI_BANDWIDTH_PHASES - 1);

        // Validate the bandwidth
        Run = UsbSchedulerValidate(Controller->Scheduler,
            Qh->Period, Qh->Bandwidth, TransactionCount);
    }

    // Sanitize the validation
    if (Run == OsError) {
        ERROR("UHCI: Had no room for the transfer in queueus");
        return OsError;
    }

    // Reserve and done!
    return UsbSchedulerReserveBandwidth(Controller->Scheduler,
        Qh->Period, Qh->Bandwidth, TransactionCount, &Qh->StartFrame, NULL);
}

/* UhciTransactionInitialize
 * Initializes a transaction by allocating a new endpoint-descriptor
 * and preparing it for usage */
OsStatus_t
UhciTransactionInitialize(
    _In_  UhciController_t*     Controller, 
    _In_  UsbTransfer_t*        Transfer,
    _Out_ UhciQueueHead_t**     QhResult)
{
    // Variables
    UhciQueueHead_t *Qh     = NULL;

    // Allocate a new queue-head
    *QhResult = Qh  = UhciQhAllocate(Controller, Transfer->Type, Transfer->Speed);
    if (Qh == NULL) {
        *QhResult   = USB_OUT_OF_RESOURCES;
        return OsError;
    }

    // Handle bandwidth allocation if neccessary
    if (Qh->Flags & UHCI_QH_BANDWIDTH_ALLOC) {
        if (UhciCalculateBandwidth(Controller, Transfer, Qh) != OsSuccess) {
            memset((void*)Qh, 0, sizeof(UhciQueueHead_t));
            *QhResult = USB_OUT_OF_RESOURCES;
            return OsError;
        }
    }
    return OsSuccess;
}

/* UhciTransactionDispatch
 * Queues the transfer up in the controller hardware, after finalizing the
 * transactions and preparing them. */
UsbTransferStatus_t
UhciTransactionDispatch(
    _In_ UhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
{
    // Variables
    UhciQueueHead_t *Qh     = NULL;
    uintptr_t QhAddress     = 0;
    int QhIndex             = -1;

    // Instantiate values
    Qh                      = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
    QhIndex                 = UHCI_QH_GET_INDEX(Qh->Flags);
    QhAddress               = UHCI_POOL_QHINDEX(Controller, QhIndex);

    // Trace
    TRACE("UHCI: QH at 0x%x, FirstTd 0x%x, NextQh 0x%x", 
        QhAddress, Qh->Child, Qh->Link);
    TRACE("UHCI: Queue %u, StartFrame %u, Flags 0x%x", 
        Qh->Queue, Qh->StartFrame, Qh->Flags);

    // Asynchronous requests, Control & Bulk
    UhciUpdateCurrentFrame(Controller);
    if (Qh->Queue >= UHCI_QH_ASYNC) {
        
        // Variables
        UhciQueueHead_t *PrevQh = &Controller->QueueControl.QHPool[UHCI_QH_ASYNC];

        // Iterate and find a spot, based on the queue priority
        TRACE("(%u) Linking asynchronous queue-head (async-next: %i)", 
            Controller->QueueControl.Frame, PrevQh->LinkIndex);
        TRACE("Controller status: 0x%x", UhciRead16(Controller, UHCI_REGISTER_COMMAND));
        while (PrevQh->LinkIndex != UHCI_NO_INDEX) {
            if (PrevQh->Queue <= Qh->Queue) {
                break;
            }

            // Go to next qh
            PrevQh = &Controller->QueueControl.QHPool[PrevQh->LinkIndex];
        }

        // Insert in-between the two by transfering link
        // Make use of a memory-barrier to flush
        Qh->Link = PrevQh->Link;
        Qh->LinkIndex = PrevQh->LinkIndex;
        MemoryBarrier();
        PrevQh->Link = (QhAddress | UHCI_LINK_QH);
        PrevQh->LinkIndex = QhIndex;
    }
    // Periodic requests
    else if (Qh->Queue > UHCI_QH_ISOCHRONOUS && Qh->Queue < UHCI_QH_ASYNC) {
        
        // Variables
        UhciQueueHead_t *ExistingQueueHead = 
            &Controller->QueueControl.QHPool[Qh->Queue];

        // Iterate to end of interrupt list
        while (ExistingQueueHead->LinkIndex != UHCI_QH_ASYNC) {
            ExistingQueueHead = &Controller->QueueControl.
                QHPool[ExistingQueueHead->LinkIndex];
        }

        // Insert in-between the two by transfering link
        // Make use of a memory-barrier to flush
        Qh->Link = ExistingQueueHead->Link;
        Qh->LinkIndex = ExistingQueueHead->LinkIndex;
        MemoryBarrier();
        ExistingQueueHead->Link = (QhAddress | UHCI_LINK_QH);
        ExistingQueueHead->LinkIndex = QhIndex;
    }
    else {
        UhciLinkIsochronous(Controller, Qh);
    }
    Transfer->Status = TransferQueued;
    return TransferQueued;
}

/* UhciTransactionFinalize
 * Cleans up the transfer, deallocates resources and validates the td's */
OsStatus_t
UhciTransactionFinalize(
    _In_ UhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer,
    _In_ int                    Validate)
{
    // Variables
    UhciQueueHead_t *Qh             = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
    UhciTransferDescriptor_t *Td    = NULL;
    CollectionItem_t *Node          = NULL;
    int BytesLeft                   = 0;
    UsbTransferResult_t Result;
    
    // Debug
    TRACE("UhciTransactionFinalize()");

    // Unlink qh
    if (Transfer->Transfer.Type != IsochronousTransfer) {
        UhciUnlinkGeneric(Controller, Transfer, Qh);
    }
    else {
        // Unlink and release bandwidth
        UhciUnlinkIsochronous(Controller, Qh);
        UsbSchedulerReleaseBandwidth(Controller->Scheduler, Qh->Period,
            Qh->Bandwidth, Qh->StartFrame, 1);
    }

    // Step two is to unallocate the td's
    // Get first td
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    while (Td) {
        // Save link-index before resetting
        int LinkIndex = Td->LinkIndex;

        // Reset the TD, nothing in there is something we store further
        memset((void*)Td, 0, sizeof(UhciTransferDescriptor_t));

        // Go to next td or terminate
        if (LinkIndex != UHCI_NO_INDEX 
            && LinkIndex != UHCI_POOL_TDNULL) {
            Td = &Controller->QueueControl.TDPool[LinkIndex];
        }
        else {
            break;
        }
    }

    // Is the transfer done?
    if ((Transfer->Transfer.Type == ControlTransfer
        || Transfer->Transfer.Type == BulkTransfer)
        && Transfer->Status == TransferFinished
        && Transfer->TransactionsExecuted != Transfer->TransactionsTotal
        && !(Qh->Flags & UHCI_QH_SHORTTRANSFER)) {
        BytesLeft = 1;
    }

    // We don't allocate the queue head before the transfer
    // is done, we might not be done yet
    if (BytesLeft == 1) {
        HciQueueTransferGeneric(Transfer);
        return OsError;
    }
    else {
        // Now unallocate the Qh by zeroing that
        memset((void*)Qh, 0, sizeof(UhciQueueHead_t));
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
    UhciQueueHead_t *Qh             = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
    UhciController_t *Controller    = NULL;

    // Get Controller
    Controller = (UhciController_t*)UsbManagerGetController(Transfer->DeviceId);

    // Mark for unscheduling on next interrupt/check
    Qh->Flags |= UHCI_QH_UNSCHEDULE;
    return TransferFinished;
}
