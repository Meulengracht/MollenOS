/**
 * Copyright 2023, Philip Meulengracht
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
#include <os/memory.h>
#include "../ohci.h"

void
OHCITDSetup(
        _In_ OhciTransferDescriptor_t* td,
        _In_ struct TransferElement*   element)
{
    TRACE("OHCITDSetup(dataAddress=0x%x)", element->Data.OHCI.Page0 + element->Data.OHCI.Offsets[0]);

    // The TD must already be linked before initialization. Leave its links intact.

    // SETUP packets use DATA0.
    td->Flags = OHCI_TD_SETUP;
    td->Flags |= OHCI_TD_IOC_NONE;
    td->Flags |= OHCI_TD_TOGGLE_LOCAL;
    td->Flags |= OHCI_TD_ACTIVE;

    // Set the buffer addresses.
    td->Cbp       = element->Data.OHCI.Page0 + element->Data.OHCI.Offsets[0];
    td->BufferEnd = element->Data.OHCI.Page1;

    // Save the flags and buffer pointer for restart.
    td->OriginalFlags = td->Flags;
    td->OriginalCbp   = td->Cbp;
}

void
OHCITDData(
        _In_ OhciTransferDescriptor_t*  td,
        _In_ enum USBTransferType       type,
        _In_ uint32_t                   pid,
        _In_ struct TransferElement*    element,
        _In_ int                        toggle)
{
    TRACE("OHCITDData(length=%u)", element->Length);

    // The TD must already be linked before initialization. Leave its links intact.

    // Initialize the data TD flags.
    td->Flags = pid;
    td->Flags |= OHCI_TD_IOC_NONE;
    td->Flags |= OHCI_TD_TOGGLE_LOCAL;
    td->Flags |= OHCI_TD_ACTIVE;

    if (toggle) {
        td->Flags |= OHCI_TD_TOGGLE;
    }

    // Allow short IN packets. For control transfers, this applies only
    // to the data stage.
    if (type == USBTRANSFER_TYPE_CONTROL) {
        if (pid == OHCI_TD_IN && element->Length > 0) {
            td->Flags |= OHCI_TD_SHORTPACKET_OK;
        }
    } else if (pid == OHCI_TD_IN) {
        td->Flags |= OHCI_TD_SHORTPACKET_OK;
    }

    td->Cbp = 0;
    td->BufferEnd = 0;

    // Leave both buffer addresses zero for a zero-length packet.
    if (element->Length > 0) {
        // When CurrentBufferPointer crosses a 4K boundary, the controller copies
        // the upper 20 bits of BufferEnd into its working pointer. Transfer then
        // continues at offset zero in the page containing the last buffer byte.
        // This can happen within a packet.
        td->Cbp       = element->Data.OHCI.Page0 + element->Data.OHCI.Offsets[0];
        td->BufferEnd = element->Data.OHCI.Page1;
    }

    // Save the flags and buffer pointer for restart.
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
    TRACE("OHCITDVerify()");

    // Count previously processed TDs without adding their bytes again.
    if (td->Object.Flags & USB_ELEMENT_PROCESSED) {
        TRACE("OHCITDVerify: already processed, skipping");
        scanContext->ElementsExecuted++;
        return;
    }

    errorCount = OHCI_TD_ERRORCOUNT(td->Flags);
    cc         = OHCI_TD_ERRORCODE(td->Flags);

    // If the TD is still active, leave it for the next scan.
    if (cc == OHCI_CC_INIT || cc == OHCI_CC_INIT0) {
        TRACE("OHCITDVerify: td is still active");
        return;
    }

    if (errorCount == 3) {
        TRACE("OHCITDVerify: td failed to execute: %i", cc);
        scanContext->Result = OHCIErrorCodeToTransferStatus(cc);
    } else if (cc != 0) {
        scanContext->Result = OHCIErrorCodeToTransferStatus(cc);
    }

    // BufferEnd points to the last byte, so include it in the requested length.
    if (td->BufferEnd != 0) {
        uint32_t bytesTransferred;
        uint32_t bytesRequested;

        // A buffer spanning two pages may use non-contiguous physical pages.
        if (__XPAGE(td->OriginalCbp) != __XPAGE(td->BufferEnd)) {
            uint32_t bytesPage0 = __XUPPAGE(td->OriginalCbp) - td->OriginalCbp;
            uint32_t bytesPage1 = __XPAGEOFFSET(td->BufferEnd);
            bytesRequested = bytesPage0 + bytesPage1 + 1;

            if (td->Cbp == 0) {
                bytesTransferred = bytesRequested;
            } else {
                // For a partial transfer, locate Cbp in the first or second page.
                if (__XPAGE(td->Cbp) == __XPAGE(td->OriginalCbp)) {
                    bytesTransferred = td->Cbp - td->OriginalCbp;
                } else {
                    // The transfer reached the second page.
                    bytesTransferred = bytesPage0 + __XPAGEOFFSET(td->Cbp);
                }
            }
        } else {
            // The buffer fits within one page.
            bytesRequested = (td->BufferEnd - td->OriginalCbp) + 1;
            if (td->Cbp == 0) {
                bytesTransferred = bytesRequested;
            } else {
                bytesTransferred = td->Cbp - td->OriginalCbp;
            }
        }
        TRACE("OHCITDVerify: td has transferred %i bytes", bytesTransferred);

        if (bytesTransferred < bytesRequested) {
            scanContext->Short = true;
        }
        scanContext->BytesTransferred += bytesTransferred;
    }

    // Mark the TD as processed and record its data toggle.
    td->Object.Flags |= USB_ELEMENT_PROCESSED;
    scanContext->LastToggle = (td->Flags & OHCI_TD_TOGGLE) ? 1 : 0;
    scanContext->ElementsProcessed++;
    scanContext->ElementsExecuted++;
}

void
OHCITDRestart(
        _In_ OhciController_t*         controller,
        _In_ UsbManagerTransfer_t*     transfer,
        _In_ OhciTransferDescriptor_t* td)
{
    uintptr_t linkAddress = 0;
    int       toggle      = UsbManagerGetToggle(&controller->Base, &transfer->Address);

    // Clear PROCESSED so the next scan checks this TD again.
    td->Object.Flags &= ~(USB_ELEMENT_PROCESSED);

    td->OriginalFlags &= ~(OHCI_TD_TOGGLE);
    if (toggle) {
        td->OriginalFlags |= OHCI_TD_TOGGLE;
    }
    UsbManagerSetToggle(&controller->Base, &transfer->Address, toggle ^ 1);

    if (transfer->Type == USBTRANSFER_TYPE_INTERRUPT && transfer->ResultCode != USBTRANSFERCODE_NAK) {
        uintptr_t bufferStep = transfer->MaxPacketSize;
        uintptr_t bufferBaseUpdated = ADDLIMIT(
                                              transfer->Elements[0].Data.Address,
                                              td->OriginalCbp,
                                              bufferStep,
                                              (transfer->Elements[0].Data.Address + transfer->Elements[0].Length)
        );
        td->OriginalCbp = LODWORD(bufferBaseUpdated);
        td->BufferEnd   = td->Cbp + (bufferStep - 1);
    }

    // Restore the flags and buffer pointer.
    td->Flags = td->OriginalFlags;
    td->Cbp   = td->OriginalCbp;
    
    // Restore the link from the scheduler's depth index.
    UsbSchedulerGetPoolElement(
            controller->Base.Scheduler,
            (td->Object.DepthIndex >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK,
            td->Object.DepthIndex & USB_ELEMENT_INDEX_MASK,
            NULL,
            &linkAddress
    );
    td->Link = linkAddress;
    assert(td->Link != 0);
}
