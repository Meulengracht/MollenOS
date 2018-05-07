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
    OhciIsocTransferDescriptor_t *PreviousTd    = NULL;
    OhciIsocTransferDescriptor_t *ZeroTd        = NULL;
    OhciIsocTransferDescriptor_t *Td            = NULL;
    UsbTransactionType_t Type                   = Transfer->Transfer.Transactions[0].Type;
    uintptr_t BufferIterator                    = Transfer->Transfer.Transactions[0].BufferAddress;
    size_t BytesToTransfer                      = Transfer->Transfer.Transactions[0].Length;
    OhciQueueHead_t *Qh                         = (OhciQueueHead_t*)Transfer->EndpointDescriptor;

    // Debug
    TRACE("OhciTransferFill()");

    // Start out by retrieving the zero td
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_iTD_POOL,
        OHCI_iTD_NULL, (uint8_t**)&ZeroTd, NULL);

    while (BytesToTransfer) {
        // Calculate how many bytes this td can transfer for us
        // it can at max span 2 pages = 8K. 8x1024. BUT only if the page
        // is starting at 0.
        size_t BytesStep    = 0x2000 - (BufferIterator & 0xFFF); // Maximum
        BytesStep           = MIN(BytesStep, (8 * Transfer->Transfer.Endpoint.MaxPacketSize)); // Adjust
        BytesStep           = MIN(BytesStep, BytesToTransfer); // Adjust again

        if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, OHCI_TD_POOL, (uint8_t**)&Td) == OsSuccess) {
            OhciTdIsochronous(Td, Transfer->Transfer.Endpoint.MaxPacketSize, 
                (Type == InTransaction ? OHCI_TD_IN : OHCI_TD_OUT), BufferIterator, BytesStep);
        }

        // If we didn't allocate a td, we ran out of 
        // resources, and have to wait for more. Queue up what we have
        if (Td == NULL) {
            TRACE(" > Failed to allocate descriptor");
            break;
        }
        else {
            UsbSchedulerChainElement(Controller->Base.Scheduler, 
                (uint8_t*)Qh, (uint8_t*)Td, USB_ELEMENT_NO_INDEX, USB_CHAIN_DEPTH);
            PreviousTd = Td;
        }

        // Update iterators
        BytesToTransfer -= BytesStep;
        BufferIterator  += BytesStep;
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

    // Step 1 - Allocate queue head
    if (Transfer->EndpointDescriptor == NULL) {
        if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, 
            OHCI_QH_POOL, (uint8_t**)&EndpointDescriptor) != OsSuccess) {
            return TransferQueued;
        }
        assert(EndpointDescriptor != NULL);
        Transfer->EndpointDescriptor = EndpointDescriptor;

        // Store and initialize the qh
        if (OhciQhInitialize(Controller, Transfer, 
            Transfer->Transfer.Address.DeviceAddress, 
            Transfer->Transfer.Address.EndpointAddress) != OsSuccess) {
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
