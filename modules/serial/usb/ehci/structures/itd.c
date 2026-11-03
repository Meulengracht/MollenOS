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

bool
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
    int       transactionCount = 0;

    // The first transaction must have a buffer address and a nonzero length.
    if (addresses[0] == 0 || lengths[0] == 0) {
        return false;
    }

    // The TD must already be linked before initialization. Leave its links unchanged.
    iTd->Object.Flags |= EHCI_LINK_iTD;

    memset((void*)iTd->Buffers, 0, sizeof(iTd->Buffers));
    memset((void*)iTd->ExtBuffers, 0, sizeof(iTd->ExtBuffers));
    memset((void*)iTd->Transactions, 0, sizeof(iTd->Transactions));
    memset((void*)iTd->TransactionsCopy, 0, sizeof(iTd->TransactionsCopy));
    
    iTd->Object.Flags &= ~USB_ELEMENT_PROCESSED;

    // The first three buffer entries also carry endpoint and transfer settings.
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

    iTd->Buffers[2] = MIN(3, transfer->TData.Periodic.Bandwith + 1);

    // Set up buffer pages and transactions for up to eight microframes.
    for (int i = 0; i < 8; i++) {
        if (!addresses[i] || !lengths[i]) {
            break;
        }

        // Add a buffer entry when the transaction starts on a different page.
        if ((addresses[i] & pageMask) != buffer) {
            buffer = addresses[i] & pageMask;
            bufferIndex++;
            // An iTD has seven buffer-page entries. Reject an eighth page before
            // writing beyond the descriptor into adjacent scheduler metadata.
            if (bufferIndex >= 7) {
                return false;
            }
            iTd->Buffers[bufferIndex] |= EHCI_iTD_BUFFER(buffer);
#if __BITS == 64
            if (controller->CParameters & EHCI_CPARAM_64BIT) {
                iTd->ExtBuffers[bufferIndex] = EHCI_iTD_EXTBUFFER(buffer);
            }
#endif
        }

        iTd->Transactions[i]  = EHCI_iTD_OFFSET(addresses[i]);
        iTd->Transactions[i] |= EHCI_iTD_PAGE(bufferIndex);
        iTd->Transactions[i] |= EHCI_iTD_LENGTH(lengths[i]) | EHCI_iTD_IOC;
        transactionCount++;
    }

    // Buffer and transaction fields must be visible before setting ACTIVE.
    // Save the active transactions for the next periodic restart.
    dma_mb();
    for (int i = 0; i < transactionCount; i++) {
        iTd->Transactions[i] |= EHCI_iTD_ACTIVE;
        iTd->TransactionsCopy[i] = iTd->Transactions[i];
    }
    return true;
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
    int cc;
    int i;

    if (iTD->Object.Flags & USB_ELEMENT_PROCESSED) {
        scanContext->ElementsExecuted++;
        return;
    }
    // All scheduled microframes must finish before we process the descriptor.
    for (i = 0; i < 8; i++) {
        if (iTD->TransactionsCopy[i] != 0 && (iTD->Transactions[i] & EHCI_iTD_ACTIVE)) {
            return;
        }
    }
    for (i = 0; i < 8 && iTD->TransactionsCopy[i] != 0; i++) {
        cc = EHCIConditionCodeToIndex(EHCI_iTD_CC(iTD->Transactions[i]));
        if (cc != 0) {
            scanContext->Result = cc == 1 ? USBTRANSFERCODE_STALL :
                cc == 2 ? USBTRANSFERCODE_BABBLE : USBTRANSFERCODE_BUFFERERROR;
            break;
        }
        scanContext->BytesTransferred += (iTD->Transactions[i] >> 16) & 0xFFF;
    }
    iTD->Object.Flags |= USB_ELEMENT_PROCESSED;
    scanContext->ElementsProcessed++;
    scanContext->ElementsExecuted++;
}

void
EHCIITDRestart(
        _In_ EhciIsochronousDescriptor_t* iTD)
{
    // Clear PROCESSED so the next scan can process this TD again.
    iTD->Object.Flags &= ~(USB_ELEMENT_PROCESSED);

    for (int i = 0; i < 8; i++) {
        iTD->Transactions[i] = iTD->TransactionsCopy[i];
    }
}
