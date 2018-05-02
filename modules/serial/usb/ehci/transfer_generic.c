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

/* HciQueueTransferGeneric 
 * Queues a new asynchronous/interrupt transfer for the given driver and pipe. 
 * The function does not block. */
UsbTransferStatus_t
HciQueueTransferGeneric(
    _In_ UsbManagerTransfer_t*  Transfer)
{
    // Variables
    EhciQueueHead_t *EndpointDescriptor     = NULL;
    EhciController_t *Controller            = NULL;
    size_t Address, Endpoint;
    DataKey_t Key;

    // Get Controller
    Controller          = (EhciController_t*)UsbManagerGetController(Transfer->DeviceId);
    Transfer->Status    = TransferNotProcessed;

    // Extract address and endpoint
    Address     = HIWORD(Transfer->Pipe);
    Endpoint    = LOWORD(Transfer->Pipe);

    // Step 1 - Allocate queue head
    if (Transfer->EndpointDescriptor == NULL) {
        if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, 
            EHCI_QH_POOL, (uint8_t**)&EndpointDescriptor) != OsSuccess) {
            return TransferQueued;
        }
        assert(EndpointDescriptor != NULL);
        Transfer->EndpointDescriptor = EndpointDescriptor;

        // Store and initialize the qh
        if (OhciQhInitialize(Controller, Transfer, Address, Endpoint) != OsSuccess) {
            // No bandwidth, serious.
            UsbSchedulerFreeElement(Controller->Base.Scheduler, (uint8_t*)EndpointDescriptor);
            return TransferNoBandwidth;
        }
    }

    // Store transaction in queue if it's not there already
    Key.Value = (int)Transfer->Id;
    if (CollectionGetDataByKey(Controller->Base.TransactionList, Key, 0) == NULL) {
        CollectionAppend(Controller->Base.TransactionList, CollectionCreateNode(Key, Transfer));
        EhciTransactionCount(Controller, Transfer, &Transfer->TransactionsTotal);
    }

    // If it fails to queue up => restore toggle
    if (EhciTransferFill(Controller, Transfer) != OsSuccess) {
        return TransferQueued;
    }
    return EhciTransactionDispatch(Controller, Transfer);
}
