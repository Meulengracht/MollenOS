/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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

/* UhciQueueDebug
 * Dumps the QH-settings and all the attached td's */
void
UhciQueueDebug(
    _In_ UhciController_t   *Controller,
    _In_ UhciQueueHead_t    *Qh)
{
    // Variables
    UhciTransferDescriptor_t *Td = NULL;
    uintptr_t PhysicalAddress = 0;

    PhysicalAddress = UHCI_POOL_QHINDEX(Controller, UHCI_QH_GET_INDEX(Qh->Flags));
    TRACE("QH(0x%x): Flags 0x%x, NextQh 0x%x, FirstChild 0x%x", 
        PhysicalAddress, Qh->Flags, Qh->Link, Qh->Child);

    // Get first td
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    while (Td != NULL) {
        PhysicalAddress = UHCI_POOL_TDINDEX(Controller, UHCI_TD_GET_INDEX(Td->HcdFlags));
        TRACE("TD(0x%x): Link 0x%x, Flags 0x%x, Header 0x%x, Buffer 0x%x", 
            PhysicalAddress, Td->Link, Td->Flags, Td->Header, Td->Buffer);
        // Go to next td
        if (Td->LinkIndex != UHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
        }
        else {
            Td = NULL;
        }
    }
}

/* UhciTransactionInitialize
 * Initializes a transaction by allocating a new endpoint-descriptor
 * and preparing it for usage */
OsStatus_t
UhciTransactionInitialize(
    _In_ UhciController_t   *Controller, 
    _In_ UsbTransfer_t      *Transfer,
    _Out_ UhciQueueHead_t  **QhResult)
{
    // Variables
    UhciQueueHead_t *Qh = NULL;
    size_t TransactionCount = 0;

    // Allocate a new queue-head
    *QhResult = Qh = UhciQhAllocate(Controller, Transfer->Type, Transfer->Speed);

    // Handle bandwidth allocation if neccessary
    if (Qh != NULL && (Qh->Flags & UHCI_QH_BANDWIDTH_ALLOC)) {
        // Variables
        OsStatus_t Run = OsError;
        int Exponent, Queue;

        // Calculate transaction count
        TransactionCount = DIVUP(Transfer->Transactions[0].Length, Transfer->Endpoint.MaxPacketSize);

        // Calculate the correct index
        for (Exponent = 7; Exponent >= 0; --Exponent) {
            if ((1 << Exponent) <= (int)Transfer->Endpoint.Interval)
                break;
        }

        // Sanitize that the exponent is valid
        if (Exponent < 0) {
            ERROR("UHCI: Invalid interval %u", Transfer->Endpoint.Interval);
            Exponent = 0;
        }

        // Calculate the bandwidth
        Qh->Bandwidth = UsbCalculateBandwidth(Transfer->Speed, 
            Transfer->Endpoint.Direction, Transfer->Type, 
            Transfer->Transactions[0].Length);

        // Make sure we have enough bandwidth for the transfer
        if (Exponent > 0) {
            while (Run != OsSuccess && --Exponent >= 0) {
                // Select queue
                Queue = 9 - Exponent;

                // Calculate initial period
                Qh->Period = 1 << Exponent;

                // Update the queue of qh
                Qh->Flags = UHCI_QH_CLR_QUEUE(Qh->Flags);
                Qh->Flags |= UHCI_QH_SET_QUEUE(Queue);

                // For now, interrupt phase is fixed by the layout
                // of the QH lists.
                Qh->Phase = (Qh->Period / 2) & (UHCI_BANDWIDTH_PHASES - 1);

                // Validate the bandwidth
                Run = UsbSchedulerValidate(Controller->Scheduler,
                    Qh->Period, Qh->Bandwidth, TransactionCount);
            }
        }
        else {
            // Select queue
            Queue = 9 - Exponent;

            // Calculate initial period
            Qh->Period = 1 << Exponent;

            // Update the queue of qh
            Qh->Flags = UHCI_QH_CLR_QUEUE(Qh->Flags);
            Qh->Flags |= UHCI_QH_SET_QUEUE(Queue);

            // For now, interrupt phase is fixed by the layout
            // of the QH lists.
            Qh->Phase = (Qh->Period / 2) & (UHCI_BANDWIDTH_PHASES - 1);

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
        UsbSchedulerReserveBandwidth(Controller->Scheduler,
                    Qh->Period, Qh->Bandwidth, TransactionCount, 
                    &Qh->StartFrame, NULL);
    }

    // Done
    return OsSuccess;
}

/* UhciTransactionCount
 * Returns the number of transactions neccessary for the transfer. */
OsStatus_t
UhciTransactionCount(
    _In_ UhciController_t       *Controller,
    _In_ UsbManagerTransfer_t   *Transfer,
    _Out_ int                   *TransactionsTotal)
{
    // Variables
    int TransactionCount    = 0;
    int i;

    // Get next address from which we need to load
    for (i = 0; i < Transfer->Transfer.TransactionCount; i++) {
        UsbTransactionType_t Type   = Transfer->Transfer.Transactions[i].Type;
        size_t BytesToTransfer      = Transfer->Transfer.Transactions[i].Length;
        size_t ByteOffset           = 0;
        size_t ByteStep             = 0;
        int AddZeroLength           = 0;

        // Keep adding td's
        while (BytesToTransfer || AddZeroLength == 1
            || Transfer->Transfer.Transactions[i].ZeroLength == 1) {
            if (Type == SetupTransaction) {
                ByteStep    = BytesToTransfer;
            }
            else {
                ByteStep    = MIN(BytesToTransfer, Transfer->Transfer.Endpoint.MaxPacketSize);
            }
            TransactionCount++;

            // Break out on zero lengths
            if (Transfer->Transfer.Transactions[i].ZeroLength == 1
                || AddZeroLength == 1) {
                break;
            }

            // Reduce
            BytesToTransfer -= ByteStep;
            ByteOffset      += ByteStep;

            // If it was out, and we had a multiple of MPS, then ZLP
            if (ByteStep == Transfer->Transfer.Endpoint.MaxPacketSize 
                && BytesToTransfer == 0
                && Transfer->Transfer.Type == BulkTransfer
                && Transfer->Transfer.Transactions[i].Type == OutTransaction) {
                AddZeroLength = 1;
            }
        }
    }
    *TransactionsTotal = TransactionCount;
    return OsSuccess;
}

/* UhciTransferFill 
 * Fills the transfer with as many transfer-descriptors as possible/needed. */
OsStatus_t
UhciTransferFill(
    _In_ UhciController_t           *Controller,
    _InOut_ UsbManagerTransfer_t    *Transfer)
{
    // Variables
    UhciTransferDescriptor_t *InitialTd     = NULL;
    UhciTransferDescriptor_t *PreviousTd    = NULL;
    UhciTransferDescriptor_t *Td            = NULL;
    size_t Address, Endpoint;
    int OutOfResources                      = 0;
    int i;

    // Debug
    TRACE("UhciTransferFill()");

    // Extract address and endpoint
    Address = HIWORD(Transfer->Pipe);
    Endpoint = LOWORD(Transfer->Pipe);

    // Get next address from which we need to load
    for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
        UsbTransactionType_t Type   = Transfer->Transfer.Transactions[i].Type;
        size_t BytesToTransfer      = Transfer->Transfer.Transactions[i].Length;
        size_t ByteOffset           = 0;
        size_t ByteStep             = 0;
        int PreviousToggle          = -1;
        int Toggle                  = 0;

        // Adjust offsets
        ByteOffset                  = Transfer->BytesTransferred[i];
        BytesToTransfer            -= Transfer->BytesTransferred[i];
        if (BytesToTransfer == 0 && Transfer->Transfer.Transactions[i].ZeroLength != 1) {
            continue;
        }

        // If it's a handshake package AND it's first td
        // of package, then set toggle
        if (ByteOffset == 0 && Transfer->Transfer.Transactions[i].Handshake) {
            Transfer->Transfer.Transactions[i].Handshake = 0;
            PreviousToggle          = UsbManagerGetToggle(Transfer->DeviceId, Transfer->Pipe);
            UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, 1);
        }

        // Keep adding td's
        while (BytesToTransfer || Transfer->Transfer.Transactions[i].ZeroLength == 1) {
            Toggle          = UsbManagerGetToggle(Transfer->DeviceId, Transfer->Pipe);
            if (Type == SetupTransaction) {
                ByteStep    = BytesToTransfer;
                Td          = UhciTdSetup(Controller, &Transfer->Transfer.Transactions[i], 
                    Address, Endpoint, Transfer->Transfer.Type, Transfer->Transfer.Speed);
            }
            else {
                ByteStep    = MIN(BytesToTransfer, Transfer->Transfer.Endpoint.MaxPacketSize);
                Td          = UhciTdIo(Controller, Transfer->Transfer.Type, 
                    (Type == InTransaction ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT), 
                    Toggle, Address, Endpoint, Transfer->Transfer.Endpoint.MaxPacketSize,
                    Transfer->Transfer.Speed, 
                    Transfer->Transfer.Transactions[i].BufferAddress + ByteOffset, ByteStep);
            }

            // If we didn't allocate a td, we ran out of 
            // resources, and have to wait for more. Queue up what we have
            if (Td == NULL) {
                if (PreviousToggle != -1) {
                    UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, PreviousToggle);
                    Transfer->Transfer.Transactions[i].Handshake = 1;
                }
                OutOfResources = 1;
                break;
            }
            else {
                // Store first
                if (InitialTd == NULL) {
                    InitialTd   = Td;
                    PreviousTd  = Td;
                }
                else {
                    // Update physical link
                    PreviousTd->LinkIndex   = UHCI_TD_GET_INDEX(Td->HcdFlags);
                    PreviousTd->Link        = (UHCI_POOL_TDINDEX(Controller, PreviousTd->LinkIndex) | UHCI_LINK_DEPTH);
                    PreviousTd              = Td;
                }

                // Update toggle by flipping
                UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, Toggle ^ 1);

                // Break out on zero lengths
                if (Transfer->Transfer.Transactions[i].ZeroLength == 1) {
                    Transfer->Transfer.Transactions[i].ZeroLength = 0;
                    break;
                }

                // Reduce
                BytesToTransfer -= ByteStep;
                ByteOffset      += ByteStep;

                // If it was out, and we had a multiple of MPS, then ZLP
                if (ByteStep == Transfer->Transfer.Endpoint.MaxPacketSize 
                    && BytesToTransfer == 0
                    && Transfer->Transfer.Type == BulkTransfer
                    && Transfer->Transfer.Transactions[i].Type == OutTransaction) {
                    Transfer->Transfer.Transactions[i].ZeroLength = 1;
                }
            }
        }

        // Cancel?
        if (OutOfResources == 1) {
            break;
        }
    }
        
    // End of <transfer>?
    if (PreviousTd != NULL) {
        PreviousTd->Flags |= UHCI_TD_IOC;
        UhciQhInitialize(Controller, Transfer->EndpointDescriptor, 
            UHCI_TD_GET_INDEX(InitialTd->HcdFlags));
        return OsSuccess;
    }
    else {
        // Queue up for later
        return OsError;
    }
}

/* UhciTransactionDispatch
 * Queues the transfer up in the controller hardware, after finalizing the
 * transactions and preparing them. */
UsbTransferStatus_t
UhciTransactionDispatch(
    _In_ UhciController_t       *Controller,
    _In_ UsbManagerTransfer_t   *Transfer)
{
    // Variables
    UhciQueueHead_t *Qh     = NULL;
    uintptr_t QhAddress     = 0;
    int QhIndex             = -1;
    int Queue               = -1;

    /*************************
     ****** SETUP PHASE ******
     *************************/
    Qh                      = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
    QhIndex                 = UHCI_QH_GET_INDEX(Qh->Flags);
    Queue                   = UHCI_QH_GET_QUEUE(Qh->Flags);

    // Lookup physical
    QhAddress               = UHCI_POOL_QHINDEX(Controller, QhIndex);

    // Trace
    TRACE("UHCI: QH at 0x%x, FirstTd 0x%x, NextQh 0x%x", 
        QhAddress, Qh->Child, Qh->Link);
    TRACE("UHCI: Queue %u, StartFrame %u, Flags 0x%x", 
        Queue, Qh->StartFrame, Qh->Flags);

    /*************************
     **** LINKING PHASE ******
     *************************/
    UhciUpdateCurrentFrame(Controller);

    // Asynchronous requests, Control & Bulk
    if (Queue >= UHCI_QH_ASYNC) {
        
        // Variables
        UhciQueueHead_t *PrevQh = &Controller->QueueControl.QHPool[UHCI_QH_ASYNC];
        int PrevQueue = UHCI_QH_GET_QUEUE(PrevQh->Flags);

        // Iterate and find a spot, based on the queue priority
        TRACE("(%u) Linking asynchronous queue-head (async-next: %i)", 
            Controller->QueueControl.Frame, PrevQh->LinkIndex);
        TRACE("Controller status: 0x%x", UhciRead16(Controller, UHCI_REGISTER_COMMAND));
        while (PrevQh->LinkIndex != UHCI_NO_INDEX) {
            PrevQueue = UHCI_QH_GET_QUEUE(PrevQh->Flags);
            if (PrevQueue <= Queue) {
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
#ifdef UHCI_FSBR
        /* FSBR? */
        if (PrevQueue < UHCI_QH_FSBRQ
            && Queue >= UHCI_QH_FSBRQ) {
            /* Link NULL to fsbr */
            Ctrl->QhPool[UHCI_POOL_NULL]->Link = (QhAddress | UHCI_TD_LINK_QH);
            Ctrl->QhPool[UHCI_POOL_NULL]->LinkVirtual = (uint32_t)Qh;
            
            /* Link last QH to NULL */
            PrevQh = Ctrl->QhPool[UHCI_POOL_ASYNC];
            while (PrevQh->LinkVirtual != 0)
                PrevQh = (UhciQueueHead_t*)PrevQh->LinkVirtual;
            PrevQh->Link = (Ctrl->QhPoolPhys[UHCI_POOL_NULL] | UHCI_TD_LINK_QH);
            PrevQh->LinkVirtual = (uint32_t)Ctrl->QhPool[UHCI_POOL_NULL];
        }
#endif
    }
    // Periodic requests
    else if (Queue > UHCI_QH_ISOCHRONOUS && Queue < UHCI_QH_ASYNC) {
        
        // Variables
        UhciQueueHead_t *ExistingQueueHead = 
            &Controller->QueueControl.QHPool[Queue];

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

    // Done
    return TransferQueued;
}

/* UhciTransactionFinalize
 * Cleans up the transfer, deallocates resources and validates the td's */
OsStatus_t
UhciTransactionFinalize(
    _In_ UhciController_t *Controller,
    _In_ UsbManagerTransfer_t *Transfer,
    _In_ int Validate)
{
    // Variables
    UhciQueueHead_t *Qh             = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
    UhciTransferDescriptor_t *Td    = NULL;
    int ShortTransfer               = 0;
    int BytesLeft                   = 0;
    int i;
    UsbTransferResult_t Result;
    
    // Debug
    TRACE("UhciTransactionFinalize()");

    // Set status to finished initially
    Transfer->Status = TransferFinished;

    /*************************
     *** VALIDATION PHASE ****
     *************************/
    if (Validate != 0) {
        // Get first td
        Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
        while (Td) {
            // Skip unprocessed td's
            if (!(Td->Flags & UHCI_TD_ACTIVE)) {
                // Extract the error code
                int ErrorCode = UhciConditionCodeToIndex(UHCI_TD_STATUS(Td->Flags));
                Transfer->TransactionsExecuted++;

                // Calculate length transferred 
                // Take into consideration the N-1 
                if (Td->Buffer != 0) {
                    int BytesTransferred    = UHCI_TD_ACTUALLENGTH(Td->Flags) + 1;
                    int BytesRequested      = UHCI_TD_GET_LEN(Td->Header) + 1;
                    if (BytesTransferred < BytesRequested) {
                        ShortTransfer = 1;
                    }
                    for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
                        if (Transfer->Transfer.Transactions[i].Length > Transfer->BytesTransferred[i]) {
                            Transfer->BytesTransferred[i] += BytesTransferred;
                            break;
                        }
                    }
                }

                // Trace
                TRACE("Flags 0x%x, Header 0x%x, Buffer 0x%x, Td Condition Code %u", 
                    Td->Flags, Td->Header, Td->Buffer, ErrorCode);

                // Now validate the code
                if (ErrorCode != 0) {
                    WARNING("Transfer non-success: %i (Flags 0x%x)", ErrorCode, Td->Flags);
                    Transfer->Status = UhciGetStatusCode(ErrorCode);
                }
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
        && Transfer->TransactionsExecuted != Transfer->TransactionsTotal) {
        BytesLeft = 1;
    }

    // We don't allocate the queue head before the transfer
    // is done, we might not be done yet
    if (BytesLeft == 1 && ShortTransfer == 0) {
        // Queue up more data
        if (UhciTransferFill(Controller, Transfer) == OsSuccess) {
            UhciTransactionDispatch(Controller, Transfer);
        }
        return OsError;
    }
    else {
        // Now unallocate the Qh by zeroing that
        memset((void*)Qh, 0, sizeof(UhciQueueHead_t));
        Transfer->EndpointDescriptor = NULL;

        // Should we notify the user here?...
        if (Transfer->Requester != UUID_INVALID
            && (Transfer->Transfer.Type == ControlTransfer
                || Transfer->Transfer.Type == BulkTransfer)) {
            Result.Id = Transfer->Id;
            Result.BytesTransferred = Transfer->BytesTransferred[0];
            Result.BytesTransferred += Transfer->BytesTransferred[1];
            Result.BytesTransferred += Transfer->BytesTransferred[2];
            Result.Status = Transfer->Status;
            PipeSend(Transfer->Requester, Transfer->ResponsePort, 
                (void*)&Result, sizeof(UsbTransferResult_t));
        }
        free(Transfer);
        return OsSuccess;
    }
}

/* UsbQueueTransferGeneric 
 * Queues a new transfer for the given driver
 * and pipe. They must exist. The function does not block*/
UsbTransferStatus_t
UsbQueueTransferGeneric(
    _InOut_ UsbManagerTransfer_t *Transfer)
{
    // Variables
    UhciController_t *Controller        = NULL;
    UhciQueueHead_t *Qh                 = NULL;
    DataKey_t Key;
    
    // Debug
    TRACE("UsbQueueTransferGeneric()");

    // Get Controller
    Controller = (UhciController_t*)UsbManagerGetController(Transfer->DeviceId);

    // Initialize
    if (UhciTransactionInitialize(Controller, &Transfer->Transfer, &Qh) != OsSuccess) {
        return TransferNoBandwidth;
    }

    // Update the stored information
    Transfer->EndpointDescriptor = Qh;
    Transfer->Status = TransferNotProcessed;

    // If it's a control transfer set initial toggle 0
	if (Transfer->Transfer.Type == ControlTransfer) {
		UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, 0);
	}

    // Store transaction in queue
    Key.Value = 0;
    CollectionAppend(Controller->QueueControl.TransactionList, 
        CollectionCreateNode(Key, Transfer));

    // Count the transaction count
    UhciTransactionCount(Controller, Transfer, &Transfer->TransactionsTotal);

    // Fill transfer
    if (UhciTransferFill(Controller, Transfer) != OsSuccess) {
        return TransferQueued;
    }

    // Send the transaction and wait for completion
    //UhciQueueDebug(Controller, Qh);
    return UhciTransactionDispatch(Controller, Transfer);
}

/* UsbDequeueTransferGeneric 
 * Removes a queued transfer from the controller's framelist */
UsbTransferStatus_t
UsbDequeueTransferGeneric(
    _In_ UsbManagerTransfer_t *Transfer)
{
    // Variables
    UhciQueueHead_t *Qh = 
        (UhciQueueHead_t*)Transfer->EndpointDescriptor;
    UhciController_t *Controller = NULL;

    // Get Controller
    Controller = (UhciController_t*)UsbManagerGetController(Transfer->DeviceId);

    // Mark for unscheduling on next interrupt/check
    Qh->Flags |= UHCI_QH_UNSCHEDULE;

    // Done, rest of cleanup happens in Finalize
    return TransferFinished;
}
