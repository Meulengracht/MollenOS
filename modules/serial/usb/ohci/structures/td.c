/**
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
 */

//#define __TRACE
#define __need_minmax
#include <assert.h>
#include <ddk/utils.h>
#include <os/mollenos.h>
#include <os/memory.h>
#include "../ohci.h"

void
OHCITDSetup(
    _In_ OhciTransferDescriptor_t* td,
    _In_ uintptr_t                 dataAddress)
{
    // Set no link
    td->Link = 0;

    // Initialize the Td flags, no toggle for SETUP
    // packet
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

void
OHCITDData(
    _In_ OhciTransferDescriptor_t* td,
    _In_ enum USBTransferType      type,
    _In_ uint32_t                  PID,
    _In_ uintptr_t                 dataAddress,
    _In_ size_t                    length,
    _In_ int                       toggle)
{
    size_t pageSize = MemoryPageSize();
    size_t calculatedLength;
    
    TRACE("OHCITDData(Type %u, Id %u, Toggle %i, Address 0x%x, Length 0x%x",
          type, PID, Toggle, dataAddress, length);

    // We can encompass a maximum of two pages (when the initial page offset is 0)
    // so make sure we correct for that limit
    calculatedLength = MIN(((2 * pageSize) - (dataAddress & (pageSize - 1))), length);

    // Set this is as end of chain
    td->Link = 0;

    // Initialize flags as a IO Td
    td->Flags |= PID;
    td->Flags |= OHCI_TD_IOC_NONE;
    td->Flags |= OHCI_TD_TOGGLE_LOCAL;
    td->Flags |= OHCI_TD_ACTIVE;

    if (toggle) {
        td->Flags |= OHCI_TD_TOGGLE;
    }

    // We have to allow short-packets in some cases
    // where data returned or send might be shorter
    if (type == USBTRANSFER_TYPE_CONTROL) {
        if (PID == OHCI_TD_IN && length > 0) {
            td->Flags |= OHCI_TD_SHORTPACKET_OK;
        }
    } else if (PID == OHCI_TD_IN) {
        td->Flags |= OHCI_TD_SHORTPACKET_OK;
    }

    // Is there bytes to transfer or null packet?
    if (calculatedLength > 0) {
        td->Cbp       = LODWORD(dataAddress);
        td->BufferEnd = td->Cbp + (calculatedLength - 1);
    }

    // Store copy of original content
    td->OriginalFlags = td->Flags;
    td->OriginalCbp   = td->Cbp;
}

void
OHCITDDump(
    _In_ OhciController_t*         controller,
    _In_ OhciTransferDescriptor_t* td)
{
    uintptr_t physicalAddress = 0;

    UsbSchedulerGetPoolElement(
            controller->Base.Scheduler,
            OHCI_TD_POOL,
            td->Object.Index & USB_ELEMENT_INDEX_MASK,
            NULL,
            &physicalAddress
    );
    WARNING("TD(0x%x): Link 0x%x, Flags 0x%x, Buffer 0x%x - 0x%x",
            physicalAddress, td->Link, td->Flags, td->Cbp, td->BufferEnd);
}

void
OHCITDVerify(
        _In_ struct HCIProcessReasonScanContext* scanContext,
        _In_ OhciTransferDescriptor_t*           td)
{
    int errorCount;
    int cc;

    // Have we already processed this one? In that case we ignore
    // it completely.
    if (td->Object.Flags & USB_ELEMENT_PROCESSED) {
        scanContext->ElementsExecuted++;
        return;
    }

    errorCount = OHCI_TD_ERRORCOUNT(td->Flags);
    cc         = OHCI_TD_ERRORCODE(td->Flags);

    // If the TD is still active do nothing.
    if (cc == OHCI_CC_INIT) {
        return;
    }

    if (errorCount == 2) {
        if (cc != 0) {
            scanContext->Result = OHCIErrorCodeToTransferStatus(cc);
        }
    } else if (cc != 0) {
        // error has occurred but all attempts not exhausted.
        return;
    }

    // Calculate length transferred 
    // Take into consideration the N-1 
    if (__Transfer_IsAsync(scanContext->Transfer) && td->BufferEnd != 0) {
        int bytesTransferred;
        int bytesRequested = (int)(td->BufferEnd - td->OriginalCbp) + 1;

        if (td->Cbp == 0) bytesTransferred = bytesRequested;
        else              bytesTransferred = (int)(td->Cbp - td->OriginalCbp);

        if (bytesTransferred < bytesRequested) {
            scanContext->Short = true;
        }
        scanContext->BytesTransferred += bytesTransferred;
    }

    // mark TD as processed, and retrieve the data-toggle
    td->Object.Flags |= USB_ELEMENT_PROCESSED;
    scanContext->LastToggle = (td->Flags & OHCI_TD_TOGGLE) ? 1 : 0;
    scanContext->ElementsProcessed++;
    scanContext->ElementsExecuted++;
}

void
OhciTdSynchronize(
    _In_  UsbManagerTransfer_t*     Transfer,
    _In_  OhciTransferDescriptor_t* Td)
{
    int Toggle = UsbManagerGetToggle(Transfer->DeviceID, &Transfer->Address);

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
    UsbManagerSetToggle(Transfer->DeviceID, &Transfer->Address, Toggle ^ 1);
}

void
OHCITDRestart(
    _In_ OhciController_t*         Controller,
    _In_ UsbManagerTransfer_t*     Transfer,
    _In_ OhciTransferDescriptor_t* Td)
{
    uintptr_t BufferStep;
    uintptr_t LinkAddress = 0;
    int       Toggle      = UsbManagerGetToggle(Transfer->DeviceID, &Transfer->Address);

    BufferStep = Transfer->MaxPacketSize;

    Td->OriginalFlags &= ~(OHCI_TD_TOGGLE);
    if (Toggle) {
        Td->OriginalFlags |= OHCI_TD_TOGGLE;
    }
    UsbManagerSetToggle(Transfer->DeviceID, &Transfer->Address, Toggle ^ 1);

    // Adjust buffer if not just restart
    if (Transfer->Status != TransferNAK) {
        uintptr_t bufferBaseUpdated = ADDLIMIT(
                Transfer->Elements[0].DataAddress,
                Td->OriginalCbp,
                BufferStep,
                (Transfer->Elements[0].DataAddress + Transfer->Elements[0].Length)
        );
        Td->OriginalCbp = LODWORD(bufferBaseUpdated);
    }

    // Reset attributes
    Td->Flags     = Td->OriginalFlags;
    Td->Cbp       = Td->OriginalCbp;
    Td->BufferEnd = Td->Cbp + (BufferStep - 1);
    
    // Restore link
    UsbSchedulerGetPoolElement(
            Controller->Base.Scheduler,
            (Td->Object.DepthIndex >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK,
            Td->Object.DepthIndex & USB_ELEMENT_INDEX_MASK,
            NULL,
            &LinkAddress
    );
    Td->Link = LinkAddress;
    assert(Td->Link != 0);
}
