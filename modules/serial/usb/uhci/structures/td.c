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

#include <ddk/utils.h>
#include "../uhci.h"
#include <stdlib.h>

size_t
UhciTdSetup(
    _In_ UhciTransferDescriptor_t* Td,
    _In_ size_t                    Device, 
    _In_ size_t                    Endpoint,
    _In_ uint8_t                   Speed,
    _In_ uintptr_t                 Address,
    _In_ size_t                    Length)
{
    uintptr_t CalculatedLength;
    
    // TODO: use frame-size for platfrom
    // This also works for 0 length packets, as it will result in 0 - 1
    CalculatedLength = MIN(UHCI_TD_LENGTH_MASK, 0x1000 - (Address % 0x1000));
    CalculatedLength = MIN(CalculatedLength, Length);
    
    // Set no link
    Td->Link = UHCI_LINK_END;

    // Setup td flags
    Td->Flags  = UHCI_TD_ACTIVE;
    Td->Flags |= UHCI_TD_SETCOUNT(3);
    if (Speed == USB_SPEED_LOW) {
        Td->Flags |= UHCI_TD_LOWSPEED;
    }

    // Setup td header
    Td->Header  = UHCI_TD_PID_SETUP;
    Td->Header |= UHCI_TD_DEVICE_ADDR(Device);
    Td->Header |= UHCI_TD_EP_ADDR(Endpoint);
    Td->Header |= UHCI_TD_MAX_LEN(CalculatedLength - 1);

    // Install the buffer
    Td->Buffer = LODWORD(Address);

    // Store data
    Td->OriginalFlags  = Td->Flags;
    Td->OriginalHeader = Td->Header;
    
    // Set usb scheduler link info
    Td->Object.Flags |= UHCI_LINK_DEPTH;
    return CalculatedLength;
}

size_t
UhciTdIo(
    _In_ UhciTransferDescriptor_t* Td,
    _In_ uint8_t                   Type,
    _In_ uint8_t                   transactionType,
    _In_ size_t                    Device, 
    _In_ size_t                    Endpoint,
    _In_ uint8_t                   Speed,
    _In_ uintptr_t                 Address,
    _In_ size_t                    Length,
    _In_ int                       Toggle)
{
    uintptr_t CalculatedLength;
    uint32_t  PId = (transactionType == USB_TRANSACTION_IN ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT);
    
    // TODO: use frame-size for platfrom
    // This also works for 0 length packets, as it will result in 0 - 1
    CalculatedLength = MIN(UHCI_TD_LENGTH_MASK, 0x1000 - (Address % 0x1000));
    CalculatedLength = MIN(CalculatedLength, Length);
    
    // Set no link
    Td->Link = UHCI_LINK_END;

    // Setup td flags
    Td->Flags  = UHCI_TD_ACTIVE;
    Td->Flags |= UHCI_TD_SETCOUNT(3);
    if (Speed == USB_SPEED_LOW) {
        Td->Flags |= UHCI_TD_LOWSPEED;
    }
    if (Type == USB_TRANSFER_ISOCHRONOUS) {
        Td->Flags |= UHCI_TD_ISOCHRONOUS;
    }

    // We set SPD on in transfers, but NOT on zero length
    if (Type == USB_TRANSFER_CONTROL) {
        if (PId == UHCI_TD_PID_IN && Length > 0) {
            Td->Flags |= UHCI_TD_SHORT_PACKET;
        }
    }
    else if (PId == UHCI_TD_PID_IN) {
        Td->Flags |= UHCI_TD_SHORT_PACKET;
    }

    // Setup td header
    Td->Header  = PId;
    Td->Header |= UHCI_TD_DEVICE_ADDR(Device);
    Td->Header |= UHCI_TD_EP_ADDR(Endpoint);
    if (Toggle) {
        Td->Header |= UHCI_TD_DATA_TOGGLE;
    }

    // Setup size
    Td->Header |= UHCI_TD_MAX_LEN(CalculatedLength - 1);

    // Store buffer
    Td->Buffer = LODWORD(Address);

    // Store data
    Td->OriginalFlags  = Td->Flags;
    Td->OriginalHeader = Td->Header;

    // Set usb scheduler link info
    Td->Object.Flags |= UHCI_LINK_DEPTH;
    return CalculatedLength;
}

void
UhciTdDump(
    _In_ UhciController_t*         Controller,
    _In_ UhciTransferDescriptor_t* Td)
{
    uintptr_t PhysicalAddress   = 0;

    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_TD_POOL, 
        Td->Object.Index & USB_ELEMENT_INDEX_MASK, NULL, &PhysicalAddress);
    WARNING("TD(0x%x): Link 0x%x, Flags 0x%x, Header 0x%x, Buffer 0x%x", 
        PhysicalAddress, Td->Link, Td->Flags, Td->Header, Td->Buffer);
}

void
UhciTdValidate(
    _In_  UsbManagerTransfer_t*     transfer,
    _In_  UhciTransferDescriptor_t* td)
{
    int conditionCodeIndex = UhciConditionCodeToIndex(UHCI_TD_STATUS(td->Flags));
    int i;

    // Sanitize active status
    if (td->Flags & UHCI_TD_ACTIVE) {
        // If this one is still active, but it's an transfer that has
        // elements processed - resync toggles
        if (transfer->Status != TransferInProgress) {
            transfer->Flags |= TransferFlagSync;
        }
        return;
    }

    // Now validate the code
    if (conditionCodeIndex != 0) {
        transfer->Status = UhciGetStatusCode(conditionCodeIndex);
        return; // Skip bytes transferred
    }
    else if (transfer->Status == TransferInProgress) {
        transfer->Status = TransferFinished;
    }

    // Calculate length transferred 
    // Take into consideration the N-1 
    if (td->Buffer != 0) {
        int BytesTransferred = UHCI_TD_ACTUALLENGTH(td->Flags) + 1;
        int BytesRequested   = UHCI_TD_GET_LEN(td->Header) + 1;
        if (BytesTransferred < BytesRequested) {
            transfer->Flags |= TransferFlagShort;

            // On short transfers we might have to sync, but only 
            // if there are un-processed td's after this one
            if (td->Object.DepthIndex != USB_ELEMENT_NO_INDEX) {
                transfer->Flags |= TransferFlagSync;
            }
        }
        for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
            if (transfer->Transfer.Transactions[i].Length > transfer->Transactions[i].BytesTransferred) {
                transfer->Transactions[i].BytesTransferred += BytesTransferred;
                break;
            }
        }
    }
}

void
UhciTdSynchronize(
    _In_  UsbManagerTransfer_t*     Transfer,
    _In_  UhciTransferDescriptor_t* Td)
{
    int Toggle = UsbManagerGetToggle(Transfer->DeviceId, &Transfer->Transfer.Address);

    // Is it neccessary?
    if (Toggle == 1 && (Td->Header & UHCI_TD_DATA_TOGGLE)) {
        return;
    }

    Td->Header &= ~(UHCI_TD_DATA_TOGGLE);
    if (Toggle) {
        Td->Header |= UHCI_TD_DATA_TOGGLE;
    }

    // Update copy
    Td->OriginalHeader = Td->Header;
    UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, Toggle ^ 1);
}

void
UhciTdRestart(
    _In_  UsbManagerTransfer_t*     Transfer,
    _In_  UhciTransferDescriptor_t* Td)
{
    UhciQueueHead_t* Qh;
    uintptr_t        BufferBaseUpdated;
    uintptr_t        BufferStep;
    int              Toggle = UsbManagerGetToggle(Transfer->DeviceId, &Transfer->Transfer.Address);

    // Setup some variables
    if (Transfer->Transfer.Type == USB_TRANSFER_INTERRUPT) {
        Qh         = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
        BufferStep = Transfer->Transfer.MaxPacketSize;
        
        // Flip
        Td->OriginalHeader &= ~UHCI_TD_DATA_TOGGLE;
        if (Toggle) {
            Td->OriginalHeader |= UHCI_TD_DATA_TOGGLE;
        }
        UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, Toggle ^ 1);
        
        // Adjust buffer if not just restart
        if (Transfer->Status == TransferFinished) {
            BufferBaseUpdated = ADDLIMIT(Qh->BufferBase, Td->Buffer, 
                BufferStep, Qh->BufferBase + Transfer->Transfer.PeriodicBufferSize);
            Td->Buffer     = LODWORD(BufferBaseUpdated);
            Qh->BufferBase = LODWORD(BufferBaseUpdated);
        }
    }
    
    // Restore
    Td->Header = Td->OriginalHeader;
    Td->Flags  = Td->OriginalFlags;
}
