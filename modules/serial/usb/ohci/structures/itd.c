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
 */

//#define __TRACE
#define __need_minmax
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "../ohci.h"
#include <assert.h>
#include <string.h>

void
OHCITDIsochronous(
    _In_ OhciIsocTransferDescriptor_t* Td,
    _In_ size_t                        MaxPacketSize,
    _In_ uint32_t                      PId,
    _In_ uintptr_t                     Address,
    _In_ size_t                        Length)
{
    size_t BytesToTransfer = Length;
    size_t BufferOffset    = 0;
    int    FrameCount      = DIVUP(Length, MaxPacketSize);
    int    FrameIndex      = 0;
    int    Crossed         = 0;

    TRACE("OHCITDIsochronous(Id %u, Address 0x%x, Length 0x%x",
        PId, Address, Length);

    // Max packet size is 1023 for isoc
    // If direction is out and mod 1023 is 0
    // add a zero-length frame
    // If framecount is > 8, nono
    if (FrameCount > 8) {
        FrameCount = 8;
    }

    Td->Flags |= PId;
    Td->Flags |= OHCI_iTD_FRAMECOUNT((FrameCount - 1));
    Td->Flags |= OHCI_TD_IOC_NONE;
    Td->Flags |= OHCI_TD_ACTIVE;

    // Initialize buffer access
    Td->Cbp       = LODWORD(Address);
    Td->BufferEnd = Td->Cbp + Length - 1;

    // Iterate frames and setup
    while (BytesToTransfer) {
        // Set offset 0 and increase bufitr
        size_t BytesStep        = MIN(BytesToTransfer, MaxPacketSize);
        Td->Offsets[FrameIndex] = BufferOffset;
        Td->Offsets[FrameIndex] |= ((Crossed & 0x1) << 12);
        Td->OriginalOffsets[FrameIndex] = Td->Offsets[FrameIndex];
        BufferOffset += BytesStep;

        // Sanity on page-crossover
        if (((Address + BufferOffset) & 0xFFFFF000) != (Address & 0xFFFFF000)) {
            BufferOffset = (Address + BufferOffset) & 0xFFF; // Reset offset
            Crossed      = 1;
        }

        // Update iterators
        BytesToTransfer--;
        FrameIndex++;
    }

    /**
     * When calling these initializers for ITDs, the ITDs have already been
     * correctly linked, and their links set accordingly. So we do not touch
     * the links, unless they need a specific hardcoded value.
     */

    // Store copy of original content
    Td->OriginalFlags     = Td->Flags;
    Td->OriginalCbp       = Td->Cbp;
    Td->OriginalBufferEnd = Td->BufferEnd;
}

void
OHCIITDDump(
    _In_ OhciController_t*              Controller,
    _In_ OhciIsocTransferDescriptor_t*  Td)
{
    uintptr_t PhysicalAddress = 0;

    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_iTD_POOL, 
        Td->Object.Index & USB_ELEMENT_INDEX_MASK, NULL, &PhysicalAddress);
    WARNING("iTD(0x%x): Link 0x%x, Flags 0x%x, Header 0x%x, Buffer 0x%x", 
        PhysicalAddress, Td->Link, Td->Flags, Td->Cbp, Td->BufferEnd);
}

void
OHCIITDVerify(
        _In_ struct HCIProcessReasonScanContext* scanContext,
        _In_ OhciIsocTransferDescriptor_t*       iTD)
{
    // Have we already processed this one? In that case we ignore
    // it completely.
    if (iTD->Object.Flags & USB_ELEMENT_PROCESSED) {
        scanContext->ElementsExecuted++;
        return;
    }

    // For isochronous transfers we don't really care that much about how much
    // data was transferred, or whether toggles need to be resynced. Instead, we
    // just need to know whether the transfer completed correctly.
    for (int i = 0; i < 8; i++) {
        int cc = OHCI_iTD_OFFSETCODE(iTD->Offsets[i]);
        if (iTD->Offsets[i] == 0) {
            break;
        }
        if (cc != 0 && cc != OHCI_CC_INIT) {
            scanContext->Result = OHCIErrorCodeToTransferStatus(cc);
        }
    }

    // mark TD as processed
    iTD->Object.Flags |= USB_ELEMENT_PROCESSED;
    scanContext->ElementsProcessed++;
    scanContext->ElementsExecuted++;
}

void
OHCIITDRestart(
    _In_ OhciController_t*             controller,
    _In_ UsbManagerTransfer_t*         transfer,
    _In_ OhciIsocTransferDescriptor_t* iTD)
{
    uintptr_t linkAddress = 0;

    // Make sure we clear the PROCESSED status on the TD, otherwise it
    // won't be processed again
    iTD->Object.Flags &= ~(USB_ELEMENT_PROCESSED);

    for (int i = 0; i < 8; i++) {
        iTD->Offsets[i] = iTD->OriginalOffsets[i];
    }

    iTD->Flags     = iTD->OriginalFlags;
    iTD->Cbp       = iTD->OriginalCbp;
    iTD->BufferEnd = iTD->OriginalBufferEnd;

    UsbSchedulerGetPoolElement(
            controller->Base.Scheduler,
            (iTD->Object.DepthIndex >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK,
            iTD->Object.DepthIndex & USB_ELEMENT_INDEX_MASK,
            NULL,
            &linkAddress
    );
    iTD->Link = linkAddress;
    assert(iTD->Link != 0);
}
