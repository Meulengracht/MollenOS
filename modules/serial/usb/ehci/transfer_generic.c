/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 */
//#define __TRACE

#include <ddk/utils.h>
#include "ehci.h"
#include <assert.h>
#include <stdlib.h>

static oserr_t
EhciTransferFill(
    _In_ EhciController_t*     Controller,
    _In_ UsbManagerTransfer_t* Transfer)
{
    EhciTransferDescriptor_t* PreviousTd = NULL;
    EhciTransferDescriptor_t* Td         = NULL;
    EhciQueueHead_t*          Qh         = (EhciQueueHead_t*)Transfer->EndpointDescriptor;
    
    int OutOfResources = 0;
    int i;

    TRACE("EhciTransferFill()");
    
    // Clear out the TransferFlagPartial
    Transfer->Flags &= ~(TransferFlagPartial);

    // Get next address from which we need to load
    for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
        uint8_t Type            = Transfer->Transfer.Transactions[i].Type;
        size_t  BytesToTransfer = Transfer->Transfer.Transactions[i].Length;
        int     PreviousToggle  = -1;
        int     Toggle          = 0;
        int     IsZLP           = Transfer->Transfer.Transactions[i].Flags & USB_TRANSACTION_ZLP;
        int     IsHandshake     = Transfer->Transfer.Transactions[i].Flags & USB_TRANSACTION_HANDSHAKE;

        TRACE("Transaction(%i, Length %u, Type %i)", i, BytesToTransfer, Type);

        BytesToTransfer -= Transfer->Transactions[i].BytesTransferred;
        if (BytesToTransfer == 0 && !IsZLP) {
            TRACE(" > Skipping");
            continue;
        }

        // If it's a handshake package AND it's first td of package, then set toggle
        if (Transfer->Transactions[i].BytesTransferred == 0 && IsHandshake) {
            Transfer->Transfer.Transactions[i].Flags &= ~(USB_TRANSACTION_HANDSHAKE);
            PreviousToggle = UsbManagerGetToggle(Transfer->DeviceId, &Transfer->Transfer.Address);
            UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, 1);
        }
        
        // If its a bulk transfer, with a direction of out, and the requested length is a multiple of
        // the MPS, then we should make sure we add a ZLP
        if ((Transfer->Transfer.Transactions[i].Length % Transfer->Transfer.MaxPacketSize) == 0 &&
            Transfer->Transfer.Type == USB_TRANSFER_BULK &&
            Transfer->Transfer.Transactions[i].Type == USB_TRANSACTION_OUT) {
            Transfer->Transfer.Transactions[i].Flags |= USB_TRANSACTION_ZLP;
            IsZLP = 1;
        }

        TRACE(" > BytesToTransfer(%u)", BytesToTransfer);
        while (BytesToTransfer || IsZLP) {
            DMASG_t*  Dma     = NULL;
            size_t    Length  = BytesToTransfer;
            uintptr_t Address = 0;
            
            if (Length && Transfer->Transfer.Transactions[i].BufferHandle != UUID_INVALID) {
                Dma     = &Transfer->Transactions[i].DmaTable.entries[Transfer->Transactions[i].SgIndex];
                Address = Dma->address + Transfer->Transactions[i].SgOffset;
                Length  = MIN(Length, Dma->length - Transfer->Transactions[i].SgOffset);
            }
            
            Toggle = UsbManagerGetToggle(Transfer->DeviceId, &Transfer->Transfer.Address);
            if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, EHCI_TD_POOL, (uint8_t**)&Td) == OS_EOK) {
                if (Type == USB_TRANSACTION_SETUP) {
                    TRACE(" > Creating setup packet");
                    Toggle = 0; // Initial toggle must ALWAYS be 0 for setup
                    Length = EhciTdSetup(Controller, Td, Address, Length);
                }
                else {
                    TRACE(" > Creating io packet");
                    Length = EhciTdIo(Controller, Td, Transfer->Transfer.MaxPacketSize, 
                        Type, Address, Length, Toggle);
                }
            }

            // If we didn't allocate a td, we ran out of 
            // resources, and have to wait for more. Queue up what we have
            if (Td == NULL) {
                TRACE(" > Failed to allocate descriptor");
                if (PreviousToggle != -1) {
                    UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, PreviousToggle);
                    Transfer->Transfer.Transactions[i].Flags |= USB_TRANSACTION_HANDSHAKE;
                }
                OutOfResources = 1;
                break;
            }
            else {
                UsbSchedulerChainElement(Controller->Base.Scheduler, EHCI_QH_POOL, 
                    (uint8_t*)Qh, EHCI_TD_POOL, (uint8_t*)Td, USB_ELEMENT_NO_INDEX, USB_CHAIN_DEPTH);
                PreviousTd = Td;

                // Update toggle by flipping
                UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, Toggle ^ 1);
                
                // We have two terminating conditions, either we run out of bytes
                // or we had one ZLP that had to added. 
                // Make sure we handle the one where we run out of bytes
                if (Length) {
                    BytesToTransfer                    -= Length;
                    Transfer->Transactions[i].SgOffset += Length;
                    if (Transfer->Transactions[i].SgOffset == Dma->length) {
                        Transfer->Transactions[i].SgIndex++;
                        Transfer->Transactions[i].SgOffset = 0;
                    }
                }
                else {
                    assert(IsZLP != 0);
                    TRACE(" > Encountered zero-length");
                    Transfer->Transfer.Transactions[i].Flags &= ~(USB_TRANSACTION_ZLP);
                    break;
                }
            }
        }
        
        // Check for partial transfers
        if (OutOfResources == 1) {
            Transfer->Flags |= TransferFlagPartial;
            break;
        }
    }

    // If we ran out of resources queue up later
    if (PreviousTd != NULL) {
        // Set last td to generate a interrupt (not null)
        PreviousTd->Token         |= EHCI_TD_IOC;
        PreviousTd->OriginalToken |= EHCI_TD_IOC;
        return OS_EOK;
    }
    return OS_EBUSY;
}

UsbTransferStatus_t
HciQueueTransferGeneric(
    _In_ UsbManagerTransfer_t* transfer)
{
    EhciQueueHead_t*    endpointDescriptor = NULL;
    EhciController_t*   controller;
    UsbTransferStatus_t status;

    controller = (EhciController_t*)UsbManagerGetController(transfer->DeviceId);
    if (!controller) {
        return TransferInvalid;
    }

    // Step 1 - Allocate queue head
    if (!transfer->EndpointDescriptor) {
        if (UsbSchedulerAllocateElement(controller->Base.Scheduler,
                                        EHCI_QH_POOL, (uint8_t**)&endpointDescriptor) != OS_EOK) {
            goto queued;
        }
        assert(endpointDescriptor != NULL);
        transfer->EndpointDescriptor = endpointDescriptor;

        // Store and initialize the qh
        if (EhciQhInitialize(controller, transfer,
                             transfer->Transfer.Address.DeviceAddress,
                             transfer->Transfer.Address.EndpointAddress) != OS_EOK) {
            // No bandwidth, serious.
            UsbSchedulerFreeElement(controller->Base.Scheduler, (uint8_t*)endpointDescriptor);
            status = TransferNoBandwidth;
            goto exit;
        }
    }

    // If it fails to queue up => restore toggle
    if (EhciTransferFill(controller, transfer) != OS_EOK) {
        goto queued;
    }

    EhciTransactionDispatch(controller, transfer);
    status = TransferInProgress;
    goto exit;

queued:
    transfer->Status = TransferQueued;
    status = TransferQueued;

exit:
    return status;
}
