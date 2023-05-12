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
        _In_ OhciIsocTransferDescriptor_t* iTd,
        _In_ size_t                        maxPacketSize,
        _In_ uint32_t                      pid,
        _In_ struct TransferElement*       element)
{
    int frameCount = DIVUP(element->Length, maxPacketSize);

    TRACE("OHCITDIsochronous(Id %u, Address 0x%x, Length 0x%x",
        PId, Address, Length);

    // Max packet size is 1023 for isoc
    // If direction is out and mod 1023 is 0
    // add a zero-length frame
    // If framecount is > 8, nono
    if (frameCount > 8) {
        frameCount = 8;
    }

    iTd->Flags |= pid;
    iTd->Flags |= OHCI_iTD_FRAMECOUNT((frameCount - 1));
    iTd->Flags |= OHCI_TD_IOC_NONE;
    iTd->Flags |= OHCI_TD_ACTIVE;

    iTd->Cbp       = element->Data.OHCI.Page0;
    iTd->BufferEnd = element->Data.OHCI.Page1;
    for (int i = 0; i < frameCount; i++) {
        iTd->Offsets[i] = element->Data.OHCI.Offsets[i];
        iTd->OriginalOffsets[i] = element->Data.OHCI.Offsets[i];
    }

    /**
     * When calling these initializers for ITDs, the ITDs have already been
     * correctly linked, and their links set accordingly. So we do not touch
     * the links, unless they need a specific hardcoded value.
     */

    // Store copy of original content
    iTd->OriginalFlags     = iTd->Flags;
    iTd->OriginalCbp       = iTd->Cbp;
    iTd->OriginalBufferEnd = iTd->BufferEnd;
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
