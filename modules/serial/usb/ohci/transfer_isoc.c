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
    _In_ OhciController_t*      Controller,
    _In_ OhciQueueHead_t*       Qh)
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
    uintptr_t BufferIterator    = Transfer->Transfer.Transactions[0].BufferAddress;
    size_t BytesToTransfer      = Transfer->Transfer.Transactions[0].Length;
    int Count                   = 0;

    while (BytesToTransfer) {
        // Calculate how many bytes this td can transfer for us
        // it can at max span 2 pages = 8K. 8x1023. BUT only if the page
        // is starting at 0.
        size_t BytesStep    = 0x2000 - (BufferIterator & 0xFFF); // Maximum
        BytesStep           = MIN(BytesStep, (8 * Transfer->Transfer.Endpoint.MaxPacketSize)); // Adjust
        BytesStep           = MIN(BytesStep, BytesToTransfer); // Adjust again
        Count++;

        // Update iterators
        BytesToTransfer -= BytesStep;
        BufferIterator  += BytesStep;
    }

    *TransactionsTotal = Count;
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
    UsbTransactionType_t Type               = Transfer->Transfer.Transactions[0].Type;
    uintptr_t BufferIterator                = Transfer->Transfer.Transactions[0].BufferAddress;
    size_t BytesToTransfer                  = Transfer->Transfer.Transactions[0].Length;
    size_t Address, Endpoint;
    int OutOfResources                      = 0;

    // Debug
    TRACE("OhciTransferFill()");

    // Extract address and endpoint
    Address     = HIWORD(Transfer->Pipe);
    Endpoint    = LOWORD(Transfer->Pipe);
    while (BytesToTransfer) {
        // Calculate how many bytes this td can transfer for us
        // it can at max span 2 pages = 8K. 8x1023. BUT only if the page
        // is starting at 0.
        size_t BytesStep    = 0x2000 - (BufferIterator & 0xFFF); // Maximum
        BytesStep           = MIN(BytesStep, (8 * Transfer->Transfer.Endpoint.MaxPacketSize)); // Adjust
        BytesStep           = MIN(BytesStep, BytesToTransfer); // Adjust again

        // Allocate a new transfer descriptor 
        Td = OhciTdIsochronous(Controller, Transfer->Transfer.Endpoint.MaxPacketSize,
            (Type == InTransaction ? OHCI_TD_IN : OHCI_TD_OUT), BufferIterator, BytesStep);
        if (Td == USB_OUT_OF_RESOURCES) {
            OutOfResources = 1;
            break;
        }

        // Store first
        if (InitialTd == NULL) {
            InitialTd               = Td;
            PreviousTd              = Td;
        }
        else {
            // Update physical link
            PreviousTd->LinkIndex   = Td->Index;
            PreviousTd->Link        = OHCI_POOL_TDINDEX(Controller, Td->Index);
            PreviousTd              = Td;
        }

        // Update iterators
        BytesToTransfer -= BytesStep;
        BufferIterator  += BytesStep;
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

    // Queue up for later?
    if (InitialTd == NULL) {
        return OsError;
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

/* HciQueueTransferIsochronous 
 * Queues a new isochronous transfer for the given driver and pipe. 
 * The function does not block. */
UsbTransferStatus_t
HciQueueTransferIsochronous(
    _In_ UsbManagerTransfer_t*      Transfer)
{
    // Variables
    OhciQueueHead_t *EndpointDescriptor     = NULL;
    OhciController_t *Controller            = NULL;
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

    // If it fails to queue up => restore toggle
    if (OhciTransferFill(Controller, Transfer) != OsSuccess) {
        return TransferQueued;
    }
#ifdef __TRACE
    OhciQueueDebug(Controller, EndpointDescriptor);
#endif
    return OhciTransactionDispatch(Controller, Transfer);
}
