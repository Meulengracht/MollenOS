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
 * Universal Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

#include <ddk/utils.h>
#include "uhci.h"
#include <assert.h>

static oserr_t
UhciTransferFillIsochronous(
    _In_ UhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer)
{
    UhciTransferDescriptor_t* initialTd  = NULL;
    UhciTransferDescriptor_t* previousTd = NULL;
    uintptr_t                 addressPointer;
    size_t                    bytesToTransfer;
    uint8_t                   transactionType = transfer->Transfer.Transactions[0].Type;
    
    TRACE("UhciTransferFillIsochronous()");

    bytesToTransfer = transfer->Transfer.Transactions[0].Length;
    while (bytesToTransfer) {
        UhciTransferDescriptor_t* td;
        size_t                    bytesStep;

        bytesStep = MIN(bytesToTransfer, transfer->Transfer.MaxPacketSize);
        bytesStep = MIN(bytesStep, transfer->Transactions[0].DmaTable.entries[
            transfer->Transactions[0].SgIndex].length - transfer->Transactions[0].SgOffset);

        addressPointer = transfer->Transactions[0].DmaTable.entries[
            transfer->Transactions[0].SgIndex].address + transfer->Transactions[0].SgOffset;
        
        if (UsbSchedulerAllocateElement(controller->Base.Scheduler, UHCI_TD_POOL, (uint8_t**)&td) == OsOK) {
            bytesStep = UhciTdIo(td, transfer->Transfer.Type, transactionType,
                                 transfer->Transfer.Address.DeviceAddress,
                                 transfer->Transfer.Address.EndpointAddress,
                                 transfer->Transfer.Speed, addressPointer, bytesStep, 0);
            
            if (UsbSchedulerAllocateBandwidth(controller->Base.Scheduler,
                                              transfer->Transfer.PeriodicInterval, transfer->Transfer.MaxPacketSize,
                                              transactionType, bytesStep, USB_TRANSFER_ISOCHRONOUS,
                                              transfer->Transfer.Speed, (uint8_t*)td) != OsOK) {
                // Free element
                UsbSchedulerFreeElement(controller->Base.Scheduler, (uint8_t*)td);
                break;
            }
        }

        // If we didn't allocate a td, we ran out of 
        // resources, and have to wait for more. Queue up what we have
        if (td == NULL) {
            break;
        }
        else {
            // Store first
            if (previousTd == NULL) {
                initialTd  = td;
                previousTd = td;
            }
            else {
                UsbSchedulerChainElement(controller->Base.Scheduler, UHCI_TD_POOL,
                                         (uint8_t*)initialTd, UHCI_TD_POOL, (uint8_t*)td,
                                         USB_ELEMENT_NO_INDEX, USB_CHAIN_DEPTH);
                previousTd = td;
            }

            // Increase the DmaTable metrics
            transfer->Transactions[0].SgOffset += bytesStep;
            if (transfer->Transactions[0].SgOffset ==
                transfer->Transactions[0].DmaTable.entries[
                        transfer->Transactions[0].SgIndex].length) {
                transfer->Transactions[0].SgIndex++;
                transfer->Transactions[0].SgOffset = 0;
            }
            bytesToTransfer -= bytesStep;
        }
    }

    // End of <transfer>?
    if (previousTd != NULL) {
        previousTd->Flags           |= UHCI_TD_IOC;
        transfer->EndpointDescriptor = initialTd;
        return OsOK;
    }

    return OsBusy;
}

static oserr_t
UhciTransferFill(
    _In_ UhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer)
{
    UhciTransferDescriptor_t* previousTd = NULL;
    UhciTransferDescriptor_t* td = NULL;
    UhciQueueHead_t*          qh = (UhciQueueHead_t*)transfer->EndpointDescriptor;
    
    int outOfResources = 0;
    int i;
    
    TRACE("UhciTransferFill()");

    // Clear out the TransferFlagPartial
    transfer->Flags &= ~(TransferFlagPartial);

    // Get next address from which we need to load
    for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
        uint8_t transactionType = transfer->Transfer.Transactions[i].Type;
        size_t  bytesToTransfer = transfer->Transfer.Transactions[i].Length;
        int     previousToggle = -1;
        int     toggle      = 0;
        int     isZlp       = transfer->Transfer.Transactions[i].Flags & USB_TRANSACTION_ZLP;
        int     isHandshake = transfer->Transfer.Transactions[i].Flags & USB_TRANSACTION_HANDSHAKE;

        TRACE("Transaction(%i, Buffer 0x%" PRIxIN ", Length %u, Type %i, MPS %u)", i,
              bytesToTransfer, transactionType, transfer->transfer.Endpoint.MaxPacketSize);

        bytesToTransfer -= transfer->Transactions[i].BytesTransferred;
        if (bytesToTransfer == 0 && !isZlp) {
            TRACE(" > Skipping");
            continue;
        }

        // If it's a handshake package AND it's first td of package, then set toggle
        if (transfer->Transactions[i].BytesTransferred == 0 && isHandshake) {
            transfer->Transfer.Transactions[i].Flags &= ~(USB_TRANSACTION_HANDSHAKE);
            previousToggle = UsbManagerGetToggle(transfer->DeviceId, &transfer->Transfer.Address);
            UsbManagerSetToggle(transfer->DeviceId, &transfer->Transfer.Address, 1);
        }
        
        // If its a bulk transfer, with a direction of out, and the requested length is a multiple of
        // the MPS, then we should make sure we add a ZLP
        if ((transfer->Transfer.Transactions[i].Length % transfer->Transfer.MaxPacketSize) == 0 &&
            transfer->Transfer.Type == USB_TRANSFER_BULK &&
            transfer->Transfer.Transactions[i].Type == USB_TRANSACTION_OUT) {
            transfer->Transfer.Transactions[i].Flags |= USB_TRANSACTION_ZLP;
            isZlp = 1;
        }

        // Keep adding td's
        TRACE(" > BytesToTransfer(%u)", bytesToTransfer);
        while (bytesToTransfer || isZlp) {
            struct dma_sg* Dma     = NULL;
            size_t         Length  = bytesToTransfer;
            uintptr_t      Address = 0;
            
            if (Length && transfer->Transfer.Transactions[i].BufferHandle != UUID_INVALID) {
                Dma     = &transfer->Transactions[i].DmaTable.entries[transfer->Transactions[i].SgIndex];
                Address = Dma->address + transfer->Transactions[i].SgOffset;
                Length  = MIN(Length, Dma->length - transfer->Transactions[i].SgOffset);
                Length  = MIN(Length, transfer->Transfer.MaxPacketSize);
            }

            toggle = UsbManagerGetToggle(transfer->DeviceId, &transfer->Transfer.Address);
            if (UsbSchedulerAllocateElement(controller->Base.Scheduler, UHCI_TD_POOL, (uint8_t**)&td) == OsOK) {
                if (transactionType == USB_TRANSACTION_SETUP) {
                    TRACE(" > Creating setup packet");
                    toggle = 0; // Initial toggle must ALWAYS be 0 for setup
                    Length = UhciTdSetup(td,
                                         transfer->Transfer.Address.DeviceAddress,
                                         transfer->Transfer.Address.EndpointAddress,
                                         transfer->Transfer.Speed, Address, Length);
                }
                else {
                    TRACE(" > Creating io packet");
                    Length = UhciTdIo(td, transfer->Transfer.Type, transactionType,
                                      transfer->Transfer.Address.DeviceAddress,
                                      transfer->Transfer.Address.EndpointAddress,
                                      transfer->Transfer.Speed, Address, Length, toggle);
                }
            }

            // If we didn't allocate a td, we ran out of 
            // resources, and have to wait for more. Queue up what we have
            if (td == NULL) {
                TRACE(" > Failed to allocate descriptor");
                if (previousToggle != -1) {
                    UsbManagerSetToggle(transfer->DeviceId, &transfer->Transfer.Address, previousToggle);
                    transfer->Transfer.Transactions[i].Flags |= USB_TRANSACTION_HANDSHAKE;
                }
                outOfResources = 1;
                break;
            }
            else {
                UsbSchedulerChainElement(controller->Base.Scheduler, UHCI_QH_POOL,
                                         (uint8_t*)qh, UHCI_TD_POOL, (uint8_t*)td, USB_ELEMENT_NO_INDEX, USB_CHAIN_DEPTH);
                previousTd = td;

                // Update toggle by flipping
                UsbManagerSetToggle(transfer->DeviceId, &transfer->Transfer.Address, toggle ^ 1);

                // We have two terminating conditions, either we run out of bytes
                // or we had one ZLP that had to added. 
                // Make sure we handle the one where we run out of bytes
                if (Length) {
                    bytesToTransfer                    -= Length;
                    transfer->Transactions[i].SgOffset += Length;
                    if (Dma && transfer->Transactions[i].SgOffset == Dma->length) {
                        transfer->Transactions[i].SgIndex++;
                        transfer->Transactions[i].SgOffset = 0;
                    }
                }
                else {
                    assert(isZlp != 0);
                    TRACE(" > Encountered zero-length");
                    transfer->Transfer.Transactions[i].Flags &= ~(USB_TRANSACTION_ZLP);
                    break;
                }
            }
        }

        // Cancel?
        if (outOfResources == 1) {
            transfer->Flags |= TransferFlagPartial;
            break;
        }
    }
    
    // End of <transfer>?
    if (previousTd != NULL) {
        previousTd->Flags |= UHCI_TD_IOC;
        return OsOK;
    }
    return OsBusy;
}

UsbTransferStatus_t
HciQueueTransferGeneric(
    _In_ UsbManagerTransfer_t* transfer)
{
    UhciQueueHead_t*    endpointDescriptor = NULL;
    UhciController_t*   controller;
    UsbTransferStatus_t status;

    controller = (UhciController_t*)UsbManagerGetController(transfer->DeviceId);
    if (!controller) {
        return TransferInvalid;
    }

    // Step 1 - Allocate queue head
    if (transfer->EndpointDescriptor == NULL) {
        if (UsbSchedulerAllocateElement(controller->Base.Scheduler,
                                        UHCI_QH_POOL, (uint8_t**)&endpointDescriptor) != OsOK) {
            goto queued;
        }
        assert(endpointDescriptor != NULL);
        transfer->EndpointDescriptor = endpointDescriptor;

        // Store and initialize the qh
        if (UhciQhInitialize(controller, transfer) != OsOK) {
            // No bandwidth, serious.
            UsbSchedulerFreeElement(controller->Base.Scheduler, (uint8_t*)endpointDescriptor);
            status = TransferNoBandwidth;
            goto exit;
        }
    }

    // If it fails to queue up => restore toggle
    if (UhciTransferFill(controller, transfer) != OsOK) {
        goto queued;
    }

    UhciTransactionDispatch(controller, transfer);
    status = TransferInProgress;
    goto exit;

queued:
    transfer->Status = TransferQueued;
    status = TransferQueued;

exit:
    return status;
}

UsbTransferStatus_t
HciQueueTransferIsochronous(
    _In_ UsbManagerTransfer_t* transfer)
{
    UhciController_t* controller;

    controller = (UhciController_t*)UsbManagerGetController(transfer->DeviceId);
    if (!controller) {
        return TransferInvalid;
    }

    if (UhciTransferFillIsochronous(controller, transfer) != OsOK) {
        transfer->Status = TransferQueued;
        return TransferQueued;
    }

    UhciTransactionDispatch(controller, transfer);
    return TransferInProgress;
}
