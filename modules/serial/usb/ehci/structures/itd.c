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

/* EhciIsocTdInitialize
 * This initiates any periodic scheduling information that might be needed */
OsStatus_t
EhciIsocTdInitialize(
    _In_ EhciController_t*              Controller,
    _In_ UsbTransfer_t*                 Transfer,
    _In_ EhciIsochronousDescriptor_t*   iTd,
    _In_ uintptr_t                      BufferAddress,
    _In_ size_t                         ByteCount,
    _In_ size_t                         Address,
    _In_ size_t                         Endpoint)
{
    // Variables
    uintptr_t BufferIterator    = BufferAddress;
    uintptr_t PageMask          = ~((uintptr_t)0xFFF);
    size_t EpBandwidth          = MAX(3, Transfer->Endpoint.Bandwidth);
    size_t BytesLeft            = ByteCount;
    size_t PageIndex            = 0;
    int i;

    // Set link and id
    iTd->Link                   = EHCI_LINK_END;
    iTd->QueueIndex             = EHCI_NO_INDEX;

    // Fill in buffer page settings and initial page
    for (i = 0; i < 7; i++) {
        iTd->Buffers[i]         = 0;
        iTd->ExtBuffers[i]      = 0;
        if (i == 0) { // Initialize the status word/bp0
            iTd->Buffers[i]     = EHCI_iTD_DEVADDR(Address);
            iTd->Buffers[i]     |= EHCI_iTD_EPADDR(Endpoint);
            iTd->Buffers[i]     |= EHCI_iTD_BUFFER(BufferAddress);
#if __BITS == 64
            if (Controller->CParameters & EHCI_CPARAM_64BIT) {
                iTd->ExtBuffers[i] = EHCI_iTD_EXTBUFFER(BufferAddress);
            }
            else if (BufferAddress > 0xFFFFFFFF) {
                ERROR("EHCI::BufferAddress was larger than 4gb, but controller only supports 32bit");
                return UINT_MAX;
            }
#endif
        }
        else if (i == 1) { // Initialize the mps word
            iTd->Buffers[i]     = EHCI_iTD_MPS(Transfer->Endpoint.MaxPacketSize);
            if (Transfer->Endpoint.Direction == USB_ENDPOINT_IN) {
                iTd->Buffers[i] |= EHCI_iTD_IN;
            }
        }
        else if (i == 2) { // Initialize the multi word
            iTd->Buffers[i]     = MAX(3, Transfer->Endpoint.Bandwidth);
        }
    }

    // Fill in transactions
    for (i = 0; i < 8; i++) {
        if (BytesLeft > 0) {
            size_t PageBytes        = MIN(BytesLeft, 1024 * MAX(3, Transfer->Endpoint.Bandwidth));
            iTd->Transactions[i]    = EHCI_iTD_OFFSET(BufferIterator);
            iTd->Transactions[i]    |= EHCI_iTD_PAGE(PageIndex);
            iTd->Transactions[i]    |= EHCI_iTD_LENGTH(PageBytes);
            iTd->Transactions[i]    |= EHCI_iTD_ACTIVE;
            if ((BufferIterator & PageMask) != ((BufferIterator + PageBytes) & PageMask)) {
                PageIndex++;
                iTd->Buffers[PageIndex] |= EHCI_iTD_BUFFER((BufferIterator + PageBytes));
#if __BITS == 64
                if (Controller->CParameters & EHCI_CPARAM_64BIT) {
                    iTd->ExtBuffers[PageIndex] = EHCI_iTD_EXTBUFFER((BufferIterator + PageBytes));
                }
                else if ((BufferIterator + PageBytes) > 0xFFFFFFFF) {
                    ERROR("EHCI::BufferAddress was larger than 4gb, but controller only supports 32bit");
                    return UINT_MAX;
                }
#endif
            }
            BufferIterator          += PageBytes;
            BytesLeft               -= PageBytes;
            if (BytesLeft == 0) {
                iTd->Transactions[i] |= EHCI_iTD_IOC;
            }
        }
        else {
            iTd->Transactions[i]    = 0;
        }

        // Create copies of transaction details
        iTd->TransactionsCopy[i] = iTd->Transactions[i];
    }

    // Allocate bandwith
    iTd->Bandwidth      = (reg32_t)NS_TO_US(UsbCalculateBandwidth(Transfer->Speed, 
            Transfer->Endpoint.Direction, Transfer->Type, Transfer->Transactions[0].Length));
    iTd->Interval       = (1 << Transfer->Endpoint.Interval);
    if (UsbSchedulerReserveBandwidth(Controller->Scheduler, iTd->Interval, 
        iTd->Bandwidth, EpBandwidth, &iTd->StartFrame, NULL) != OsSuccess) {
        return OsError;
    }
    return OsSuccess;
}

/* EhciRestartIsocTd
 * Resets an isochronous td-chain by restoring all transactions to original states. */
void
EhciRestartIsocTd(
    EhciController_t*       Controller, 
    UsbManagerTransfer_t*   Transfer)
{
    // Variables
    EhciIsochronousDescriptor_t* iTd = (EhciIsochronousDescriptor_t*)Transfer->EndpointDescriptor;
    int i;

    while (iTd) {
        // Restore transactions
        for (i = 0; i < 8; i++) {
            iTd->Transactions[i] = iTd->TransactionsCopy[i];
        }

        // End of chain?
        if (iTd->QueueIndex != EHCI_NO_INDEX) {
            iTd = &Controller->QueueControl.ITDPool[iTd->QueueIndex];
        }
        else {
            break;
        }
    }
    
    // Reset transfer status
    Transfer->Status = TransferQueued;
}

/* EhciScanIsocTd
 * Scans a chain of isochronous td's to check whether or not it has been processed. Returns 1
 * if there was work done - otherwise 0 if untouched. */
int
EhciScanIsocTd(
    EhciController_t*       Controller, 
    UsbManagerTransfer_t*   Transfer)
{
    // Variables
    EhciIsochronousDescriptor_t* iTd    = (EhciIsochronousDescriptor_t*)Transfer->EndpointDescriptor;
    int EarlyExit                       = 0;
    int i;

    // Is td already in process of being unmade?
    if (iTd->HcdFlags & EHCI_HCDFLAGS_UNSCHEDULE) {
        return 0;
    }

    while (iTd) {
        int ConditionCode               = 0;

        // Check status of the transactions
        for (i = 0; i < 8; i++) {
            if (iTd->Transactions[i] & EHCI_iTD_ACTIVE) {
                EarlyExit               = 1;
                break;
            }
            Transfer->Status            = TransferFinished;
            Transfer->TransactionsExecuted++;
            ConditionCode               = EhciConditionCodeToIndex(EHCI_iTD_CC(iTd->Transactions[i]));
            switch (ConditionCode) {
                case 1:
                    Transfer->Status    = TransferStalled;
                    break;
                case 2:
                    Transfer->Status    = TransferBabble;
                    break;
                case 3:
                    Transfer->Status    = TransferBufferError;
                    break;
                default:
                    break;
            }
            
            // Break out early if error is encountered
            if (Transfer->Status != TransferFinished) {
                EarlyExit = 1;
                break;
            }
        }

        // Don't check more if it's not ok
        if (Transfer->Status != TransferFinished) {
            break;
        }

        // End of chain?
        if (iTd->QueueIndex != EHCI_NO_INDEX) {
            iTd = &Controller->QueueControl.ITDPool[iTd->QueueIndex];
        }
        else {
            break;
        }
    }

    // Early exit?
    if (EarlyExit == 1 && Transfer->Status == TransferQueued) {
        return 0;
    }
    else {
        return 1;
    }
}
