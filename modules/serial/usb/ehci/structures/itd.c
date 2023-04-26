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
#include <stdlib.h>
#include <assert.h>
#include <string.h>

void
EHCITDIsochronous(
        _In_ EhciController_t*            controller,
        _In_ UsbManagerTransfer_t*        transfer,
        _In_ EhciIsochronousDescriptor_t* iTd,
        _In_ uint32_t                     pid,
        _In_ const uintptr_t*             addresses,
        _In_ const uint32_t*              lengths)
{
    uintptr_t pageMask    = ~((uintptr_t)0xFFF);
    uintptr_t buffer      = addresses[0] & pageMask;
    uint32_t  bufferIndex = 0;

    iTd->Link          = EHCI_LINK_END;
    iTd->Object.Flags |= EHCI_LINK_iTD;

    // initialize the buffer special bits
    iTd->Buffers[0]  = EHCI_iTD_DEVADDR(transfer->Address.DeviceAddress);
    iTd->Buffers[0] |= EHCI_iTD_EPADDR(transfer->Address.EndpointAddress);
    iTd->Buffers[0] |= EHCI_iTD_BUFFER(buffer);
#if __BITS == 64
    if (controller->CParameters & EHCI_CPARAM_64BIT) {
        iTd->ExtBuffers[0] = EHCI_iTD_EXTBUFFER(buffer);
    }
#endif

    iTd->Buffers[1] = EHCI_iTD_MPS(transfer->MaxPacketSize);
    iTd->Buffers[1] |= pid;

    iTd->Buffers[2] = MAX(3, transfer->TData.Periodic.Bandwith);

    // Fill in buffer page settings and initial page
    for (int i = 0; i < 8; i++) {
        if (!addresses[i] || !lengths[i]) {
            break;
        }

        // Select the buffer based on address, and do some per-buffer init
        if ((addresses[i] & pageMask) != buffer) {
            buffer = addresses[i] & pageMask;
            bufferIndex++;
            iTd->Buffers[bufferIndex] |= EHCI_iTD_BUFFER(buffer);
#if __BITS == 64
            if (controller->CParameters & EHCI_CPARAM_64BIT) {
                iTd->ExtBuffers[bufferIndex] = EHCI_iTD_EXTBUFFER(buffer);
            }
#endif
        }

        iTd->Transactions[i]  = EHCI_iTD_OFFSET(addresses[i]);
        iTd->Transactions[i] |= EHCI_iTD_PAGE(bufferIndex);
        iTd->Transactions[i] |= EHCI_iTD_LENGTH(lengths[i]);
        iTd->Transactions[i] |= EHCI_iTD_ACTIVE;

        // Create copies of transaction details
        iTd->TransactionsCopy[i] = iTd->Transactions[i];
    }
}

void
EHCIITDDump(
    _In_ EhciController_t*            controller,
    _In_ EhciIsochronousDescriptor_t* td)
{
    uintptr_t physical = 0;

    UsbSchedulerGetPoolElement(
            controller->Base.Scheduler,
            EHCI_iTD_POOL,
            td->Object.Index & USB_ELEMENT_INDEX_MASK,
            NULL,
            &physical
    );
    WARNING("EHCI: iTD(0x%x), Link(0x%x), Buffer0(0x%x:0x%x), Buffer1(0x%x:0x%x), Buffer2(0x%x:0x%x)",
            physical, td->Link, td->ExtBuffers[0], td->Buffers[0],
            td->ExtBuffers[1], td->Buffers[1], td->ExtBuffers[2], td->Buffers[2]);
    WARNING("          Buffer3(0x%x), Buffer4(0x%x:0x%x), Buffer5(0x%x:0x%x)",
            td->ExtBuffers[3], td->Buffers[3], td->ExtBuffers[4], td->Buffers[4], td->ExtBuffers[5], td->Buffers[5]);
    WARNING("          Buffer6(0x%x:0x%x), XAction0(0x%x), XAction1(0x%x)",
            td->ExtBuffers[6], td->Buffers[6], td->Transactions[0], td->Transactions[1]);
    WARNING("          XAction2(0x%x), XAction3(0x%x), XAction4(0x%x)",
            td->Transactions[2], td->Transactions[3], td->Transactions[4]);
    WARNING("          XAction5(0x%x), XAction6(0x%x), XAction7(0x%x)",
            td->Transactions[5], td->Transactions[6], td->Transactions[7]);
}

void
EHCIITDVerify(
        _In_ struct HCIProcessReasonScanContext* scanContext,
        _In_ EhciIsochronousDescriptor_t*        iTD)
{
    int cc = -1;

    // Have we already processed this one? In that case we ignore
    // it completely.
    if (iTD->Object.Flags & USB_ELEMENT_PROCESSED) {
        scanContext->ElementsExecuted++;
        return;
    }

    // Check status of the transactions
    for (int i = 0; i < 8; i++) {
        if (iTD->Transactions[i] & EHCI_iTD_ACTIVE) {
            break;
        }

        cc = EHCIConditionCodeToIndex(EHCI_iTD_CC(iTD->Transactions[i]));
        switch (cc) {
            case 1:
                scanContext->Result = USBTRANSFERCODE_STALL;
                break;
            case 2:
                scanContext->Result = USBTRANSFERCODE_BABBLE;
                break;
            case 3:
                scanContext->Result = USBTRANSFERCODE_BUFFERERROR;
                break;
            default:
                break;
        }

        if (scanContext->Result != USBTRANSFERCODE_INVALID) {
            break;
        }
    }

    if (cc == -1) {
        // still active
        return;
    }

    // mark TD as processed
    iTD->Object.Flags |= USB_ELEMENT_PROCESSED;
    scanContext->ElementsProcessed++;
    scanContext->ElementsExecuted++;
}

void
EHCIITDRestart(
        _In_ EhciIsochronousDescriptor_t* iTD)
{
    for (int i = 0; i < 8; i++) {
        iTD->Transactions[i] = iTD->TransactionsCopy[i];
    }
}
