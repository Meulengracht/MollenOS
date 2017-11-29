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
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 * - Isochronous Transport
 * - Transaction Translator Support
 */
#define __TRACE

/* Includes
 * - System */
#include <os/utils.h>
#include "ehci.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* EhciTransactionInitialize
 * Initializes a transaction by allocating a new endpoint-descriptor
 * and preparing it for usage */
OsStatus_t
EhciTransactionInitialize(
    _In_ EhciController_t *Controller,
    _In_ UsbTransfer_t *Transfer,
    _In_ size_t Pipe,
    _Out_ EhciQueueHead_t **QhOut)
{
    // Variables
    EhciQueueHead_t *Qh = NULL;
    size_t Address, Endpoint;

    // Extract address and endpoint
    Address = HIWORD(Pipe);
    Endpoint = LOWORD(Pipe);

    // We handle Isochronous transfers a bit different
    if (Transfer->Type != IsochronousTransfer) {
        *QhOut = Qh = EhciQhAllocate(Controller);

        // Calculate the bus-time
        if (Transfer->Type == InterruptTransfer) {
            if (EhciQhInitialize(Controller, Qh, Transfer->Speed, 
                Transfer->Endpoint.Direction,
                Transfer->Type, Transfer->Endpoint.Interval,
                Transfer->Endpoint.MaxPacketSize, Transfer->Transactions[0].Length) != OsSuccess) {
                return OsError;
            }
        }

        // Initialize the QH
        Qh->Flags = EHCI_QH_DEVADDR(Address);
        Qh->Flags |= EHCI_QH_EPADDR(Endpoint);
        Qh->Flags |= EHCI_QH_DTC;

        // The thing with maxlength is
        // that it needs to be MIN(TransferLength, MPS)
        Qh->Flags |= EHCI_QH_MAXLENGTH(Transfer->Endpoint.MaxPacketSize);

        // Now, set additionals depending on speed
        if (Transfer->Speed == LowSpeed 
            || Transfer->Speed == FullSpeed) {
            if (Transfer->Type == ControlTransfer) {
                Qh->Flags |= EHCI_QH_CONTROLEP;
            }

            // On low-speed, set this bit
            if (Transfer->Speed == LowSpeed) {
                Qh->Flags |= EHCI_QH_LOWSPEED;
            }

            // Set nak-throttle to 0
            Qh->Flags |= EHCI_QH_RL(0);

            // We need to fill the TT's hub-addr
            // and port-addr (@todo)

            // Last thing to do in full/low is to set multiplier to 1
            Qh->State = EHCI_QH_MULTIPLIER(1);
        }
        else {
            // High speed device, no transaction translator
            Qh->Flags |= EHCI_QH_HIGHSPEED;

            // Set nak-throttle to 4 if control or bulk
            if (Transfer->Type == ControlTransfer 
                || Transfer->Type == BulkTransfer) {
                Qh->Flags |= EHCI_QH_RL(4);
            }
            else {
                Qh->Flags |= EHCI_QH_RL(0);
            }

            // If the endpoint is interrupt use the bandwidth specifier
            // otherwise set multiplier to 1
            if (Transfer->Type == InterruptTransfer) {
                Qh->State = EHCI_QH_MULTIPLIER(Transfer->Endpoint.Bandwidth);
            }
            else {
                Qh->State = EHCI_QH_MULTIPLIER(1);
            }
        }
    }
    else {
        // Isochronous transfers (@todo)
    }

    // Done
    return OsSuccess;
}

/* EhciTransactionCount
 * Returns the number of transactions neccessary for the transfer. */
OsStatus_t
EhciTransactionCount(
    _In_ EhciController_t       *Controller,
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
                ByteStep    = EHCI_TD_LENGTH(BytesToTransfer);
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

/* EhciTransferFill 
 * Fills the transfer with as many transfer-descriptors as possible/needed. */
OsStatus_t
EhciTransferFill(
    _In_ EhciController_t           *Controller,
    _InOut_ UsbManagerTransfer_t    *Transfer)
{
    // Variables
    EhciQueueHead_t *Qh                     = (EhciQueueHead_t*)Transfer->EndpointDescriptor;
    EhciTransferDescriptor_t *InitialTd     = NULL;
    EhciTransferDescriptor_t *PreviousTd    = NULL;
    EhciTransferDescriptor_t *Td            = NULL;
    size_t Address, Endpoint;
    int OutOfResources                      = 0;
    int i;

    // Debug
    TRACE("EhciTransferFill()");

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
                Td          = EhciTdSetup(Controller, &Transfer->Transfer.Transactions[i]);
                ByteStep    = BytesToTransfer;
            }
            else {
                Td          = EhciTdIo(Controller, &Transfer->Transfer,
                    &Transfer->Transfer.Transactions[i], Toggle);
                ByteStep    = (Td->Length & EHCI_TD_LENGTHMASK);
            }

            // If we didn't allocate a td, we ran out of 
            // resources, and have to wait for more. Queue up what we have
            if (Td == NULL) {
                WARNING("Out of resources, queued up %u/%u bytes", 
                    ByteOffset, Transfer->Transfer.Transactions[i].Length);
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
                    PreviousTd->LinkIndex   = Td->Index;
                    PreviousTd->Link        = EHCI_POOL_TDINDEX(Controller, PreviousTd->LinkIndex);
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
        // Set last td to generate a interrupt (not null)
        PreviousTd->Token           |= EHCI_TD_IOC;
        PreviousTd->OriginalToken   |= EHCI_TD_IOC;

        // Finalize the endpoint-descriptor
        Qh->ChildIndex              = InitialTd->Index;
        Qh->CurrentTD               = EHCI_POOL_TDINDEX(Controller, InitialTd->Index);
        return OsSuccess;
    }
    else {
        // Queue up for later
        return OsError;
    }
}

/* EhciTransactionDispatch
 * Queues the transfer up in the controller hardware, after finalizing the
 * transactions and preparing them. */
UsbTransferStatus_t
EhciTransactionDispatch(
    _In_ EhciController_t *Controller,
    _In_ UsbManagerTransfer_t *Transfer)
{
    // Variables
    EhciTransferDescriptor_t *Td = NULL;
    EhciQueueHead_t *Qh = NULL;
    uintptr_t QhAddress;

    /*************************
	 ****** SETUP PHASE ******
	 *************************/
    Qh = (EhciQueueHead_t *)Transfer->EndpointDescriptor;
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];

    // Lookup physical
    QhAddress = EHCI_POOL_QHINDEX(Controller, Qh->Index);

    // Handle the initialization a bit differently for isoc
    if (Transfer->Transfer.Type != IsochronousTransfer) {
        Transfer->Status = TransferNotProcessed;

        // Initialize overlay
        memset(&Qh->Overlay, 0, sizeof(EhciQueueHeadOverlay_t));
        Qh->Overlay.NextTD = EHCI_POOL_TDINDEX(Controller, Td->Index);
        Qh->Overlay.NextAlternativeTD = EHCI_LINK_END;

        // Debug
        TRACE("Qh Address 0x%x, Flags 0x%x, State 0x%x, Current 0x%x, Next 0x%x",
              QhAddress, Qh->Flags, Qh->State, Qh->CurrentTD, Qh->Overlay.NextTD);
    }
    else {
        // Initialize isochronous transfer
        // @todo
    }

    // Trace
    TRACE("UHCI: QH at 0x%x, FirstTd 0x%x, NextQh 0x%x",
          QhAddress, Qh->CurrentTD, Qh->LinkPointer);
    TRACE("UHCI: Bandwidth %u, StartFrame %u, Flags 0x%x",
          Qh->Bandwidth, Qh->sFrame, Qh->Flags);

    /*************************
	 **** LINKING PHASE ******
	 *************************/

    // Acquire the spinlock for atomic queue access
    SpinlockAcquire(&Controller->Base.Lock);

    // Bulk and control are asynchronous transfers
    if (Transfer->Transfer.Type == ControlTransfer 
        || Transfer->Transfer.Type == BulkTransfer) {
        // Transfer existing links
        Qh->LinkPointer = Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkPointer;
        Qh->LinkIndex = Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkIndex;
        MemoryBarrier();

        // Insert at the start of queue
        Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkIndex = Qh->Index;
        Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkPointer = QhAddress | EHCI_LINK_QH;

        // Enable the asynchronous scheduler
        Controller->QueueControl.AsyncTransactions++;
        EhciEnableAsyncScheduler(Controller);
    }
    else {
        // Isochronous or interrupt, but handle each differently
        if (Transfer->Transfer.Type == InterruptTransfer) {
            size_t StartFrame = 0, FrameMask = 0;
            size_t EpBandwidth = Transfer->Transfer.Endpoint.Bandwidth;

            // If we use completion masks
            // we'll need another transfer for start
            if (Transfer->Transfer.Speed != HighSpeed) {
                EpBandwidth++;
            }

            // Allocate the needed bandwidth
            if (UsbSchedulerReserveBandwidth(Controller->Scheduler,
                                             Qh->Interval, Qh->Bandwidth, EpBandwidth,
                                             &Qh->sFrame, &Qh->sMask) != OsSuccess) {
                ERROR("EHCI::Failed to allocate bandwidth for qh");
                for (;;);
            }

            // Save scheduling information for cleanup
            Qh->sFrame = StartFrame;
            Qh->sMask = FrameMask;

            // Calculate both the frame start and completion mask
            Qh->FrameStartMask = (uint8_t)FirstSetBit(FrameMask);
            if (Transfer->Transfer.Speed != HighSpeed) {
                Qh->FrameCompletionMask = (uint8_t)(FrameMask & 0xFF);
                Qh->FrameCompletionMask &= ~(1 << Qh->FrameStartMask);
            }
            else {
                Qh->FrameCompletionMask = 0;
            }

            // Link the periodic queuehead
            EhciLinkPeriodicQh(Controller, Qh);
        }
        else {
            ERROR("EHCI::Scheduling Isochronous");
            for (;;);
        }
    }

    // All queue operations are now done
    SpinlockRelease(&Controller->Base.Lock);

// Manually inspect
#ifdef __DEBUG
    ThreadSleep(5000);
    TRACE("Qh Address 0x%x, Flags 0x%x, State 0x%x, Current 0x%x, Next 0x%x\n",
                   QhAddress, Qh->Flags, Qh->State, Qh->CurrentTD, Qh->Overlay.NextTD);
#endif

    // Done
    return TransferQueued;
}

/* EhciTransactionFinalize
 * Cleans up the transfer, deallocates resources and validates the td's */
OsStatus_t
EhciTransactionFinalize(
    _In_ EhciController_t *Controller,
    _In_ UsbManagerTransfer_t *Transfer,
    _In_ int Validate)
{
    // Variables
    EhciQueueHead_t *Qh             =(EhciQueueHead_t*)Transfer->EndpointDescriptor;
    EhciTransferDescriptor_t *Td    = NULL;
    int ShortTransfer               = 0;
    int BytesLeft                   = 0;
    int i;
    UsbTransferResult_t Result;
    
    // Debug
    TRACE("EhciTransactionFinalize()");

    // Set status to finished initially
    Transfer->Status = TransferFinished;

    /*************************
     *** VALIDATION PHASE ****
     *************************/
    if (Validate != 0) {
        // Get first td
        Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
        while (Td) {
            int ErrorCode = EhciConditionCodeToIndex(Transfer->Transfer.Speed == HighSpeed ? (Td->Status & 0xFC) : Td->Status);
            Transfer->TransactionsExecuted++;

            // Calculate the number of bytes transfered
            if ((Td->OriginalLength & EHCI_TD_LENGTHMASK) != 0) {
                int BytesTransferred    = (Td->OriginalLength - Td->Length) & EHCI_TD_LENGTHMASK;
                int BytesRequested      = Td->OriginalLength & EHCI_TD_LENGTHMASK;
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

            // Debug
            TRACE("Td (Id %u) Token 0x%x, Status 0x%x, Length 0x%x, Buffer 0x%x, Link 0x%x\n",
                Td->Index, Td->Token, Td->Status, Td->Length, Td->Buffers[0], Td->Link);

            // Now validate the code
            if (ErrorCode != 0) {
                WARNING("Transfer non-success: %i (Flags 0x%x)", ErrorCode, Td->Status);
                Transfer->Status = EhciGetStatusCode(ErrorCode);
            }

            // Switch to next transfer descriptor
            if (Td->LinkIndex != EHCI_NO_INDEX) {
                Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
            }
            else {
                Td = NULL;
                break;
            }
        }
    }

#ifdef __DEBUG
    for (;;);
#endif

    // Unlink the endpoint descriptor
    if (Transfer->Transfer.Type == ControlTransfer 
        || Transfer->Transfer.Type == BulkTransfer) {
        // Mark Qh for unscheduling, this will then be handled
        // at the next door-bell
        Qh->HcdFlags |= EHCI_QH_UNSCHEDULE;
        EhciRingDoorbell(Controller);
    }
    else {
        // Unlinking periodics is an atomic operation
        SpinlockAcquire(&Controller->Base.Lock);
        EhciUnlinkPeriodic(Controller, (uintptr_t)Qh, 
                           Qh->Interval, Qh->sFrame);
        UsbSchedulerReleaseBandwidth(Controller->Scheduler,
                                     Qh->Interval, Qh->Bandwidth, 
                                     Qh->sFrame, Qh->sMask);
        SpinlockRelease(&Controller->Base.Lock);
    }
    
    // Free all the td's while we hopefully get unscheduled
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    while (Td) {
        int LinkIndex   = Td->LinkIndex;
        int Index       = Td->Index;

        // Reset structure but store index
        memset((void *)Td, 0, sizeof(EhciTransferDescriptor_t));
        Td->Index       = Index;

        // Switch to next transfer descriptor
        if (LinkIndex != EHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[LinkIndex];
        }
        else {
            Td = NULL;
            break;
        }
    }

    // Is the transfer done?
    if ((Transfer->Transfer.Type == ControlTransfer
        || Transfer->Transfer.Type == BulkTransfer)
        && Transfer->Status == TransferFinished
        && Transfer->TransactionsExecuted != Transfer->TransactionsTotal) {
        WARNING("%u/%u transactions executed", 
            Transfer->TransactionsExecuted, Transfer->TransactionsTotal);
        BytesLeft = 1;
    }

    // We don't allocate the queue head before the transfer
    // is done, we might not be done yet
    if (BytesLeft == 1 && ShortTransfer == 0) {
        // Queue up more data
        if (EhciTransferFill(Controller, Transfer) == OsSuccess) {
            EhciTransactionDispatch(Controller, Transfer);
        }
        return OsError;
    }
    else {
        // Now unallocate the Qh by zeroing that
        memset((void*)Qh, 0, sizeof(EhciQueueHead_t));
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
    EhciQueueHead_t *Qh             = NULL;
    EhciController_t *Controller    = NULL;
    DataKey_t Key;

    // Get Controller
    Controller = (EhciController_t *)UsbManagerGetController(Transfer->DeviceId);

    // Initialize
    if (EhciTransactionInitialize(Controller, &Transfer->Transfer, 
        Transfer->Pipe, &Qh) != OsSuccess) {
        return TransferNoBandwidth;
    }

    // Update the stored information
    Transfer->EndpointDescriptor    = Qh;
    Transfer->Status                = TransferNotProcessed;

    // If it's a control transfer set initial toggle 0
	if (Transfer->Transfer.Type == ControlTransfer) {
		UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, 0);
	}

    // Store transaction in queue
    Key.Value = 0;
    CollectionAppend(Controller->QueueControl.TransactionList, 
        CollectionCreateNode(Key, Transfer));

    // Count the transaction count
    EhciTransactionCount(Controller, Transfer, &Transfer->TransactionsTotal);

    // Fill transfer
    if (EhciTransferFill(Controller, Transfer) != OsSuccess) {
        return TransferQueued;
    }

    // Send the transaction and wait for completion
    return EhciTransactionDispatch(Controller, Transfer);
}

/* UsbDequeueTransferGeneric 
  * Removes a queued transfer from the controller's framelist */
UsbTransferStatus_t
UsbDequeueTransferGeneric(
    _In_ UsbManagerTransfer_t *Transfer)
{
    // Variables
    EhciQueueHead_t *Qh             = (EhciQueueHead_t *)Transfer->EndpointDescriptor;
    EhciController_t *Controller    = NULL;

    // Get Controller
    Controller = (EhciController_t *)UsbManagerGetController(Transfer->DeviceId);
    if (Controller == NULL) {
        return TransferInvalid;
    }

    // Mark for unscheduling
    Qh->HcdFlags |= EHCI_QH_UNSCHEDULE;
    EhciRingDoorbell(Controller);
    return TransferFinished;
}
