/* MollenOS
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
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 * - Transaction Translator Support
 */
//#define __TRACE

/* Includes
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>
#include "../ehci.h"

/* Includes
 * - Library */
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/* EhciQhInitialize
 * This initiates any periodic scheduling information that might be needed */
OsStatus_t
EhciQhInitialize(
    _In_ EhciController_t*          Controller,
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ size_t                     Address,
    _In_ size_t                     Endpoint)
{
    // Variables
    EhciQueueHead_t *Qh = (EhciQueueHead_t*)Transfer->EndpointDescriptor;
    OsStatus_t Status   = OsSuccess;
    size_t EpBandwidth  = MAX(3, Transfer->Transfer.Endpoint.Bandwidth);

    // Initialize links
    Qh->LinkPointer     = EHCI_LINK_END;
    Qh->Overlay.NextTD  = EHCI_LINK_END;
    Qh->Overlay.NextAlternativeTD = EHCI_LINK_END;

    // Initialize link flags
    Qh->Object.Flags    |= EHCI_LINK_QH;

    // Initialize the QH
    Qh->Flags            = EHCI_QH_DEVADDR(Address);
    Qh->Flags           |= EHCI_QH_EPADDR(Endpoint);
    Qh->Flags           |= EHCI_QH_DTC;
    Qh->Flags           |= EHCI_QH_MAXLENGTH(Transfer->Transfer.Endpoint.MaxPacketSize); // MIN(TransferLength, MPS)?
    if (Transfer->Transfer.Type == InterruptTransfer) {
        Qh->State = EHCI_QH_MULTIPLIER(EpBandwidth);
    }
    else {
        Qh->State = EHCI_QH_MULTIPLIER(1);
    }

    // Now, set additionals depending on speed
    if (Transfer->Transfer.Speed == LowSpeed || Transfer->Transfer.Speed == FullSpeed) {
        if (Transfer->Transfer.Type == ControlTransfer) {
            Qh->Flags |= EHCI_QH_CONTROLEP;
        }

        // On low-speed, set this bit
        if (Transfer->Transfer.Speed == LowSpeed) {
            Qh->Flags |= EHCI_QH_LOWSPEED;
        }

        // Set nak-throttle to 0
        Qh->Flags |= EHCI_QH_RL(0);

        // We need to fill the TT's hub-address and port-address
        Qh->State |= EHCI_QH_HUBADDR(Transfer->Transfer.Address.HubAddress);
        Qh->State |= EHCI_QH_PORT(Transfer->Transfer.Address.PortAddress);
    }
    else {
        // High speed device, no transaction translator
        Qh->Flags |= EHCI_QH_HIGHSPEED;

        // Set nak-throttle to 4 if control or bulk
        if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
            Qh->Flags |= EHCI_QH_RL(4);
        }
        else {
            Qh->Flags |= EHCI_QH_RL(0);
        }
    }
    
    // Allocate bandwith if interrupt qh
    if (Transfer->Transfer.Type == InterruptTransfer) {
        // If we use completion masks we'll need another transfer for start
        size_t BytesToTransfer = Transfer->Transfer.Transactions[0].Length;
        if (Transfer->Transfer.Speed != HighSpeed) {
            BytesToTransfer += Transfer->Transfer.Endpoint.MaxPacketSize;
        }

        // Allocate the bandwidth
        Status = UsbSchedulerAllocateBandwidth(Controller->Base.Scheduler, 
            &Transfer->Transfer.Endpoint, BytesToTransfer,
            Transfer->Transfer.Type, Transfer->Transfer.Speed, (uint8_t*)Qh);
        if (Status == OsSuccess) {
            // Calculate both the frame start and completion mask
            // If the transfer was to spand over a boundary, starting with subframes in
            // one frame, ending with subframes in next frame, we would have to use
            // FSTN links for low/full speed interrupt transfers. But as the allocator
            // works this never happens as it only allocates in same frame. If this
            // changes we need to update this @todo
            Qh->FrameStartMask  = (uint8_t)FirstSetBit(Qh->Object.FrameMask);
            if (Transfer->Transfer.Speed != HighSpeed) {
                Qh->FrameCompletionMask = (uint8_t)(Qh->Object.FrameMask & 0xFF);
                Qh->FrameCompletionMask &= ~(1 << Qh->FrameStartMask);
            }
            else {
                Qh->FrameCompletionMask = 0;
            }
        }
    }
    return Status;
}

/* EhciQhDump
 * Dumps the information contained in the queue-head by writing it to stdout */
void
EhciQhDump(
    _In_ EhciController_t*          Controller,
    _In_ EhciQueueHead_t*           Qh)
{
    // Variables
    uintptr_t PhysicalAddress   = 0;

    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, EHCI_QH_POOL, 
        Qh->Object.Index & USB_ELEMENT_INDEX_MASK, NULL, &PhysicalAddress);
    WARNING("EHCI: QH at 0x%x, Current 0x%x, NextQh 0x%x", PhysicalAddress, Qh->Current, Qh->LinkPointer);
    WARNING("      Bandwidth %u, StartFrame %u, Flags 0x%x", Qh->Object.Bandwidth, Qh->Object.StartFrame, Qh->Flags);
    WARNING("      .NextTd 0x%x, .AltTd 0x%x, .Status 0x%x", Qh->Overlay.NextTD, Qh->Overlay.NextAlternativeTD, Qh->Overlay.Status);
    WARNING("      .Token 0x%x, .Length 0x%x, .Buffers[0] 0x%x", Qh->Overlay.Token, Qh->Overlay.Length, Qh->Overlay.Buffers[0]);
    WARNING("      .Buffers[1] 0x%x, .Buffers[2] 0x%x", Qh->Overlay.Buffers[1], Qh->Overlay.Buffers[2]);
}

/* EhciQhRestart
 * Restarts an interrupt QH by resetting it to it's start state */
void
EhciQhRestart(
    EhciController_t*               Controller, 
    UsbManagerTransfer_t*           Transfer)
{
    // Variables
    EhciQueueHead_t *Qh             = (EhciQueueHead_t*)Transfer->EndpointDescriptor;
    uintptr_t LinkPhysical          = 0;

    memset(&Qh->Overlay, 0, sizeof(EhciQueueHeadOverlay_t));

    // Update links
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, EHCI_TD_POOL, 
        Qh->Object.DepthIndex & USB_ELEMENT_INDEX_MASK, NULL, &LinkPhysical);
    Qh->Overlay.NextTD              = LinkPhysical;
    Qh->Overlay.NextAlternativeTD   = EHCI_LINK_END;
}
