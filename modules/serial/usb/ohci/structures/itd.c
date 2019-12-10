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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

#include <os/mollenos.h>
#include <ddk/utils.h>
#include "../ohci.h"
#include <assert.h>
#include <string.h>

void
OhciTdIsochronous(
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
    
    // Debug
    TRACE("OhciTdIsochronous(Id %u, Address 0x%x, Length 0x%x", 
        PId, Address, Length);

    // Max packet size is 1023 for isoc
    // If direction is out and mod 1023 is 0
    // add a zero-length frame
    // If framecount is > 8, nono
    if (FrameCount > 8) {
        FrameCount = 8;
    }

    // Initialize flags
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

    // Set this is as end of chain
    Td->Link = 0;

    // Store copy of original content
    Td->OriginalFlags     = Td->Flags;
    Td->OriginalCbp       = Td->Cbp;
    Td->OriginalBufferEnd = Td->BufferEnd;
}

void
OhciiTdDump(
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
OhciiTdValidate(
    _In_ UsbManagerTransfer_t*         Transfer,
    _In_ OhciIsocTransferDescriptor_t* Td)
{
    int i;

    // Sanitize active status
    if (Td->Flags & OHCI_TD_ACTIVE) {
        // If this one is still active, but it's an transfer that has
        // elements processed - resync toggles
        if (Transfer->Status != TransferQueued) {
            Transfer->Flags |= TransferFlagSync;
        }
        return;
    }

    // Sanitize the error codes
    for (i = 0; i < 8; i++) {
        int ErrorCode = OHCI_iTD_OFFSETCODE(Td->Offsets[i]);
        if (Td->Offsets[i] == 0) {
            break;
        }

        if (ErrorCode != 0) {
            Transfer->Status = OhciGetStatusCode(ErrorCode);
        }
        else if (Transfer->Status == TransferQueued) {
            Transfer->Status = TransferFinished;
        }
    }

    // Calculate length transferred 
    // Take into consideration the N-1 
    if (Td->BufferEnd != 0) {
        int BytesTransferred    = 0;
        int BytesRequested      = (Td->BufferEnd - Td->OriginalCbp) + 1;
        if (Td->Cbp == 0)       BytesTransferred = BytesRequested;
        else                    BytesTransferred = Td->Cbp - Td->OriginalCbp;

        if (BytesTransferred < BytesRequested) {
            Transfer->Flags |= TransferFlagShort;

            // On short transfers we might have to sync, but only 
            // if there are un-processed td's after this one
            if (Td->Object.DepthIndex != USB_ELEMENT_NO_INDEX) {
                Transfer->Flags |= TransferFlagSync;
            }
        }
        for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
            if (Transfer->Transfer.Transactions[i].Length > Transfer->Transactions[i].BytesTransferred) {
                Transfer->Transactions[i].BytesTransferred += BytesTransferred;
                break;
            }
        }
    }
}

void
OhciiTdRestart(
    _In_ OhciController_t*             Controller,
    _In_ UsbManagerTransfer_t*         Transfer,
    _In_ OhciIsocTransferDescriptor_t* Td)
{
    uintptr_t LinkAddress = 0;
    int       i;

    // Reset offsets
    for (i = 0; i < 8; i++) {
        Td->Offsets[i]      = Td->OriginalOffsets[i];
    }

    // Reset rest
    Td->Flags               = Td->OriginalFlags;
    Td->Cbp                 = Td->OriginalCbp;
    Td->BufferEnd           = Td->OriginalBufferEnd;
    
    // Restore link
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler,
        (Td->Object.DepthIndex >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK, 
        Td->Object.DepthIndex & USB_ELEMENT_INDEX_MASK, NULL, &LinkAddress);
    Td->Link = LinkAddress;
    assert(Td->Link != 0);
}
