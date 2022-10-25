/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

#include <ddk/utils.h>
#include "ohci.h"
#include <assert.h>
#include <stdlib.h>

static oserr_t
OhciTransferFill(
    _In_ OhciController_t*     Controller,
    _In_ UsbManagerTransfer_t* Transfer)
{
    OhciIsocTransferDescriptor_t* PreviousTd = NULL;
    OhciIsocTransferDescriptor_t* ZeroTd     = NULL;
    OhciQueueHead_t*              Qh         = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
    
    uint8_t Type            = Transfer->Transfer.Transactions[0].Type;
    size_t  BytesToTransfer = Transfer->Transfer.Transactions[0].Length;
    size_t  MaxBytesPerDescriptor;

    // Debug
    TRACE("OhciTransferFill()");

    // Start out by retrieving the zero td
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_iTD_POOL,
        OHCI_iTD_NULL, (uint8_t**)&ZeroTd, NULL);
    
    // Calculate mpd
    MaxBytesPerDescriptor = Transfer->Transfer.MaxPacketSize * 8;

    while (BytesToTransfer) {
        OhciIsocTransferDescriptor_t* iTd;
        uintptr_t                     AddressPointer;
        size_t                        BytesStep;
        
        // Out of three different limiters we must select the lowest one. Either
        // we must transfer lower bytes because of the requested amount, or the limit
        // of a descriptor, or the limit of the DMA table
        BytesStep = MIN(BytesToTransfer, MaxBytesPerDescriptor);
        BytesStep = MIN(BytesStep, Transfer->Transactions[0].DmaTable.entries[
            Transfer->Transactions[0].SgIndex].length - Transfer->Transactions[0].SgOffset);
        
        AddressPointer = Transfer->Transactions[0].DmaTable.entries[
            Transfer->Transactions[0].SgIndex].address + Transfer->Transactions[0].SgOffset;
        
        if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, OHCI_TD_POOL, (uint8_t**)&iTd) == OS_EOK) {
            OhciTdIsochronous(iTd, Transfer->Transfer.MaxPacketSize, 
                (Type == USB_TRANSACTION_IN ? OHCI_TD_IN : OHCI_TD_OUT), AddressPointer, BytesStep);
        }

        // If we didn't allocate a td, we ran out of 
        // resources, and have to wait for more. Queue up what we have
        if (iTd == NULL) {
            TRACE(" > Failed to allocate descriptor");
            break;
        }
        else {
            UsbSchedulerChainElement(Controller->Base.Scheduler, OHCI_QH_POOL, 
                (uint8_t*)Qh, OHCI_iTD_POOL, (uint8_t*)iTd, USB_ELEMENT_NO_INDEX, USB_CHAIN_DEPTH);
            PreviousTd = iTd;
        }

        // Increase the DmaTable metrics
        Transfer->Transactions[0].SgOffset += BytesStep;
        if (Transfer->Transactions[0].SgOffset == 
                Transfer->Transactions[0].DmaTable.entries[
                    Transfer->Transactions[0].SgIndex].length) {
            Transfer->Transactions[0].SgIndex++;
            Transfer->Transactions[0].SgOffset = 0;
        }
        BytesToTransfer -= BytesStep;
    }

    // If we ran out of resources it can be pretty serious
    if (PreviousTd != NULL) {
        // We have a transfer
        UsbSchedulerChainElement(Controller->Base.Scheduler, OHCI_QH_POOL, 
            (uint8_t*)Qh, OHCI_iTD_POOL, (uint8_t*)ZeroTd, USB_ELEMENT_NO_INDEX, USB_CHAIN_DEPTH);
        
        // Enable ioc
        PreviousTd->Flags         &= ~OHCI_TD_IOC_NONE;
        PreviousTd->OriginalFlags = PreviousTd->Flags;
        return OS_EOK;
    }
    
    // Queue up for later
    return OS_EUNKNOWN;
}

UsbTransferStatus_t
HciQueueTransferIsochronous(
    _In_ UsbManagerTransfer_t* transfer)
{
    OhciQueueHead_t*    endpointDescriptor = NULL;
    OhciController_t*   controller;
    UsbTransferStatus_t status;

    controller = (OhciController_t*) UsbManagerGetController(transfer->DeviceId);
    if (!controller) {
        return TransferInvalid;
    }

    // Step 1 - Allocate queue head
    if (transfer->EndpointDescriptor == NULL) {
        if (UsbSchedulerAllocateElement(controller->Base.Scheduler,
                                        OHCI_QH_POOL, (uint8_t**)&endpointDescriptor) != OS_EOK) {
            goto queued;
        }
        assert(endpointDescriptor != NULL);
        transfer->EndpointDescriptor = endpointDescriptor;

        // Store and initialize the qh
        if (OhciQhInitialize(controller, transfer,
                             transfer->Transfer.Address.DeviceAddress,
                             transfer->Transfer.Address.EndpointAddress) != OS_EOK) {
            // No bandwidth, serious.
            UsbSchedulerFreeElement(controller->Base.Scheduler, (uint8_t*)endpointDescriptor);
            status = TransferNoBandwidth;
            goto exit;
        }
    }

    // If it fails to queue up => restore toggle
    if (OhciTransferFill(controller, transfer) != OS_EOK) {
        goto queued;
    }

    OhciTransactionDispatch(controller, transfer);
    status = TransferInProgress;
    goto exit;

queued:
    transfer->Status = TransferQueued;
    status = TransferQueued;

exit:
    return status;
}
