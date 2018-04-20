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
 * MollenOS MCore - Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
#define __TRACE

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>
#include "ohci.h"

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* OhciTransactionCount
 * Returns the number of transactions neccessary for the transfer. */
static OsStatus_t
OhciTransactionCount(
    _In_  OhciController_t*     Controller,
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
        while (BytesToTransfer || AddZeroLength == 1 ||
               Transfer->Transfer.Transactions[i].ZeroLength == 1) {
            if (Type == SetupTransaction) {
                ByteStep    = BytesToTransfer;
            }
            else {
                ByteStep    = MIN(BytesToTransfer, Transfer->Transfer.Endpoint.MaxPacketSize);
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

/* OhciTransferFill 
 * Fills the transfer with as many transfer-descriptors as possible/needed. */
static OsStatus_t
OhciTransferFill(
    _In_ OhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
{
    // Variables
    OhciTransferDescriptor_t *PreviousTd    = NULL;
    OhciTransferDescriptor_t *ZeroTd        = NULL;
    OhciTransferDescriptor_t *Td            = NULL;
    OhciQueueHead_t *Qh                     = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
    size_t Address, Endpoint;
    int OutOfResources                      = 0;
    int i;

    // Debug
    TRACE("OhciTransferFill()");

    // Extract address and endpoint
    Address     = HIWORD(Transfer->Pipe);
    Endpoint    = LOWORD(Transfer->Pipe);

    // Start out by retrieving the zero td
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_TD_POOL,
        OHCI_TD_NULL, (uint8_t**)&ZeroTd, NULL);

    // Get next address from which we need to load
    for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
        UsbTransactionType_t Type   = Transfer->Transfer.Transactions[i].Type;
        size_t BytesToTransfer      = Transfer->Transfer.Transactions[i].Length;
        size_t ByteOffset           = 0;
        size_t ByteStep             = 0;
        int PreviousToggle          = -1;
        int Toggle                  = 0;
        TRACE("Transaction(%i, Buffer 0x%x, Length %u, Type %i)", i,
            Transfer->Transfer.Transactions[i].BufferAddress, BytesToTransfer, Type);

        // Adjust offsets
        ByteOffset                  = Transfer->BytesTransferred[i];
        BytesToTransfer            -= Transfer->BytesTransferred[i];
        if (BytesToTransfer == 0 && Transfer->Transfer.Transactions[i].ZeroLength != 1) {
            TRACE(" > Skipping");
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
        TRACE(" > BytesToTransfer(%u)", BytesToTransfer);
        while (BytesToTransfer || Transfer->Transfer.Transactions[i].ZeroLength == 1) {
            Toggle          = UsbManagerGetToggle(Transfer->DeviceId, Transfer->Pipe);
            if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, OHCI_TD_POOL, (uint8_t**)&Td) == OsSuccess) {
                if (Type == SetupTransaction) {
                    TRACE(" > Creating setup packet");
                    Toggle      = 0; // Initial toggle must ALWAYS be 0 for setup
                    ByteStep    = BytesToTransfer;
                    OhciTdSetup(&Transfer->Transfer.Transactions[i], Td);
                }
                else {
                    TRACE(" > Creating io packet");
                    ByteStep    = MIN(BytesToTransfer, Transfer->Transfer.Endpoint.MaxPacketSize);
                    OhciTdIo(Td, Transfer->Transfer.Type, 
                        (Type == InTransaction ? OHCI_TD_IN : OHCI_TD_OUT), Toggle,
                        Transfer->Transfer.Transactions[i].BufferAddress + ByteOffset, ByteStep);
                }
            }

            // If we didn't allocate a td, we ran out of 
            // resources, and have to wait for more. Queue up what we have
            if (Td == NULL) {
                TRACE(" > Failed to allocate descriptor");
                if (PreviousToggle != -1) {
                    UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, PreviousToggle);
                    Transfer->Transfer.Transactions[i].Handshake = 1;
                }
                OutOfResources = 1;
                break;
            }
            else {
                UsbSchedulerChainElement(Controller->Base.Scheduler, 
                    (uint8_t*)Qh, (uint8_t*)Td, USB_ELEMENT_NO_INDEX, USB_CHAIN_DEPTH);
                PreviousTd = Td;

                // Update toggle by flipping
                UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, Toggle ^ 1);

                // Break out on zero lengths
                if (Transfer->Transfer.Transactions[i].ZeroLength == 1) {
                    TRACE(" > Encountered zero-length");
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
    if (PreviousTd != NULL) {
        // We have a transfer
        UsbSchedulerChainElement(Controller->Base.Scheduler, 
            (uint8_t*)Qh, (uint8_t*)ZeroTd, USB_ELEMENT_NO_INDEX, USB_CHAIN_DEPTH);
        
        // Enable ioc
        PreviousTd->Flags           &= ~OHCI_TD_IOC_NONE;
        PreviousTd->OriginalFlags   = PreviousTd->Flags;
        return OsSuccess;
    }
    else {
        return OsError; // Queue up for later
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
    OhciQueueHead_t *EndpointDescriptor     = NULL;
    OhciController_t *Controller            = NULL;
    size_t Address, Endpoint;
    DataKey_t Key;

    // Get Controller
    Controller          = (OhciController_t*)UsbManagerGetController(Transfer->DeviceId);
    Transfer->Status    = TransferNotProcessed;

    // Extract address and endpoint
    Address     = HIWORD(Transfer->Pipe);
    Endpoint    = LOWORD(Transfer->Pipe);

    // Step 1 - Allocate queue head
    if (Transfer->EndpointDescriptor == NULL) {
        if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, 
            OHCI_QH_POOL, (uint8_t**)&EndpointDescriptor) != OsSuccess) {
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
        OhciTransactionCount(Controller, Transfer, &Transfer->TransactionsTotal);
    }

    // If it fails to queue up => restore toggle
    if (OhciTransferFill(Controller, Transfer) != OsSuccess) {
        return TransferQueued;
    }
    return OhciTransactionDispatch(Controller, Transfer);
}
