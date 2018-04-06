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

/* UhciQueueDebug
 * Dumps the QH-settings and all the attached td's */
static void
UhciQueueDebug(
    _In_ UhciController_t*  Controller,
    _In_ UhciQueueHead_t*   Qh)
{
    // Variables
    UhciTransferDescriptor_t *Td = NULL;
    uintptr_t PhysicalAddress = 0;

    PhysicalAddress = UHCI_POOL_QHINDEX(Controller, Qh->Index);
    TRACE("QH(0x%x): Flags 0x%x, NextQh 0x%x, FirstChild 0x%x", 
        PhysicalAddress, Qh->Flags, Qh->Link, Qh->Child);

    // Get first td
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    while (Td != NULL) {
        PhysicalAddress = UHCI_POOL_TDINDEX(Controller, Td->Index);
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

/* UhciTransactionCount
 * Returns the number of transactions neccessary for the transfer. */
static OsStatus_t
UhciTransactionCount(
    _In_  UhciController_t*     Controller,
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

/* UhciTransferFill 
 * Fills the transfer with as many transfer-descriptors as possible/needed. */
static OsStatus_t
UhciTransferFill(
    _In_ UhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
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
                    PreviousTd->LinkIndex   = Td->Index;
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
        UhciQhInitialize(Controller, Transfer->EndpointDescriptor, InitialTd->Index);
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
    _In_ UsbManagerTransfer_t*      Transfer)
{
    // Variables
    UhciQueueHead_t *EndpointDescriptor     = NULL;
    UhciController_t *Controller            = NULL;
    int BackupToggle                        = UsbManagerGetToggle(Transfer->DeviceId, Transfer->Pipe);
    DataKey_t Key;

    // Get Controller
    Controller          = (UhciController_t*)UsbManagerGetController(Transfer->DeviceId);
    Transfer->Status    = TransferNotProcessed;
    if (Transfer->EndpointDescriptor == NULL) {
        UhciTransactionInitialize(Controller, &Transfer->Transfer, &EndpointDescriptor);
        if (EndpointDescriptor == USB_OUT_OF_RESOURCES) {
            return TransferQueued;
        }
        Transfer->EndpointDescriptor = EndpointDescriptor;
    }

    // Store transaction in queue if it's not there already
    Key.Value = (int)Transfer->Id;
    if (CollectionGetDataByKey(Controller->Base.TransactionList, Key, 0) == NULL) {
        CollectionAppend(Controller->Base.TransactionList, CollectionCreateNode(Key, Transfer));
        UhciTransactionCount(Controller, Transfer, &Transfer->TransactionsTotal);
    }

    // If it's a control transfer set initial toggle 0
    if (Transfer->Transfer.Type == ControlTransfer) {
        UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, 0);
    }

    // If it fails to queue up => restore toggle
    if (UhciTransferFill(Controller, Transfer) != OsSuccess) {
        if (Transfer->Transfer.Type == ControlTransfer) {
            UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, BackupToggle);
        }
        return TransferQueued;
    }
#ifdef __TRACE
    UhciQueueDebug(Controller, EndpointDescriptor);
#endif
    return UhciTransactionDispatch(Controller, Transfer);
}
