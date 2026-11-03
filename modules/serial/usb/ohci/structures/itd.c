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

    // Maximum isochronous packet size: 1023 bytes.
    // For OUT transfers, add a zero-length frame when the length is a multiple of 1023.
    // Limit each ITD to eight frames.
    if (frameCount > 8) {
        frameCount = 8;
    }

    _CRT_UNUSED(pid);
    iTd->Flags &= 0xFFFF;
    iTd->Flags |= OHCI_iTD_FRAMECOUNT((frameCount - 1));
    iTd->Flags |= OHCI_TD_IOC_NONE;
    iTd->Flags |= OHCI_TD_ACTIVE;

    iTd->Cbp       = element->Data.OHCI.Page0;
    iTd->BufferEnd = element->Data.OHCI.Page1;
    for (int i = 0; i < frameCount; i++) {
        iTd->Offsets[i] = element->Data.OHCI.Offsets[i] | 0xE000;
        iTd->OriginalOffsets[i] = iTd->Offsets[i];
    }

    // The ITD must already be linked before initialization. Leave its links unchanged.

    // Save the initial fields for OHCIITDRestart.
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
    int count = ((iTD->OriginalFlags >> 24) & 7) + 1;
    int cc;

    if (iTD->Object.Flags & USB_ELEMENT_PROCESSED) {
        scanContext->ElementsExecuted++;
        return;
    }
    
    if (OHCI_TD_ERRORCODE(iTD->Flags) >= OHCI_CC_INIT0) {
        return;
    }

    // All packets must be processed before the ITD can be marked complete.
    for (int i = 0; i < count; i++) {
        if (OHCI_iTD_OFFSETCODE(iTD->Offsets[i]) >= OHCI_CC_INIT0) {
            return;
        }
    }

    for (int i = 0; i < count; i++) {
        cc = OHCI_iTD_OFFSETCODE(iTD->Offsets[i]);
        if (cc != 0) {
            scanContext->Result = OHCIErrorCodeToTransferStatus(cc);
            break;
        }
        scanContext->BytesTransferred += OHCI_iTD_OFFSETLENGTH(iTD->Offsets[i]);
    }

    // Mark the TD as processed.
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

    // Clear PROCESSED so OHCIITDVerify checks the TD on the next transfer.
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
