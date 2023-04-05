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
#define __need_minmax
#include <os/mollenos.h>
#include <os/memory.h>
#include <ddk/utils.h>
#include "../ohci.h"
#include <assert.h>
#include <string.h>

void
OHCITDSetup(
    _In_ OhciTransferDescriptor_t* td,
    _In_ uintptr_t                 dataAddress)
{
    // Set no link
    td->Link = 0;

    // Initialize the Td flags
    td->Flags |= OHCI_TD_SETUP;
    td->Flags |= OHCI_TD_IOC_NONE;
    td->Flags |= OHCI_TD_TOGGLE_LOCAL;
    td->Flags |= OHCI_TD_ACTIVE;

    // Install the buffer
    td->Cbp       = dataAddress;
    td->BufferEnd = td->Cbp + (sizeof(usb_packet_t) - 1);

    // Store copy of original content
    td->OriginalFlags = td->Flags;
    td->OriginalCbp   = td->Cbp;
}

size_t
OhciTdIo(
    _In_ OhciTransferDescriptor_t* Td,
    _In_ uint8_t                   Type,
    _In_ uint32_t                  PId,
    _In_ int                       Toggle,
    _In_ uintptr_t                 Address,
    _In_ size_t                    Length)
{
    size_t pageSize = MemoryPageSize();
    size_t calculatedLength;
    
    TRACE("OhciTdIo(Type %u, Id %u, Toggle %i, Address 0x%x, Length 0x%x",
        Type, PId, Toggle, Address, Length);

    // We can encompass a maximum of two pages (when the initial page offset is 0)
    // so make sure we correct for that limit
    calculatedLength = MIN(((2 * pageSize) - (Address & (pageSize - 1))), Length);

    // Set this is as end of chain
    Td->Link = 0;

    // Initialize flags as a IO Td
    Td->Flags |= PId;
    Td->Flags |= OHCI_TD_IOC_NONE;
    Td->Flags |= OHCI_TD_TOGGLE_LOCAL;
    Td->Flags |= OHCI_TD_ACTIVE;

    // We have to allow short-packets in some cases
    // where data returned or send might be shorter
    if (Type == USBTRANSFER_TYPE_CONTROL) {
        if (PId == OHCI_TD_IN && Length > 0) {
            Td->Flags |= OHCI_TD_SHORTPACKET_OK;
        }
    } else if (PId == OHCI_TD_IN) {
        Td->Flags |= OHCI_TD_SHORTPACKET_OK;
    }

    // Set the data-toggle?
    if (Toggle) {
        Td->Flags |= OHCI_TD_TOGGLE;
    }

    // Is there bytes to transfer or null packet?
    if (calculatedLength > 0) {
        Td->Cbp       = LODWORD(Address);
        Td->BufferEnd = Td->Cbp + (calculatedLength - 1);
    }

    // Store copy of original content
    Td->OriginalFlags = Td->Flags;
    Td->OriginalCbp   = Td->Cbp;
    return calculatedLength;
}

void
OhciTdDump(
    _In_ OhciController_t*         Controller,
    _In_ OhciTransferDescriptor_t* Td)
{
    uintptr_t PhysicalAddress = 0;

    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_TD_POOL, 
        Td->Object.Index & USB_ELEMENT_INDEX_MASK, NULL, &PhysicalAddress);
    WARNING("TD(0x%x): Link 0x%x, Flags 0x%x, Buffer 0x%x - 0x%x", 
        PhysicalAddress, Td->Link, Td->Flags, Td->Cbp, Td->BufferEnd);
}

void
OhciTdValidate(
    _In_  UsbManagerTransfer_t*     Transfer,
    _In_  OhciTransferDescriptor_t* Td)
{
    int ErrorCount = OHCI_TD_ERRORCOUNT(Td->Flags);
    int ErrorCode  = OHCI_TD_ERRORCODE(Td->Flags);
    int i;

    // Sanitize active status
    if (Td->Flags & OHCI_TD_ACTIVE) {
        // If this one is still active, but it's an transfer that has
        // elements processed - resync toggles
        if (Transfer->Status != TransferInProgress) {
            Transfer->Flags |= TransferFlagSync;
        }
        return;
    }

    // Sanitize the error code
    if ((ErrorCount == 2 && ErrorCode != 0)) {
        Transfer->Status = OhciGetStatusCode(ErrorCode);
    }
    else if (ErrorCode == 0 && Transfer->Status == TransferInProgress) {
        Transfer->Status = TransferFinished;
    }

    // Calculate length transferred 
    // Take into consideration the N-1 
    if (Td->BufferEnd != 0) {
        int BytesTransferred = 0;
        int BytesRequested   = (Td->BufferEnd - Td->OriginalCbp) + 1;
        
        if (Td->Cbp == 0) BytesTransferred = BytesRequested;
        else              BytesTransferred = Td->Cbp - Td->OriginalCbp;

        if (BytesTransferred < BytesRequested) {
            Transfer->Flags |= TransferFlagShort;

            // On short transfers we might have to sync, but only 
            // if there are un-processed td's after this one
            if (Td->Object.DepthIndex != USB_ELEMENT_NO_INDEX) {
                Transfer->Flags |= TransferFlagSync;
            }
        }
        for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
            if (Transfer->Base.Transactions[i].Length > Transfer->Transactions[i].BytesTransferred) {
                Transfer->Transactions[i].BytesTransferred += BytesTransferred;
                break;
            }
        }
    }
}

void
OhciTdSynchronize(
    _In_  UsbManagerTransfer_t*     Transfer,
    _In_  OhciTransferDescriptor_t* Td)
{
    int Toggle = UsbManagerGetToggle(Transfer->DeviceID, &Transfer->Base.Address);

    // Is it neccessary?
    if (Toggle == 1 && (Td->Flags & OHCI_TD_TOGGLE)) {
        return;
    }

    // Clear
    Td->Flags &= ~(OHCI_TD_TOGGLE);
    if (Toggle) {
        Td->Flags |= OHCI_TD_TOGGLE;
    }

    // Update copy
    Td->OriginalFlags = Td->Flags;
    UsbManagerSetToggle(Transfer->DeviceID, &Transfer->Base.Address, Toggle ^ 1);
}

void
OhciTdRestart(
    _In_ OhciController_t*         Controller,
    _In_ UsbManagerTransfer_t*     Transfer,
    _In_ OhciTransferDescriptor_t* Td)
{
    uintptr_t BufferStep;
    uintptr_t BufferBaseUpdated = 0;
    uintptr_t LinkAddress       = 0;
    int       Toggle            = UsbManagerGetToggle(Transfer->DeviceID, &Transfer->Base.Address);

    BufferStep = Transfer->Base.MaxPacketSize;

    // Clear
    Td->OriginalFlags &= ~(OHCI_TD_TOGGLE);
    if (Toggle) {
        Td->OriginalFlags |= OHCI_TD_TOGGLE;
    }
    UsbManagerSetToggle(Transfer->DeviceID, &Transfer->Base.Address, Toggle ^ 1);

    // Adjust buffer if not just restart
    if (Transfer->Status != TransferNAK) {
        BufferBaseUpdated = ADDLIMIT(
                Transfer->Transactions[0].SHMTable.Entries[0].Address, Td->OriginalCbp,
                BufferStep, (Transfer->Transactions[0].SHMTable.Entries[0].Address
                                + Transfer->Transactions[0].SHMTable.Entries[0].Length));
        Td->OriginalCbp   = LODWORD(BufferBaseUpdated);
    }

    // Reset attributes
    Td->Flags     = Td->OriginalFlags;
    Td->Cbp       = Td->OriginalCbp;
    Td->BufferEnd = Td->Cbp + (BufferStep - 1);
    
    // Restore link
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler,
        (Td->Object.DepthIndex >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK, 
        Td->Object.DepthIndex & USB_ELEMENT_INDEX_MASK, NULL, &LinkAddress);
    Td->Link = LinkAddress;
    assert(Td->Link != 0);
}
