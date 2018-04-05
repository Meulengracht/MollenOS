/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * MollenOS MCore - Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE
#define __COMPILE_ASSERT

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>
#include "../ohci.h"

/* Includes
 * - Library */
#include <threads.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

// Size assertions
COMPILE_TIME_ASSERT(sizeof(OhciTransferDescriptor_t) == 64);

/* OhciTdAllocate
 * Allocates a new TD for usage with the transaction. If this returns
 * NULL we are out of TD's and we should wait till next transfer. */
OhciTransferDescriptor_t*
OhciTdAllocate(
    _In_ OhciController_t *Controller)
{
    // Variables
    OhciTransferDescriptor_t *Td = NULL;
    int i;

    // Now, we usually allocated new descriptors for interrupts
    // and isoc, but it doesn't make sense for us as we keep one
    // large pool of Td's, just allocate from that in any case
    SpinlockAcquire(&Controller->Base.Lock);
    for (i = 0; i < OHCI_POOL_TDNULL; i++) {
        // Skip ahead if allocated, skip twice if isoc
        if (Controller->QueueControl.TDPool[i].Flags & OHCI_TD_ALLOCATED) {
            continue;
        }

        // Found one, reset
        memset(&Controller->QueueControl.TDPool[i], 0, sizeof(OhciTransferDescriptor_t));
        Controller->QueueControl.TDPool[i].Flags        = OHCI_TD_ALLOCATED;
        Controller->QueueControl.TDPool[i].Index        = (int16_t)i;
        Controller->QueueControl.TDPool[i].LinkIndex    = OHCI_NO_INDEX;
        Td = &Controller->QueueControl.TDPool[i];
        break;
    }
    SpinlockRelease(&Controller->Base.Lock);
    return Td;
}

/* OhciTdSetup 
 * Creates a new setup token td and initializes all the members.
 * The Td is immediately ready for execution. */
OhciTransferDescriptor_t*
OhciTdSetup(
    _In_ OhciController_t *Controller, 
    _In_ UsbTransaction_t *Transaction)
{
    // Variables
    OhciTransferDescriptor_t *Td = NULL;

    // Allocate a new Td
    Td = OhciTdAllocate(Controller);
    if (Td == NULL) {
        return USB_OUT_OF_RESOURCES;
    }

    // Set no link
    Td->Link = 0;
    Td->LinkIndex = OHCI_NO_INDEX;

    // Initialize the Td flags
    Td->Flags |= OHCI_TD_SETUP;
    Td->Flags |= OHCI_TD_IOC_NONE;
    Td->Flags |= OHCI_TD_TOGGLE_LOCAL;
    Td->Flags |= OHCI_TD_ACTIVE;

    // Install the buffer
    Td->Cbp = Transaction->BufferAddress;
    Td->BufferEnd = Td->Cbp + sizeof(UsbPacket_t) - 1;

    // Store copy of original content
    Td->OriginalFlags = Td->Flags;
    Td->OriginalCbp = Td->Cbp;
    return Td;
}

/* OhciTdIsochronous
 * Creates a new isoc token td and initializes all the members.
 * The Td is immediately ready for execution. */
OhciTransferDescriptor_t*
OhciTdIsochronous(
    _In_ OhciController_t*          Controller,
    _In_ size_t                     MaxPacketSize,
    _In_ uint32_t                   PId,
    _In_ uintptr_t                  Address,
    _In_ size_t                     Length)
{
    // Variables
    OhciTransferDescriptor_t *Td    = NULL;
    size_t BytesToTransfer          = Length;
    size_t BufferOffset             = 0;
    int FrameCount                  = DIVUP(Length, MaxPacketSize);
    int FrameIndex                  = 0;
    int Crossed                     = 0;
    
    // Debug
    TRACE("OhciTdIsochronous(Id %u, Address 0x%x, Length 0x%x", PId, Address, Length);

    // Allocate a new Td and sanitize response
    Td = OhciTdAllocate(Controller);
    if (Td == NULL) {
        return USB_OUT_OF_RESOURCES;
    }

    // Max packet size is 1023 for isoc
    // If direction is out and mod 1023 is 0
    // add a zero-length frame
    // If framecount is > 8, nono
    if (FrameCount > 8) {
        FrameCount = 8;
    }

    // Initialize flags
    Td->Flags       |= PId;
    Td->Flags       |= OHCI_TD_FRAMECOUNT((FrameCount - 1));
    Td->Flags       |= OHCI_TD_ISOCHRONOUS;
    Td->Flags       |= OHCI_TD_IOC_NONE;
    Td->Flags       |= OHCI_TD_ACTIVE;

    // Initialize buffer access
    Td->Cbp         = LODWORD(Address);
    Td->BufferEnd   = Td->Cbp + Length - 1;

    // Iterate frames and setup
    while (BytesToTransfer) {
        // Set offset 0 and increase bufitr
        size_t BytesStep        = MIN(BytesToTransfer, MaxPacketSize);
        Td->Offsets[FrameIndex] = BufferOffset;
        Td->Offsets[FrameIndex] = ((Crossed & 0x1) << 12);
        BufferOffset            += BytesStep;

        // Sanity on page-crossover
        if (((Address + BufferOffset) & 0xFFFFF000) != (Address & 0xFFFFF000)) {
            BufferOffset        = (Address + BufferOffset) & 0xFFF; // Reset offset
            Crossed             = 1;
        }

        // Update iterators
        BytesToTransfer--;
        FrameIndex++;
    }

    // Set this is as end of chain
    Td->Link            = 0;
    Td->LinkIndex       = OHCI_NO_INDEX;

    // Store copy of original content
    Td->OriginalFlags   = Td->Flags;
    Td->OriginalCbp     = Td->Cbp;
    Td->OriginalBufferEnd = Td->BufferEnd;
    return Td;
}

/* OhciTdIo 
 * Creates a new io token td and initializes all the members.
 * The Td is immediately ready for execution. */
OhciTransferDescriptor_t*
OhciTdIo(
    _In_ OhciController_t*  Controller,
    _In_ UsbTransferType_t  Type,
    _In_ uint32_t           PId,
    _In_ int                Toggle,
    _In_ uintptr_t          Address,
    _In_ size_t             Length)
{
    // Variables
    OhciTransferDescriptor_t *Td = NULL;
    
    // Allocate a new Td and sanitize response
    Td = OhciTdAllocate(Controller);
    if (Td == NULL) {
        return USB_OUT_OF_RESOURCES;
    }
    
    // Debug
    TRACE("OhciTdIo(Type %u, Id %u, Toggle %i, Address 0x%x, Length 0x%x",
        Type, PId, Toggle, Address, Length);

    // Set this is as end of chain
    Td->Link        = 0;
    Td->LinkIndex   = OHCI_NO_INDEX;

    // Initialize flags as a IO Td
    Td->Flags       |= PId;
    Td->Flags       |= OHCI_TD_IOC_NONE;
    Td->Flags       |= OHCI_TD_TOGGLE_LOCAL;
    Td->Flags       |= OHCI_TD_ACTIVE;

    // We have to allow short-packets in some cases
    // where data returned or send might be shorter
    if (Type == ControlTransfer) {
        if (PId == OHCI_TD_IN && Length > 0) {
            Td->Flags |= OHCI_TD_SHORTPACKET_OK;
        }
    }
    else if (PId == OHCI_TD_IN) {
        Td->Flags |= OHCI_TD_SHORTPACKET_OK;
    }

    // Set the data-toggle?
    if (Toggle) {
        Td->Flags |= OHCI_TD_TOGGLE;
    }

    // Is there bytes to transfer or null packet?
    if (Length > 0) {
        Td->Cbp         = LODWORD(Address);
        Td->BufferEnd   = Td->Cbp + (Length - 1);
    }

    // Store copy of original content
    Td->OriginalFlags   = Td->Flags;
    Td->OriginalCbp     = Td->Cbp;
    return Td;
}

/* OhciTdValidate
 * Checks the transfer descriptors for errors and updates the transfer that is attached
 * with the bytes transferred and error status. */
void
OhciTdValidate(
    _In_  OhciController_t*         Controller,
    _In_  UsbManagerTransfer_t*     Transfer,
    _In_  OhciTransferDescriptor_t* Td,
    _Out_ int*                      ShortTransfer)
{
    // Variables
    int ErrorCount          = OHCI_TD_ERRORCOUNT(Td->Flags);
    int ErrorCode           = OHCI_TD_ERRORCODE(Td->Flags);
    int i;

    // Sanitize active status
    if (Td->Flags & OHCI_TD_ACTIVE) {
        return;
    }
    Transfer->TransactionsExecuted++;

    // Sanitize the error code
    if ((ErrorCount == 2 && ErrorCode != 0) && Transfer->Status == TransferFinished) {
        Transfer->Status    = OhciGetStatusCode(ErrorCode);
    }

    // Calculate length transferred 
    // Take into consideration the N-1 
    if (Td->BufferEnd != 0) {
        int BytesTransferred    = 0;
        int BytesRequested      = (Td->BufferEnd - Td->OriginalCbp) + 1;
        if (Td->Cbp == 0) {
            BytesTransferred    = BytesRequested;
        }
        else {
            BytesTransferred    = Td->Cbp - Td->OriginalCbp;
        }
        if (BytesTransferred < BytesRequested) {
            *ShortTransfer      = 1;
        }
        for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
            if (Transfer->Transfer.Transactions[i].Length > Transfer->BytesTransferred[i]) {
                Transfer->BytesTransferred[i] += BytesTransferred;
                break;
            }
        }
    }
}

/* OhciTdRestart
 * Restarts a transfer descriptor that already exists in queue. 
 * Synchronizes toggles, updates buffer points if the transfer is not isochronous. */
void
OhciTdRestart(
    _In_ OhciController_t*          Controller,
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ OhciTransferDescriptor_t*  Td)
{
    // Variables
    uintptr_t BufferBase    = 0;
    uintptr_t BufferStep    = 0;
    int SwitchToggles       = 0;

    // Interrupt transfers need some additional processing
    // Update toggle if neccessary (in original data)
    if (Transfer->Transfer.Type == InterruptTransfer) {
        SwitchToggles       = Transfer->TransactionsTotal % 2;
        BufferBase          = Transfer->Transfer.Transactions[0].BufferAddress;
        BufferStep          = Transfer->Transfer.Endpoint.MaxPacketSize;

        uintptr_t BufferBaseUpdated = ADDLIMIT(BufferBase, Td->OriginalCbp, 
            BufferStep, BufferBase + Transfer->Transfer.PeriodicBufferSize);
        Td->OriginalCbp             = LODWORD(BufferBaseUpdated);
        if (SwitchToggles == 1) {
            int Toggle          = UsbManagerGetToggle(Transfer->DeviceId, Transfer->Pipe);
            Td->OriginalFlags   &= ~OHCI_TD_TOGGLE;
            Td->OriginalFlags   |= (Toggle << 24);
            UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, Toggle ^ 1);
        }
    }

    // Reset attributes
    Td->Flags       = Td->OriginalFlags;
    Td->Cbp         = Td->OriginalCbp;
    Td->Link        = OHCI_POOL_TDINDEX(Controller, Td->LinkIndex);
    if (Transfer->Transfer.Type == InterruptTransfer) {
        Td->BufferEnd   = Td->Cbp + (BufferStep - 1);
    }
    else {
        Td->BufferEnd   = Td->OriginalBufferEnd;
    }
}
