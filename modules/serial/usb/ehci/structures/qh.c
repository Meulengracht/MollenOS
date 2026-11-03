/**
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
 */

//#define __TRACE
#define __need_minmax
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "../ehci.h"
#include <assert.h>
#include <string.h>

oserr_t
EHCIQHInitialize(
        _In_ EhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer,
        _In_ EhciQueueHead_t*      qh,
        _In_ uint8_t               deviceAddress,
        _In_ uint8_t               endpointAddress)
{
    oserr_t          oserr     = OS_EOK;

    // Initialize links
    qh->LinkPointer               = EHCI_LINK_END;
    qh->Overlay.NextTD            = EHCI_LINK_END;
    qh->Overlay.NextAlternativeTD = EHCI_LINK_END;

    // Initialize link flags
    qh->Object.Flags |= EHCI_LINK_QH;

    // Initialize the QH
    qh->Flags = EHCI_QH_DEVADDR(deviceAddress);
    qh->Flags |= EHCI_QH_EPADDR(endpointAddress);
    qh->Flags |= EHCI_QH_MAXLENGTH(transfer->MaxPacketSize); // MIN(TransferLength, MPS)?
    qh->Flags |= EHCI_QH_DTC;
    if (__Transfer_IsPeriodic(transfer)) {
        uint32_t bandwidth = MIN(3, transfer->TData.Periodic.Bandwith + 1);
        qh->State = EHCI_QH_MULTIPLIER(bandwidth);
    } else {
        qh->State = EHCI_QH_MULTIPLIER(1);
    }

    // Now, set additionals depending on speed
    if (transfer->Speed == USBSPEED_LOW || transfer->Speed == USBSPEED_FULL) {
        if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
            qh->Flags |= EHCI_QH_CONTROLEP;
        }

        // On low-speed, set this bit
        if (transfer->Speed == USBSPEED_LOW) {
            qh->Flags |= EHCI_QH_LOWSPEED;
        }

        // Set nak-throttle to 0
        qh->Flags |= EHCI_QH_RL(0);

        // We need to fill the TT's hub-address and port-address
        qh->State |= EHCI_QH_HUBADDR(transfer->Address.HubAddress);
        qh->State |= EHCI_QH_PORT(transfer->Address.PortAddress);
    } else {
        // High speed device, no transaction translator
        qh->Flags |= EHCI_QH_HIGHSPEED;

        // Set nak-throttle to 4 if async
        if (__Transfer_IsAsync(transfer)) {
            qh->Flags |= EHCI_QH_RL(4);
        } else {
            qh->Flags |= EHCI_QH_RL(0);
        }
    }

    if (__Transfer_IsPeriodic(transfer)) {
        // If we use completion masks we'll need another transfer for start
        size_t transferSize = __Transfer_Length(transfer);
        if (transfer->Speed != USBSPEED_HIGH) {
            transferSize += transfer->MaxPacketSize;
        }

        oserr = UsbSchedulerAllocateBandwidth(
                controller->Base.Scheduler,
                transfer->TData.Periodic.Interval,
                transfer->MaxPacketSize,
                __Transfer_Direction(transfer),
                transferSize,
                transfer->Type,
                transfer->Speed,
                (uint8_t*)qh
        );

        if (oserr == OS_EOK) {
            // The allocator must keep all microframes within the same frame.
            // TODO: Add FSTN links if low/full-speed interrupt transfers can
            // cross a frame boundary.
            // EHCI requires a bit mask. For a single-packet reservation,
            // FrameMask is zero, so select the first microframe explicitly.
            if (qh->Object.FrameMask == 0) {
                qh->FrameStartMask = 1;
            } else {
                qh->FrameStartMask = (uint8_t)(qh->Object.FrameMask & -qh->Object.FrameMask);
            }
            
            if (transfer->Speed != USBSPEED_HIGH) {
                qh->FrameCompletionMask = (uint8_t)(qh->Object.FrameMask & 0xFF);
                qh->FrameCompletionMask &= ~qh->FrameStartMask;
            } else {
                qh->FrameCompletionMask = 0;
            }
        }
    }
    return oserr;
}

void
EHCIQHDump(
    _In_ EhciController_t* controller,
    _In_ EhciQueueHead_t*  qh)
{
    uintptr_t physicalAddress = 0;

    UsbSchedulerGetPoolElement(
            controller->Base.Scheduler,
            EHCI_QH_POOL,
            qh->Object.Index & USB_ELEMENT_INDEX_MASK,
            NULL,
            &physicalAddress
    );
    WARNING("EHCI: QH at 0x%x, Current 0x%x, NextQh 0x%x", physicalAddress, qh->Current, qh->LinkPointer);
    WARNING("      Bandwidth %u, StartFrame %u, Flags 0x%x", qh->Object.Bandwidth, qh->Object.StartFrame, qh->Flags);
    WARNING("      .NextTd 0x%x, .AltTd 0x%x, .Status 0x%x", qh->Overlay.NextTD, qh->Overlay.NextAlternativeTD, qh->Overlay.Status);
    WARNING("      .Token 0x%x, .Length 0x%x, .Buffers[0] 0x%x", qh->Overlay.Token, qh->Overlay.Length, qh->Overlay.Buffers[0]);
    WARNING("      .Buffers[1] 0x%x, .Buffers[2] 0x%x", qh->Overlay.Buffers[1], qh->Overlay.Buffers[2]);
}

void
EHCIQHRestart(
    EhciController_t*     controller,
    UsbManagerTransfer_t* transfer)
{
    EhciQueueHead_t* qh = (EhciQueueHead_t*)transfer->RootElement;
    uintptr_t        linkPhysical = 0;

    // The new descriptors must be visible before setting the overlay's NextTD.
    // Keep both links terminated while clearing the rest of the overlay.
    qh->Overlay.NextTD = EHCI_LINK_END;
    qh->Overlay.NextAlternativeTD = EHCI_LINK_END;
    memset((uint8_t*)&qh->Overlay + 8, 0, sizeof(qh->Overlay) - 8);

    UsbSchedulerGetPoolElement(
            controller->Base.Scheduler,
            EHCI_TD_POOL,
            qh->Object.DepthIndex & USB_ELEMENT_INDEX_MASK,
            NULL,
            &linkPhysical
    );
    dma_wmb();
    
    qh->Overlay.NextTD            = linkPhysical;
    qh->Overlay.NextAlternativeTD = EHCI_LINK_END;
    dma_mb();
}
