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
 * MollenOS MCore - Universal Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

#include <ds/collection.h>
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "../uhci.h"
#include <string.h>
#include <stdlib.h>

void
UhciTdSetup(
    _In_ UhciTransferDescriptor_t* Td,
    _In_ uintptr_t                 BufferAddress,
    _In_ size_t                    Address, 
    _In_ size_t                    Endpoint,
    _In_ UsbSpeed_t                Speed)
{
    // Set no link
    Td->Link = UHCI_LINK_END;

    // Setup td flags
    Td->Flags  = UHCI_TD_ACTIVE;
    Td->Flags |= UHCI_TD_SETCOUNT(3);
    if (Speed == LowSpeed) {
        Td->Flags |= UHCI_TD_LOWSPEED;
    }

    // Setup td header
    Td->Header  = UHCI_TD_PID_SETUP;
    Td->Header |= UHCI_TD_DEVICE_ADDR(Address);
    Td->Header |= UHCI_TD_EP_ADDR(Endpoint);
    Td->Header |= UHCI_TD_MAX_LEN((sizeof(UsbPacket_t) - 1));

    // Install the buffer
    Td->Buffer = LODWORD(BufferAddress);

    // Store data
    Td->OriginalFlags  = Td->Flags;
    Td->OriginalHeader = Td->Header;
    
    // Set usb scheduler link info
    Td->Object.Flags |= UHCI_LINK_DEPTH;
}

void
UhciTdIo(
    _In_ UhciTransferDescriptor_t* Td,
    _In_ UsbTransferType_t         Type,
    _In_ uint32_t                  PId,
    _In_ int                       Toggle,
    _In_ size_t                    Address, 
    _In_ size_t                    Endpoint,
    _In_ size_t                    MaxPacketSize,
    _In_ UsbSpeed_t                Speed,
    _In_ uintptr_t                 BufferAddress,
    _In_ size_t                    Length)
{
    // Set no link
    Td->Link = UHCI_LINK_END;

    // Setup td flags
    Td->Flags  = UHCI_TD_ACTIVE;
    Td->Flags |= UHCI_TD_SETCOUNT(3);
    if (Speed == LowSpeed) {
        Td->Flags |= UHCI_TD_LOWSPEED;
    }
    if (Type == IsochronousTransfer) {
        Td->Flags |= UHCI_TD_ISOCHRONOUS;
    }

    // We set SPD on in transfers, but NOT on zero length
    if (Type == ControlTransfer) {
        if (PId == UHCI_TD_PID_IN && Length > 0) {
            Td->Flags |= UHCI_TD_SHORT_PACKET;
        }
    }
    else if (PId == UHCI_TD_PID_IN) {
        Td->Flags |= UHCI_TD_SHORT_PACKET;
    }

    // Setup td header
    Td->Header  = PId;
    Td->Header |= UHCI_TD_DEVICE_ADDR(Address);
    Td->Header |= UHCI_TD_EP_ADDR(Endpoint);
    if (Toggle) {
        Td->Header |= UHCI_TD_DATA_TOGGLE;
    }

    // Setup size
    if (Length > 0) {
        if (Length < MaxPacketSize && Type == InterruptTransfer) {
            Td->Header |= UHCI_TD_MAX_LEN((MaxPacketSize - 1));
        }
        else {
            Td->Header |= UHCI_TD_MAX_LEN((Length - 1));
        }
    }
    else {
        Td->Header |= UHCI_TD_MAX_LEN(0x7FF);
    }

    // Store buffer
    Td->Buffer = LODWORD(BufferAddress);

    // Store data
    Td->OriginalFlags  = Td->Flags;
    Td->OriginalHeader = Td->Header;

    // Set usb scheduler link info
    Td->Object.Flags |= UHCI_LINK_DEPTH;
}

/* UhciTdDump
 * Dumps the information contained in the descriptor by writing it. */
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
    _In_  UsbManagerTransfer_t*     Transfer,
    _In_  UhciTransferDescriptor_t* Td)
{
    int ErrorCode = UhciConditionCodeToIndex(UHCI_TD_STATUS(Td->Flags));
    int i;

    // Sanitize active status
    if (Td->Flags & UHCI_TD_ACTIVE) {
        // If this one is still active, but it's an transfer that has
        // elements processed - resync toggles
        if (Transfer->Status != TransferQueued) {
            Transfer->Flags |= TransferFlagSync;
        }
        return;
    }
    Transfer->TransactionsExecuted++;

    // Now validate the code
    if (ErrorCode != 0) {
        Transfer->Status = UhciGetStatusCode(ErrorCode);
        return; // Skip bytes transferred
    }
    else if (ErrorCode == 0 && Transfer->Status == TransferQueued) {
        Transfer->Status = TransferFinished;
    }

    // Calculate length transferred 
    // Take into consideration the N-1 
    if (Td->Buffer != 0) {
        int BytesTransferred = UHCI_TD_ACTUALLENGTH(Td->Flags) + 1;
        int BytesRequested   = UHCI_TD_GET_LEN(Td->Header) + 1;
        if (BytesTransferred < BytesRequested) {
            Transfer->Flags |= TransferFlagShort;

            // On short transfers we might have to sync, but only 
            // if there are un-processed td's after this one
            if (Td->Object.DepthIndex != USB_ELEMENT_NO_INDEX) {
                Transfer->Flags |= TransferFlagSync;
            }
        }
        for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
            if (Transfer->Transfer.Transactions[i].Length > Transfer->BytesTransferred[i]) {
                Transfer->BytesTransferred[i] += BytesTransferred;
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
    if (Transfer->Transfer.Type == InterruptTransfer) {
        Qh         = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
        BufferStep = Transfer->Transfer.Endpoint.MaxPacketSize;
        
        // Flip
        Td->OriginalHeader &= ~UHCI_TD_DATA_TOGGLE;
        if (Toggle) {
            Td->OriginalHeader |= UHCI_TD_DATA_TOGGLE;
        }
        UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, Toggle ^ 1);
        
        // Adjust buffer if not just restart
        if (Transfer->Status != TransferNAK) {
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
