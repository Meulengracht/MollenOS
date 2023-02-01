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
 * Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */

//#define __TRACE
#define __need_minmax
#include <ddk/utils.h>
#include "ohci.h"
#include <assert.h>
#include <stdlib.h>

static oserr_t
OhciTransferFill(
    _In_ OhciController_t*     Controller,
    _In_ UsbManagerTransfer_t* Transfer)
{
    OhciTransferDescriptor_t *PreviousTd    = NULL;
    OhciTransferDescriptor_t *Td            = NULL;
    OhciQueueHead_t *Qh                     = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
    uint16_t ZeroIndex                      = Qh->Object.DepthIndex;
    
    int OutOfResources = 0;
    int i;

    // Debug
    TRACE("[usb] [ohci] fill transfer");

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
        
        TRACE("[usb] [ohci] xaction %i, length %u, type %i, zlp %i, handshake %i", 
            i, BytesToTransfer, Type, IsZLP, IsHandshake);

        BytesToTransfer -= Transfer->Transactions[i].BytesTransferred;
        if (BytesToTransfer == 0 && !IsZLP) {
            TRACE(" ... skipping");
            continue;
        }

        // If it's a handshake package AND it's first td of package, then set toggle
        if (Transfer->Transactions[i].BytesTransferred == 0 && IsHandshake) {
            TRACE("... setting toggle");
            Transfer->Transfer.Transactions[i].Flags &= ~(USB_TRANSACTION_HANDSHAKE);
            PreviousToggle = UsbManagerGetToggle(Transfer->DeviceId, &Transfer->Transfer.Address);
            UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, 1);
        }
        
        // If its a bulk transfer, with a direction of out, and the requested length is a multiple of
        // the MPS, then we should make sure we add a ZLP
        if ((Transfer->Transfer.Transactions[i].Length % Transfer->Transfer.MaxPacketSize) == 0 &&
            Transfer->Transfer.Type == USB_TRANSFER_BULK &&
            Transfer->Transfer.Transactions[i].Type == USB_TRANSACTION_OUT) {
            TRACE("... appending zlp");
            Transfer->Transfer.Transactions[i].Flags |= USB_TRANSACTION_ZLP;
            IsZLP = 1;
        }

        TRACE("[usb] [ohci] trimmed length %u", BytesToTransfer);
        while (BytesToTransfer || IsZLP) {
            SHMSG_t*   SG      = NULL;
            size_t     Length  = BytesToTransfer;
            uintptr_t  Address = 0;
            
            if (Length && Transfer->Transfer.Transactions[i].BufferHandle != UUID_INVALID) {
                SG      = &Transfer->Transactions[i].SHMTable.Entries[Transfer->Transactions[i].SGIndex];
                Address = SG->Address + Transfer->Transactions[i].SGOffset;
                Length  = MIN(Length, SG->Length - Transfer->Transactions[i].SGOffset);
            }
            
            Toggle = UsbManagerGetToggle(Transfer->DeviceId, &Transfer->Transfer.Address);
            TRACE("... address 0x%" PRIxIN ", length %u, toggle %i", Address, LODWORD(Length), Toggle);
            if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, OHCI_TD_POOL, (uint8_t**)&Td) == OS_EOK) {
                if (Type == USB_TRANSACTION_SETUP) {
                    TRACE("... setup packet");
                    Toggle = 0; // Initial toggle must ALWAYS be 0 for setup
                    Length = OhciTdSetup(Td, Address, Length);
                }
                else {
                    TRACE("... io packet");
                    Length = OhciTdIo(Td, Transfer->Transfer.Type, 
                        (Type == USB_TRANSACTION_IN ? OHCI_TD_IN : OHCI_TD_OUT), 
                        Toggle, Address, Length);
                }
            }

            // If we didn't allocate a td, we ran out of 
            // resources, and have to wait for more. Queue up what we have
            if (Td == NULL) {
                TRACE(".. failed to allocate descriptor");
                if (PreviousToggle != -1) {
                    UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, PreviousToggle);
                    Transfer->Transfer.Transactions[i].Flags |= USB_TRANSACTION_HANDSHAKE;
                }
                OutOfResources = 1;
                break;
            }
            else {
                UsbSchedulerChainElement(Controller->Base.Scheduler, 
                    OHCI_QH_POOL, (uint8_t*)Qh, OHCI_TD_POOL, (uint8_t*)Td, ZeroIndex, USB_CHAIN_DEPTH);
                PreviousTd = Td;

                // Update toggle by flipping
                UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, Toggle ^ 1);
                
                // We have two terminating conditions, either we run out of bytes
                // or we had one ZLP that had to added. 
                // Make sure we handle the one where we run out of bytes
                if (Length) {
                    BytesToTransfer                    -= Length;
                    Transfer->Transactions[i].SGOffset += Length;
                    if (SG && Transfer->Transactions[i].SGOffset == SG->Length) {
                        Transfer->Transactions[i].SGIndex++;
                        Transfer->Transactions[i].SGOffset = 0;
                    }
                }
                else {
                    assert(IsZLP != 0);
                    TRACE(".. zlp, done");
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
        // Enable ioc
        PreviousTd->Flags         &= ~OHCI_TD_IOC_NONE;
        PreviousTd->OriginalFlags = PreviousTd->Flags;
        return OS_EOK;
    }
    
    // Queue up for later
    return OS_EUNKNOWN;
}

UsbTransferStatus_t
HciQueueTransferGeneric(
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
