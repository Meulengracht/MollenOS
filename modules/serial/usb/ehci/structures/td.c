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

/* EhciTdFill
 * This sets up a QTD (TD) buffer structure and makes 
 * sure it's split correctly out on all the pages */
size_t
EhciTdFill(
    _In_ EhciController_t*          Controller,
    _In_ EhciTransferDescriptor_t*  Td, 
    _In_ uintptr_t                  BufferAddress, 
    _In_ size_t                     Length)
{
    // Variables
    size_t LengthRemaining  = Length;
    size_t Count            = 0;
    int i;

    // Sanitize parameters
    if (Length == 0 || BufferAddress == 0) {
        return 0;
    }

    // Iterate buffers
    for (i = 0; LengthRemaining > 0 && i < 5; i++) {
        uintptr_t Physical = BufferAddress + (i * 0x1000);
        
        // Update buffer
        Td->Buffers[i]          = EHCI_TD_BUFFER(Physical);
        Td->ExtBuffers[i]       = 0;
#if __BITS == 64
        if (Controller->CParameters & EHCI_CPARAM_64BIT) {
            Td->ExtBuffers[i]   = EHCI_TD_EXTBUFFER(Physical);
        }
#endif

        // Update iterators
        Count                   += MIN(0x1000, LengthRemaining);
        LengthRemaining         -= MIN(0x1000, LengthRemaining);
    }
    return Count; // Return how many bytes were "buffered"
}

/* EhciTdSetup
 * This allocates & initializes a TD for a setup transaction 
 * this is only used for control transactions */
void
EhciTdSetup(
    _In_ EhciController_t*          Controller,
    _In_ EhciTransferDescriptor_t*  Td,
    _In_ UsbTransaction_t*          Transaction)
{
    // Variables
    size_t CalculatedLength         = 0;

    // Initialize the transfer-descriptor
    Td->Link                    = EHCI_LINK_END;
    Td->AlternativeLink         = EHCI_LINK_END;

    Td->Status                  = EHCI_TD_ACTIVE;
    Td->Token                   = EHCI_TD_SETUP | EHCI_TD_ERRCOUNT;

    // Calculate the length of the setup transfer
    CalculatedLength            = EhciTdFill(Controller, Td, Transaction->BufferAddress, sizeof(UsbPacket_t));
    Td->Length                  = (uint16_t)(EHCI_TD_LENGTH(CalculatedLength));

    // Store copies
    Td->OriginalLength          = Td->Length;
    Td->OriginalToken           = Td->Token;
}

/* EhciTdIo
 * This allocates & initializes a TD for an i/o transaction 
 * and is used for control, bulk and interrupt */
void
EhciTdIo(
    _In_ EhciController_t*          Controller,
    _In_ EhciTransferDescriptor_t*  Td,
    _In_ UsbTransfer_t*             Transfer,
    _In_ UsbTransaction_t*          Transaction,
    _In_ int                        Toggle)
{
    // Variables
    uintptr_t NullTdPhysical    = 0;
    uint8_t PId                 = (Transaction->Type == InTransaction) ? EHCI_TD_IN : EHCI_TD_OUT;
    size_t CalculatedLength     = 0;

    // Initialize the new Td
    Td->Link                    = EHCI_LINK_END;
    Td->Status                  = EHCI_TD_ACTIVE;
    Td->Token                   = PId | EHCI_TD_ERRCOUNT;

    // Always stop transaction on short-reads
    if (PId == EHCI_TD_IN) {
        UsbSchedulerGetPoolElement(Controller->Base.Scheduler, EHCI_TD_POOL, EHCI_TD_NULL, NULL, &NullTdPhysical);
        Td->AlternativeLink     = LODWORD(NullTdPhysical);
    }

    // Calculate the length of the transfer
    CalculatedLength            = EhciTdFill(Controller, Td, Transaction->BufferAddress, Transaction->Length);
    Td->Length                  = (uint16_t)(EHCI_TD_LENGTH(CalculatedLength));
    if (Toggle) {
        Td->Length              |= EHCI_TD_TOGGLE;
    }

    // Calculate next toggle 
    // if transaction spans multiple transfers
    if (Transaction->Length > 0 && !(DIVUP(Transaction->Length, Transfer->Endpoint.MaxPacketSize) % 2)) {
        Toggle                  ^= 0x1;
    }
    Td->OriginalLength          = Td->Length;
    Td->OriginalToken           = Td->Token;
}

/* EhciTdDump
 * Dumps the information contained in the descriptor by writing it. */
void
EhciTdDump(
    _In_ EhciController_t*          Controller,
    _In_ EhciTransferDescriptor_t*  Td)
{
    // Variables
    uintptr_t PhysicalAddress   = 0;

    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, EHCI_TD_POOL, 
        Td->Object.Index & USB_ELEMENT_INDEX_MASK, NULL, &PhysicalAddress);
    WARNING("EHCI: TD(0x%x), Link(0x%x), AltLink(0x%x), Status(0x%x), Token(0x%x)",
        PhysicalAddress, Td->Link, Td->AlternativeLink, Td->Status, Td->Token);
    WARNING("          Length(0x%x), Buffer0(0x%x:0x%x), Buffer1(0x%x:0x%x)",
        Td->Length, Td->ExtBuffers[0], Td->Buffers[0], Td->ExtBuffers[1], Td->Buffers[1]);
    WARNING("          Buffer2(0x%x:0x%x), Buffer3(0x%x:0x%x), Buffer4(0x%x:0x%x)", 
        Td->ExtBuffers[2], Td->Buffers[2], Td->ExtBuffers[3], Td->Buffers[3], Td->ExtBuffers[4], Td->Buffers[4]);
}

/* EhciTdValidate
 * Checks the transfer descriptors for errors and updates the transfer that is attached
 * with the bytes transferred and error status. */
void
EhciTdValidate(
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ EhciTransferDescriptor_t*  Td)
{
    // Variables
    int ErrorCode;
    int i;

    // Sanitize active status
    if (Td->Status & EHCI_TD_ACTIVE) {
        // If this one is still active, but it's an transfer that has
        // elements processed - resync toggles
        if (Transfer->Status != TransferQueued) {
            Transfer->Flags |= TransferFlagSync;
        }
        return;
    }
    Transfer->TransactionsExecuted++;

    // Get error code based on type of transfer
    ErrorCode               = EhciConditionCodeToIndex(
        Transfer->Transfer.Speed == HighSpeed ? (Td->Status & 0xFC) : Td->Status);
    
    if (ErrorCode != 0) {
        Transfer->Status    = EhciGetStatusCode(ErrorCode);
        return; // Skip bytes transferred
    }
    else if (ErrorCode == 0 && Transfer->Status == TransferQueued) {
        Transfer->Status    = TransferFinished;
    }

    // Add bytes transferred
    if ((Td->OriginalLength & EHCI_TD_LENGTHMASK) != 0) {
        int BytesTransferred    = (Td->OriginalLength - Td->Length) & EHCI_TD_LENGTHMASK;
        int BytesRequested      = Td->OriginalLength & EHCI_TD_LENGTHMASK;
        
        if (BytesTransferred < BytesRequested) {
            Transfer->Flags |= TransferFlagShort;

            // On short transfers we might have to sync, but only 
            // if there are un-processed td's after this one
            if (Td->Object.DepthIndex != USB_ELEMENT_NO_INDEX) {
                Transfer->Flags |= TransferFlagSync;
            }
        }
        for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
            if (Transfer->Transfer.Transactions[i].Length > Transfer->BytesTransferred[i]) {
                Transfer->BytesTransferred[i] += BytesTransferred;
                break;
            }
        }
    }
}

/* EhciTdSynchronize
 * Synchronizes the toggle status of the transfer descriptor by retrieving
 * current and updating the pipe toggle. */
void
EhciTdSynchronize(
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ EhciTransferDescriptor_t*  Td)
{
    // Variables
    int Toggle          = UsbManagerGetToggle(Transfer->DeviceId, &Transfer->Transfer.Address);

    // Is it neccessary?
    if (Toggle == 1 && (Td->Length & EHCI_TD_TOGGLE)) {
        return;
    }

    // Clear
    Td->Length          &= ~(EHCI_TD_TOGGLE);
    if (Toggle) {
        Td->Length      |= EHCI_TD_TOGGLE;
    }

    // Update copy
    Td->OriginalLength  = Td->Length;
    UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, Toggle ^ 1);
}

/* EhciTdRestart
 * Restarts a transfer descriptor by resettings it's status and updating buffers if the
 * trasnfer type is an interrupt-transfer that uses circularbuffers. */
void
EhciTdRestart(
    _In_ EhciController_t*          Controller,
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ EhciTransferDescriptor_t*  Td)
{
    // Variables
    uintptr_t BufferBaseUpdated = 0;
    uintptr_t BufferBase    = 0;
    uintptr_t BufferStep    = 0;
    int Toggle              = UsbManagerGetToggle(Transfer->DeviceId, &Transfer->Transfer.Address);

    // Clear
    Td->OriginalLength      &= ~(EHCI_TD_TOGGLE);
    if (Toggle) {
        Td->OriginalLength  = EHCI_TD_TOGGLE;
    }
    UsbManagerSetToggle(Transfer->DeviceId, &Transfer->Transfer.Address, Toggle ^ 1);

    // Reset members of td
    Td->Status                  = EHCI_TD_ACTIVE;
    Td->Length                  = Td->OriginalLength;
    Td->Token                   = Td->OriginalToken;
    
    // Adjust buffer if not just restart
    if (Transfer->Status != TransferNAK) {
        BufferBaseUpdated = ADDLIMIT(BufferBase, Td->Buffers[0], 
            BufferStep, BufferBase + Transfer->Transfer.PeriodicBufferSize);
        EhciTdFill(Controller, Td, BufferBaseUpdated, BufferStep);
    }
}
