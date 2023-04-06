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
#define __need_minmax
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
    uint8_t                   transactionType = transfer->Base.Transactions[0].Type;
    
    TRACE("UhciTransferFillIsochronous()");

    bytesToTransfer = transfer->Base.Transactions[0].Length;
    while (bytesToTransfer) {
        UhciTransferDescriptor_t* td;
        size_t                    bytesStep;

        bytesStep = MIN(bytesToTransfer, transfer->Base.MaxPacketSize);
        bytesStep = MIN(bytesStep, transfer->Transactions[0].SHMTable.Entries[
            transfer->Transactions[0].SGIndex].Length - transfer->Transactions[0].SGOffset);

        addressPointer = transfer->Transactions[0].SHMTable.Entries[
            transfer->Transactions[0].SGIndex].Address + transfer->Transactions[0].SGOffset;
        
        if (UsbSchedulerAllocateElement(controller->Base.Scheduler, UHCI_TD_POOL, (uint8_t**)&td) == OS_EOK) {
            bytesStep = UhciTdIo(td, transfer->Base.Type, transactionType,
                                 transfer->Base.Address.DeviceAddress,
                                 transfer->Base.Address.EndpointAddress,
                                 transfer->Base.Speed, addressPointer, bytesStep, 0);
            
            if (UsbSchedulerAllocateBandwidth(controller->Base.Scheduler,
                                              transfer->Base.PeriodicInterval, transfer->Base.MaxPacketSize,
                                              transactionType, bytesStep, USBTRANSFER_TYPE_ISOC,
                                              transfer->Base.Speed, (uint8_t*)td) != OS_EOK) {
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

            // Increase the SHMTable metrics
            transfer->Transactions[0].SGOffset += bytesStep;
            if (transfer->Transactions[0].SGOffset ==
                transfer->Transactions[0].SHMTable.Entries[
                        transfer->Transactions[0].SGIndex].Length) {
                transfer->Transactions[0].SGIndex++;
                transfer->Transactions[0].SGOffset = 0;
            }
            bytesToTransfer -= bytesStep;
        }
    }

    // End of <transfer>?
    if (previousTd != NULL) {
        previousTd->Flags           |= UHCI_TD_IOC;
        transfer->EndpointDescriptor = initialTd;
        return OS_EOK;
    }

    return OS_EBUSY;
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
        uint8_t transactionType = transfer->Base.Transactions[i].Type;
        size_t  bytesToTransfer = transfer->Base.Transactions[i].Length;
        int     previousToggle = -1;
        int     toggle      = 0;
        int     isZlp       = transfer->Base.Transactions[i].Flags & USB_TRANSACTION_ZLP;
        int     isHandshake = transfer->Base.Transactions[i].Flags & USB_TRANSACTION_HANDSHAKE;

        TRACE("Transaction(%i, Buffer 0x%" PRIxIN ", Length %u, Type %i, MPS %u)", i,
              bytesToTransfer, transactionType, transfer->Base.MaxPacketSize);

        bytesToTransfer -= transfer->Transactions[i].BytesTransferred;
        if (bytesToTransfer == 0 && !isZlp) {
            TRACE(" > Skipping");
            continue;
        }

        // If it's a handshake package AND it's first td of package, then set toggle
        if (transfer->Transactions[i].BytesTransferred == 0 && isHandshake) {
            transfer->Base.Transactions[i].Flags &= ~(USB_TRANSACTION_HANDSHAKE);
            previousToggle = UsbManagerGetToggle(transfer->DeviceID, &transfer->Base.Address);
            UsbManagerSetToggle(transfer->DeviceID, &transfer->Base.Address, 1);
        }
        
        // If its a bulk transfer, with a direction of out, and the requested length is a multiple of
        // the MPS, then we should make sure we add a ZLP
        if ((transfer->Base.Transactions[i].Length % transfer->Base.MaxPacketSize) == 0 &&
            transfer->Base.Type == USB_TRANSFER_BULK &&
            transfer->Base.Transactions[i].Type == USB_TRANSACTION_OUT) {
            transfer->Base.Transactions[i].Flags |= USB_TRANSACTION_ZLP;
            isZlp = 1;
        }

        // Keep adding td's
        TRACE(" > BytesToTransfer(%u)", bytesToTransfer);
        while (bytesToTransfer || isZlp) {
            SHMSG_t*  SG     = NULL;
            size_t    Length  = bytesToTransfer;
            uintptr_t Address = 0;
            
            if (Length && transfer->Base.Transactions[i].BufferHandle != UUID_INVALID) {
                SG      = &transfer->Transactions[i].SHMTable.Entries[transfer->Transactions[i].SGIndex];
                Address = SG->Address + transfer->Transactions[i].SGOffset;
                Length  = MIN(Length, SG->Length - transfer->Transactions[i].SGOffset);
                Length  = MIN(Length, transfer->Base.MaxPacketSize);
            }

            toggle = UsbManagerGetToggle(transfer->DeviceID, &transfer->Base.Address);
            if (UsbSchedulerAllocateElement(controller->Base.Scheduler, UHCI_TD_POOL, (uint8_t**)&td) == OS_EOK) {
                if (transactionType == USB_TRANSACTION_SETUP) {
                    TRACE(" > Creating setup packet");
                    toggle = 0; // Initial toggle must ALWAYS be 0 for setup
                    Length = UhciTdSetup(td,
                                         transfer->Base.Address.DeviceAddress,
                                         transfer->Base.Address.EndpointAddress,
                                         transfer->Base.Speed, Address, Length);
                }
                else {
                    TRACE(" > Creating io packet");
                    Length = UhciTdIo(td, transfer->Base.Type, transactionType,
                                      transfer->Base.Address.DeviceAddress,
                                      transfer->Base.Address.EndpointAddress,
                                      transfer->Base.Speed, Address, Length, toggle);
                }
            }

            // If we didn't allocate a td, we ran out of 
            // resources, and have to wait for more. Queue up what we have
            if (td == NULL) {
                TRACE(" > Failed to allocate descriptor");
                if (previousToggle != -1) {
                    UsbManagerSetToggle(transfer->DeviceID, &transfer->Base.Address, previousToggle);
                    transfer->Base.Transactions[i].Flags |= USB_TRANSACTION_HANDSHAKE;
                }
                outOfResources = 1;
                break;
            }
            else {
                UsbSchedulerChainElement(controller->Base.Scheduler, UHCI_QH_POOL,
                                         (uint8_t*)qh, UHCI_TD_POOL, (uint8_t*)td, USB_ELEMENT_NO_INDEX, USB_CHAIN_DEPTH);
                previousTd = td;

                // Update toggle by flipping
                UsbManagerSetToggle(transfer->DeviceID, &transfer->Base.Address, toggle ^ 1);

                // We have two terminating conditions, either we run out of bytes
                // or we had one ZLP that had to added. 
                // Make sure we handle the one where we run out of bytes
                if (Length) {
                    bytesToTransfer                    -= Length;
                    transfer->Transactions[i].SGOffset += Length;
                    if (SG && transfer->Transactions[i].SGOffset == SG->Length) {
                        transfer->Transactions[i].SGIndex++;
                        transfer->Transactions[i].SGOffset = 0;
                    }
                }
                else {
                    assert(isZlp != 0);
                    TRACE(" > Encountered zero-length");
                    transfer->Base.Transactions[i].Flags &= ~(USB_TRANSACTION_ZLP);
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
        return OS_EOK;
    }
    return OS_EBUSY;
}

enum USBTransferCode
HCITransferQueue(
    _In_ UsbManagerTransfer_t* transfer)
{
    UhciQueueHead_t*    endpointDescriptor = NULL;
    UhciController_t*   controller;
    enum USBTransferCode status;
    TRACE("HCITransferQueue()");

    controller = (UhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (!controller) {
        return TransferInvalid;
    }

    // Step 1 - Allocate queue head
    if (transfer->EndpointDescriptor == NULL) {
        if (UsbSchedulerAllocateElement(controller->Base.Scheduler,
                                        UHCI_QH_POOL, (uint8_t**)&endpointDescriptor) != OS_EOK) {
            goto queued;
        }
        assert(endpointDescriptor != NULL);
        transfer->EndpointDescriptor = endpointDescriptor;

        // Store and initialize the qh
        if (UhciQhInitialize(controller, transfer) != OS_EOK) {
            // No bandwidth, serious.
            UsbSchedulerFreeElement(controller->Base.Scheduler, (uint8_t*)endpointDescriptor);
            status = TransferNoBandwidth;
            goto exit;
        }
    }

    // If it fails to queue up => restore toggle
    if (UhciTransferFill(controller, transfer) != OS_EOK) {
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

enum USBTransferCode
HciQueueTransferIsochronous(
    _In_ UsbManagerTransfer_t* transfer)
{
    UhciController_t* controller;

    controller = (UhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (!controller) {
        return TransferInvalid;
    }

    if (UhciTransferFillIsochronous(controller, transfer) != OS_EOK) {
        transfer->Status = TransferQueued;
        return TransferQueued;
    }

    UhciTransactionDispatch(controller, transfer);
    return TransferInProgress;
}
