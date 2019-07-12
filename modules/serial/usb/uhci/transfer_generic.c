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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Universal Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

#include <os/mollenos.h>
#include <ddk/utils.h>
#include "uhci.h"
#include <assert.h>
#include <string.h>

static OsStatus_t
UhciTransferFillIsochronous(
    _In_ UhciController_t*     Controller,
    _In_ UsbManagerTransfer_t* Transfer)
{
    UhciTransferDescriptor_t* InitialTd  = NULL;
    UhciTransferDescriptor_t* PreviousTd = NULL;
    uintptr_t                 AddressPointer;
    size_t                    BytesToTransfer;

    TRACE("UhciTransferFillIsochronous()");

    UsbTransactionType_t Type = Transfer->Transfer.Transactions[0].Type;
    BytesToTransfer           = Transfer->Transfer.Transactions[0].Length;

    // Keep adding td's
    // @todo adjust for isoc have a larger length max 0x4FF??
    while (BytesToTransfer) {
        UhciTransferDescriptor_t* Td;
        size_t                    BytesStep;
        
        BytesStep = MIN(BytesToTransfer, Transfer->Transfer.Endpoint.MaxPacketSize);
        BytesStep = MIN(BytesStep, Transfer->Transactions[0].DmaTable.entries[
            Transfer->Transactions[0].SgIndex].length - Transfer->Transactions[0].SgOffset);
        
        AddressPointer = Transfer->Transactions[0].DmaTable.entries[
            Transfer->Transactions[0].SgIndex].address + Transfer->Transactions[0].SgOffset;
        
        if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, UHCI_TD_POOL, (uint8_t**)&Td) == OsSuccess) {
            BytesStep = UhciTdIo(Td, Transfer->Transfer.Type, Type,
                Transfer->Transfer.Address.DeviceAddress, 
                Transfer->Transfer.Address.EndpointAddress,
                Transfer->Transfer.Speed, AddressPointer, BytesStep, 0);
            
            if (UsbSchedulerAllocateBandwidth(Controller->Base.Scheduler, &Transfer->Transfer.Endpoint,
                BytesStep, IsochronousTransfer, Transfer->Transfer.Speed, (uint8_t*)Td) != OsSuccess) {
                // Free element
                UsbSchedulerFreeElement(Controller->Base.Scheduler, (uint8_t*)Td);
                break;
            }
        }

        // If we didn't allocate a td, we ran out of 
        // resources, and have to wait for more. Queue up what we have
        if (Td == NULL) {
            break;
        }
        else {
            // Store first
            if (PreviousTd == NULL) {
                InitialTd  = Td;
                PreviousTd = Td;
            }
            else {
                UsbSchedulerChainElement(Controller->Base.Scheduler, UHCI_TD_POOL, 
                    (uint8_t*)InitialTd, UHCI_TD_POOL, (uint8_t*)Td, USB_ELEMENT_NO_INDEX, USB_CHAIN_DEPTH);
                PreviousTd = Td;
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
    }

    // End of <transfer>?
    if (PreviousTd != NULL) {
        PreviousTd->Flags           |= UHCI_TD_IOC;
        Transfer->EndpointDescriptor = InitialTd;
        return OsSuccess;
    }
    
    // Queue up for later
    return OsError;
}

static OsStatus_t
UhciTransferFill(
    _In_ UhciController_t*     Controller,
    _In_ UsbManagerTransfer_t* Transfer)
{
    UhciTransferDescriptor_t* PreviousTd     = NULL;
    UhciTransferDescriptor_t* Td             = NULL;
    UhciQueueHead_t*          Qh             = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
    
    int OutOfResources = 0;
    int i;
    
    TRACE("UhciTransferFill()");

    // Clear out the TransferFlagPartial
    Transfer->Flags &= ~(TransferFlagPartial);

    // Get next address from which we need to load
    for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
        UsbTransactionType_t Type            = Transfer->Transfer.Transactions[i].Type;
        size_t               BytesToTransfer = Transfer->Transfer.Transactions[i].Length;
        int                  PreviousToggle  = -1;
        int                  Toggle          = 0;
        int                  IsZLP           = Transfer->Transfer.Transactions[i].Flags & USB_TRANSACTION_ZLP;
        int                  IsHandshake     = Transfer->Transfer.Transactions[i].Flags & USB_TRANSACTION_HANDSHAKE;

        TRACE("Transaction(%i, Buffer 0x%" PRIxIN ", Length %u, Type %i, MPS %u)", i,
            BytesToTransfer, Type, Transfer->Transfer.Endpoint.MaxPacketSize);

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

        // Keep adding td's
        TRACE(" > BytesToTransfer(%u)", BytesToTransfer);
        while (BytesToTransfer || IsZLP) {
            struct dma_sg* Dma     = &Transfer->Transactions[i].DmaTable.entries[Transfer->Transactions[i].SgIndex];
            uintptr_t      Address = Dma->address + Transfer->Transactions[i].SgOffset;
            size_t         Length;
            
            Length = MIN(BytesToTransfer, Dma->length - Transfer->Transactions[i].SgOffset);
            Length = MIN(Length, Transfer->Transfer.Endpoint.MaxPacketSize);
            
            Toggle = UsbManagerGetToggle(Transfer->DeviceId, &Transfer->Transfer.Address);
            if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, UHCI_TD_POOL, (uint8_t**)&Td) == OsSuccess) {
                if (Type == SetupTransaction) {
                    TRACE(" > Creating setup packet");
                    Toggle = 0; // Initial toggle must ALWAYS be 0 for setup
                    Length = UhciTdSetup(Td,
                        Transfer->Transfer.Address.DeviceAddress,
                        Transfer->Transfer.Address.EndpointAddress, 
                        Transfer->Transfer.Speed, Address, Length);
                }
                else {
                    TRACE(" > Creating io packet");
                    Length = UhciTdIo(Td, Transfer->Transfer.Type, Type, 
                        Transfer->Transfer.Address.DeviceAddress, 
                        Transfer->Transfer.Address.EndpointAddress,
                        Transfer->Transfer.Speed, Address, Length, Toggle);
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
                UsbSchedulerChainElement(Controller->Base.Scheduler, UHCI_QH_POOL, 
                        (uint8_t*)Qh, UHCI_TD_POOL, (uint8_t*)Td, USB_ELEMENT_NO_INDEX, USB_CHAIN_DEPTH);
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

                // Break out on zero lengths
                if (IsZLP) {
                    TRACE(" > Encountered zero-length");
                    Transfer->Transfer.Transactions[i].Flags &= ~(USB_TRANSACTION_ZLP);
                    break;
                }

                // If it was out, and we had a multiple of MPS, then ZLP
                if (Length == Transfer->Transfer.Endpoint.MaxPacketSize 
                    && BytesToTransfer == 0
                    && Transfer->Transfer.Type == BulkTransfer
                    && Transfer->Transfer.Transactions[i].Type == OutTransaction) {
                    Transfer->Transfer.Transactions[i].Flags |= USB_TRANSACTION_ZLP;
                    IsZLP = 1;
                }
            }
        }

        // Cancel?
        if (OutOfResources == 1) {
            Transfer->Flags |= TransferFlagPartial;
            break;
        }
    }
    
    // End of <transfer>?
    if (PreviousTd != NULL) {
        PreviousTd->Flags |= UHCI_TD_IOC;
        return OsSuccess;
    }
    
    // Queue up for later
    return OsError; 
}

UsbTransferStatus_t
HciQueueTransferGeneric(
    _In_ UsbManagerTransfer_t* Transfer)
{
    UhciQueueHead_t*  EndpointDescriptor = NULL;
    UhciController_t* Controller;
    DataKey_t         Key;
    
    Controller       = (UhciController_t*)UsbManagerGetController(Transfer->DeviceId);
    Transfer->Status = TransferNotProcessed;

    // Step 1 - Allocate queue head
    if (Transfer->EndpointDescriptor == NULL) {
        if (UsbSchedulerAllocateElement(Controller->Base.Scheduler, 
            UHCI_QH_POOL, (uint8_t**)&EndpointDescriptor) != OsSuccess) {
            return TransferQueued;
        }
        assert(EndpointDescriptor != NULL);
        Transfer->EndpointDescriptor = EndpointDescriptor;

        // Store and initialize the qh
        if (UhciQhInitialize(Controller, Transfer) != OsSuccess) {
            // No bandwidth, serious.
            UsbSchedulerFreeElement(Controller->Base.Scheduler, (uint8_t*)EndpointDescriptor);
            return TransferNoBandwidth;
        }
    }

    // Store transaction in queue if it's not there already
    Key.Value.Integer = (int)Transfer->Id;
    if (CollectionGetDataByKey(Controller->Base.TransactionList, Key, 0) == NULL) {
        CollectionAppend(Controller->Base.TransactionList, CollectionCreateNode(Key, Transfer));
    }

    // If it fails to queue up => restore toggle
    if (UhciTransferFill(Controller, Transfer) != OsSuccess) {
        return TransferQueued;
    }
    return UhciTransactionDispatch(Controller, Transfer);
}

UsbTransferStatus_t
HciQueueTransferIsochronous(
    _In_ UsbManagerTransfer_t* Transfer)
{
    UhciController_t* Controller;
    DataKey_t         Key;

    // Get Controller
    Controller       = (UhciController_t*)UsbManagerGetController(Transfer->DeviceId);
    Transfer->Status = TransferNotProcessed;

    // Store transaction in queue if it's not there already
    Key.Value.Integer = (int)Transfer->Id;
    if (CollectionGetDataByKey(Controller->Base.TransactionList, Key, 0) == NULL) {
        CollectionAppend(Controller->Base.TransactionList, CollectionCreateNode(Key, Transfer));
    }
    
    // Fill the transfer
    if (UhciTransferFillIsochronous(Controller, Transfer) != OsSuccess) {
        return TransferQueued;
    }
    return UhciTransactionDispatch(Controller, Transfer);
}
