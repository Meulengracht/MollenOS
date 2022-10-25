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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 * - Transaction Translator Support
 */
//#define __TRACE
#define __need_minmax
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "../ehci.h"
#include <assert.h>
#include <string.h>

oserr_t
EhciQhInitialize(
    _In_ EhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer,
    _In_ uint8_t               deviceAddress,
    _In_ uint8_t               endpointAddress)
{
    EhciQueueHead_t* Qh          = (EhciQueueHead_t*)transfer->EndpointDescriptor;
    oserr_t       Status      = OS_EOK;
    size_t           EpBandwidth = MAX(3, transfer->Transfer.PeriodicBandwith);

    // Initialize links
    Qh->LinkPointer               = EHCI_LINK_END;
    Qh->Overlay.NextTD            = EHCI_LINK_END;
    Qh->Overlay.NextAlternativeTD = EHCI_LINK_END;

    // Initialize link flags
    Qh->Object.Flags |= EHCI_LINK_QH;

    // Initialize the QH
    Qh->Flags  = EHCI_QH_DEVADDR(deviceAddress);
    Qh->Flags |= EHCI_QH_EPADDR(endpointAddress);
    Qh->Flags |= EHCI_QH_MAXLENGTH(transfer->Transfer.MaxPacketSize); // MIN(TransferLength, MPS)?
    Qh->Flags |= EHCI_QH_DTC;
    if (transfer->Transfer.Type == USB_TRANSFER_INTERRUPT) {
        Qh->State = EHCI_QH_MULTIPLIER(EpBandwidth);
    }
    else {
        Qh->State = EHCI_QH_MULTIPLIER(1);
    }

    // Now, set additionals depending on speed
    if (transfer->Transfer.Speed == USB_SPEED_LOW || transfer->Transfer.Speed == USB_SPEED_FULL) {
        if (transfer->Transfer.Type == USB_TRANSFER_CONTROL) {
            Qh->Flags |= EHCI_QH_CONTROLEP;
        }

        // On low-speed, set this bit
        if (transfer->Transfer.Speed == USB_SPEED_LOW) {
            Qh->Flags |= EHCI_QH_LOWSPEED;
        }

        // Set nak-throttle to 0
        Qh->Flags |= EHCI_QH_RL(0);

        // We need to fill the TT's hub-address and port-address
        Qh->State |= EHCI_QH_HUBADDR(transfer->Transfer.Address.HubAddress);
        Qh->State |= EHCI_QH_PORT(transfer->Transfer.Address.PortAddress);
    }
    else {
        // High speed device, no transaction translator
        Qh->Flags |= EHCI_QH_HIGHSPEED;

        // Set nak-throttle to 4 if control or bulk
        if (transfer->Transfer.Type == USB_TRANSFER_CONTROL || 
            transfer->Transfer.Type == USB_TRANSFER_BULK) {
            Qh->Flags |= EHCI_QH_RL(4);
        }
        else {
            Qh->Flags |= EHCI_QH_RL(0);
        }
    }
    
    // Allocate bandwith if interrupt qh
    if (transfer->Transfer.Type == USB_TRANSFER_INTERRUPT) {
        // If we use completion masks we'll need another transfer for start
        size_t BytesToTransfer = transfer->Transfer.Transactions[0].Length;
        if (transfer->Transfer.Speed != USB_SPEED_HIGH) {
            BytesToTransfer += transfer->Transfer.MaxPacketSize;
        }

        // Allocate the bandwidth
        Status = UsbSchedulerAllocateBandwidth(controller->Base.Scheduler, 
            transfer->Transfer.PeriodicInterval, transfer->Transfer.MaxPacketSize,
            transfer->Transfer.Transactions[0].Type, BytesToTransfer,
            transfer->Transfer.Type, transfer->Transfer.Speed, (uint8_t*)Qh);
        if (Status == OS_EOK) {
            // Calculate both the frame start and completion mask
            // If the transfer was to spand over a boundary, starting with subframes in
            // one frame, ending with subframes in next frame, we would have to use
            // FSTN links for low/full speed interrupt transfers. But as the allocator
            // works this never happens as it only allocates in same frame. If this
            // changes we need to update this @todo
            Qh->FrameStartMask  = (uint8_t)FirstSetBit(Qh->Object.FrameMask);
            if (transfer->Transfer.Speed != USB_SPEED_HIGH) {
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
