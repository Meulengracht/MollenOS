/* MollenOS
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
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
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
 * and preparing it for usage. This is used to initialize transfers of type
 * Control, Bulk and Interrupt. */
static OsStatus_t
EhciTransactionInitialize(
    _In_  EhciController_t* Controller,
    _In_  UsbTransfer_t*    Transfer,
    _In_  size_t            Pipe,
    _Out_ void**            DescriptorOut)
{
    // Variables
    EhciQueueHead_t *Qh = EhciQhAllocate(Controller);
    size_t Address      = 0;
    size_t Endpoint     = 0;

    // Extract address and endpoint
    Address             = HIWORD(Pipe);
    Endpoint            = LOWORD(Pipe);
    if (Qh != NULL) {
        *DescriptorOut  = (void*)Qh;
        if (EhciQhInitialize(Controller, Transfer, Qh, Address, Endpoint) != OsSuccess) {
            return OsError;
        }
    }
    else {
        *DescriptorOut  = USB_OUT_OF_RESOURCES;
        return OsError;
    }
    return OsSuccess;
}

/* EhciTransactionCount
 * Returns the number of transactions neccessary for the transfer. */
static OsStatus_t
EhciTransactionCount(
    _In_  EhciController_t*     Controller,
    _In_  UsbManagerTransfer_t* Transfer,
    _Out_ int*                  TransactionsTotal)
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
            if (Transfer->Transfer.Transactions[i].ZeroLength == 1 || AddZeroLength == 1) {
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
static OsStatus_t
EhciTransferFill(
    _In_ EhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
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
    Address     = HIWORD(Transfer->Pipe);
    Endpoint    = LOWORD(Transfer->Pipe);

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
                Td          = EhciTdIo(Controller, &Transfer->Transfer, &Transfer->Transfer.Transactions[i], Toggle);
                ByteStep    = (Td->Length & EHCI_TD_LENGTHMASK);
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

    // If we ran out of resources it can be pretty serious
    // Add a null-transaction (Out, Zero)
    if (OutOfResources == 1) {
        // If we allocated zero we have to unallocate zero and try again later
        if (InitialTd == NULL) {
            return OsError;
        }
    }
    // Set last td to generate a interrupt (not null)
    PreviousTd->Token           |= EHCI_TD_IOC;
    PreviousTd->OriginalToken   |= EHCI_TD_IOC;

    // End of <transfer>?
    if (InitialTd != NULL) {
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

/* EchiCleanupTransferGeneric
 * Cleans up transation and the transfer resources. This can only
 * be called after the hardware reference has been dropped. */
OsStatus_t
EchiCleanupTransferGeneric(
    _In_  EhciController_t*     Controller,
    _In_  UsbManagerTransfer_t* Transfer)
{
    // Variables
    EhciQueueHead_t *Qh             =(EhciQueueHead_t*)Transfer->EndpointDescriptor;
    EhciTransferDescriptor_t *Td    = NULL;
    CollectionItem_t *Node          = NULL;
    int BytesLeft                   = 0;

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
            break;
        }
    }

    // Free bandwidth in case of interrupt transfer
    if (Transfer->Transfer.Type == InterruptTransfer) {
        UsbSchedulerReleaseBandwidth(Controller->Scheduler, Qh->Interval, Qh->Bandwidth, Qh->sFrame, Qh->sMask);
    }

    // Is the transfer done?
    if ((Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer)
        && Transfer->Status == TransferFinished
        && Transfer->TransactionsExecuted != Transfer->TransactionsTotal
        && !(Qh->HcdFlags & EHCI_HCDFLAGS_SHORTTRANSFER)) {
        BytesLeft = 1;
    }

    // We don't allocate the queue head before the transfer
    // is done, we might not be done yet
    if (BytesLeft == 1) {
        // Queue up more data
        if (EhciTransferFill(Controller, Transfer) == OsSuccess) {
            EhciTransactionDispatch(Controller, Transfer);
        }
        return OsError;
    }
    else {
        Controller->QueueControl.AsyncTransactions--;
    }

    // Now run through transactions and check if any are ready to run
    memset((void*)Qh, 0, sizeof(EhciQueueHead_t));
    Transfer->EndpointDescriptor = NULL;
    _foreach(Node, Controller->Base.TransactionList) {
        UsbManagerTransfer_t *NextTransfer = (UsbManagerTransfer_t*)Node->Data;
        if (NextTransfer->Status == TransferNotProcessed) {
            if (EhciTransferFill(Controller, NextTransfer) == OsSuccess) {
                EhciTransactionDispatch(Controller, NextTransfer);
            }
        }
    }
    return OsSuccess;
}

/* EhciTransactionFinalizeGeneric
 * Cleans up the transfer, deallocates resources and validates the td's */
OsStatus_t
EhciTransactionFinalizeGeneric(
    _In_ EhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
{
    // Variables
    EhciQueueHead_t *Qh             = (EhciQueueHead_t*)Transfer->EndpointDescriptor;
    int BytesLeft                   = 0;
    UsbTransferResult_t Result;
    
    // Debug
    TRACE("EhciTransactionFinalizeGeneric()");

    // Unlink the endpoint descriptor 
    SpinlockAcquire(&Controller->Base.Lock);
    EhciSetPrefetching(Controller, Transfer->Transfer.Type, 0);
    if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
        EhciUnlinkGeneric(Controller, Qh);
    }
    else if (Transfer->Transfer.Type == InterruptTransfer) {
        EhciUnlinkPeriodic(Controller, (uintptr_t)Qh, Qh->Interval, Qh->sFrame);
    }
    EhciSetPrefetching(Controller, Transfer->Transfer.Type, 1);
    SpinlockRelease(&Controller->Base.Lock);
    
    // Is the transfer done?
    if ((Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer)
        && Transfer->Status == TransferFinished
        && Transfer->TransactionsExecuted != Transfer->TransactionsTotal
        && !(Qh->HcdFlags & EHCI_HCDFLAGS_SHORTTRANSFER)) {
        BytesLeft = 1;
    }

    // Should we notify the user here?
    if (BytesLeft != 1) {
        if (Transfer->Requester != UUID_INVALID && 
            (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer)) {
            Result.Id                   = Transfer->Id;
            Result.BytesTransferred     = Transfer->BytesTransferred[0];
            Result.BytesTransferred     += Transfer->BytesTransferred[1];
            Result.BytesTransferred     += Transfer->BytesTransferred[2];
            Result.Status               = Transfer->Status;
            PipeSend(Transfer->Requester, Transfer->ResponsePort, (void*)&Result, sizeof(UsbTransferResult_t));
        }
    }

    // Mark for unscheduling, this will then be handled at the next door-bell
    Qh->HcdFlags |= EHCI_HCDFLAGS_UNSCHEDULE;
    EhciRingDoorbell(Controller);
    return OsSuccess;
}

/* HciQueueTransferGeneric 
 * Queues a new asynchronous/interrupt transfer for the given driver and pipe. 
 * The function does not block. */
UsbTransferStatus_t
HciQueueTransferGeneric(
    _In_ UsbManagerTransfer_t*      Transfer)
{
    // Variables
    EhciController_t *Controller    = NULL;
    DataKey_t Key;

    // Get Controller
    Controller = (EhciController_t *)UsbManagerGetController(Transfer->DeviceId);

    // Update the stored information
    Transfer->Status                = TransferNotProcessed;

    // Initialize
    if (EhciTransactionInitialize(Controller, &Transfer->Transfer, 
        Transfer->Pipe, &Transfer->EndpointDescriptor) != OsSuccess) {
        return TransferNoBandwidth;
    }

    // If it's a control transfer set initial toggle 0
    if (Transfer->Transfer.Type == ControlTransfer) {
        UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, 0);
    }

    // Store transaction in queue
    Key.Value = 0;
    CollectionAppend(Controller->Base.TransactionList, CollectionCreateNode(Key, Transfer));

    // Count the transaction count
    EhciTransactionCount(Controller, Transfer, &Transfer->TransactionsTotal);
    if (EhciTransferFill(Controller, Transfer) != OsSuccess) {
        return TransferQueued;
    }
    return EhciTransactionDispatch(Controller, Transfer);
}
