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

/* OhciQueueDebug
 * Dumps the QH-settings and all the attached td's */
static void
OhciQueueDebug(
    OhciController_t *Controller,
    OhciQueueHead_t *Qh)
{
    // Variables
    OhciTransferDescriptor_t *Td = NULL;
    uintptr_t PhysicalAddress = 0;

    PhysicalAddress = OHCI_POOL_QHINDEX(Controller, Qh->Index);
    TRACE("QH(0x%x): Flags 0x%x, NextQh 0x%x, FirstChild 0x%x", 
        PhysicalAddress, Qh->Flags, Qh->LinkPointer, Qh->Current);

    // Get first td
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    while (Td != NULL) {
        PhysicalAddress = OHCI_POOL_TDINDEX(Controller, Td->Index);
        TRACE("TD(0x%x): Link 0x%x, Flags 0x%x, Cbp 0x%x, BufferEnd 0x%x", 
            PhysicalAddress, Td->Link, Td->Flags, Td->Cbp, Td->BufferEnd);
        // Go to next td
        if (Td->LinkIndex != OHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
        }
        else {
            Td = NULL;
        }
    }
}

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
    OhciTransferDescriptor_t *InitialTd     = NULL;
    OhciTransferDescriptor_t *PreviousTd    = NULL;
    OhciTransferDescriptor_t *ZeroTd        = NULL;
    OhciTransferDescriptor_t *Td            = NULL;
    size_t Address, Endpoint;
    int OutOfResources                      = 0;
    int i;

    // Debug
    TRACE("OhciTransferFill()");

    // Extract address and endpoint
    Address     = HIWORD(Transfer->Pipe);
    Endpoint    = LOWORD(Transfer->Pipe);

    // Start out by allocating a zero-td
    ZeroTd      = OhciTdIo(Controller, Transfer->Transfer.Type, OHCI_TD_OUT, 
            UsbManagerGetToggle(Transfer->DeviceId, Transfer->Pipe), 0, 0);
    // Queue up for later?
    if (ZeroTd == USB_OUT_OF_RESOURCES) {
        return OsError;
    }

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
                Td          = OhciTdSetup(Controller, &Transfer->Transfer.Transactions[i]);
            }
            else {
                ByteStep    = MIN(BytesToTransfer, Transfer->Transfer.Endpoint.MaxPacketSize);
                Td          = OhciTdIo(Controller, Transfer->Transfer.Type, 
                    (Type == InTransaction ? OHCI_TD_IN : OHCI_TD_OUT), Toggle, 
                    Transfer->Transfer.Transactions[i].BufferAddress + ByteOffset, ByteStep);
            }

            // If we didn't allocate a td, we ran out of 
            // resources, and have to wait for more. Queue up what we have
            if (Td == USB_OUT_OF_RESOURCES) {
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
                    PreviousTd->Link        = OHCI_POOL_TDINDEX(Controller, Td->Index);
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
            // Unallocate, do nothing
            memset(ZeroTd, 0, sizeof(OhciTransferDescriptor_t));
            return OsError;
        }
    }

    PreviousTd->Link            = OHCI_POOL_TDINDEX(Controller, ZeroTd->Index);
    PreviousTd->LinkIndex       = ZeroTd->Index;
    PreviousTd->Flags           &= ~OHCI_TD_IOC_NONE;
    PreviousTd->OriginalFlags   = PreviousTd->Flags;

    // Initialize Qh and queue it up
    OhciQhInitialize(Controller, Transfer->EndpointDescriptor, 
        InitialTd->Index, PreviousTd->Index, Transfer->Transfer.Type, 
        Address, Endpoint, Transfer->Transfer.Endpoint.MaxPacketSize, 
        Transfer->Transfer.Speed);
    return OsSuccess;
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
    int BackupToggle                        = UsbManagerGetToggle(Transfer->DeviceId, Transfer->Pipe);
    DataKey_t Key;

    // Get Controller
    Controller          = (OhciController_t*)UsbManagerGetController(Transfer->DeviceId);
    Transfer->Status    = TransferNotProcessed;
    if (Transfer->EndpointDescriptor == NULL) {
        EndpointDescriptor = OhciTransactionInitialize(Controller, &Transfer->Transfer);
        if (EndpointDescriptor == USB_OUT_OF_RESOURCES) {
            return TransferQueued;
        }
        Transfer->EndpointDescriptor = EndpointDescriptor;
    }

    // Store transaction in queue if it's not there already
    Key.Value = (int)Transfer->Id;
    if (CollectionGetDataByKey(Controller->Base.TransactionList, Key, 0) == NULL) {
        CollectionAppend(Controller->Base.TransactionList, CollectionCreateNode(Key, Transfer));
        OhciTransactionCount(Controller, Transfer, &Transfer->TransactionsTotal);
    }

    // If it's a control transfer set initial toggle 0
    if (Transfer->Transfer.Type == ControlTransfer) {
        UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, 0);
    }

    // If it fails to queue up => restore toggle
    if (OhciTransferFill(Controller, Transfer) != OsSuccess) {
        if (Transfer->Transfer.Type == ControlTransfer) {
            UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, BackupToggle);
        }
        return TransferQueued;
    }
#ifdef __TRACE
    OhciQueueDebug(Controller, EndpointDescriptor);
#endif
    return OhciTransactionDispatch(Controller, Transfer);
}
