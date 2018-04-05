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
 * MollenOS MCore - Universal Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>
#include "../uhci.h"

/* Includes
 * - Library */
#include <ds/collection.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* UhciTdAllocate
 * Allocates a new TD for usage with the transaction. If this returns
 * NULL we are out of TD's and we should wait till next transfer. */
UhciTransferDescriptor_t*
UhciTdAllocate(
    _In_ UhciController_t *Controller,
    _In_ UsbTransferType_t Type)
{
    // Variables
    UhciTransferDescriptor_t *Td = NULL;
    int i;

    // Unused for now
    _CRT_UNUSED(Type);

    // Lock access to the queue
    SpinlockAcquire(&Controller->Base.Lock);

    // Now, we usually allocated new descriptors for interrupts
    // and isoc, but it doesn't make sense for us as we keep one
    // large pool of TDs, just allocate from that in any case
    for (i = 0; i < UHCI_POOL_TDNULL; i++) {
        // Skip ahead if allocated, skip twice if isoc
        if (Controller->QueueControl.TDPool[i].HcdFlags & UHCI_TD_ALLOCATED) {
            continue;
        }

        // Found one, reset
        memset(&Controller->QueueControl.TDPool[i], 0, sizeof(UhciTransferDescriptor_t));
        Controller->QueueControl.TDPool[i].LinkIndex = UHCI_NO_INDEX;
        Controller->QueueControl.TDPool[i].HcdFlags = UHCI_TD_ALLOCATED;
        Controller->QueueControl.TDPool[i].HcdFlags |= UHCI_TD_SET_INDEX(i);
        Td = &Controller->QueueControl.TDPool[i];
        break;
    }

    // Release the lock, let others pass
    SpinlockRelease(&Controller->Base.Lock);
    return Td;
}

/* UhciTdSetup 
 * Creates a new setup token td and initializes all the members.
 * The Td is immediately ready for execution. */
UhciTransferDescriptor_t*
UhciTdSetup(
    _In_ UhciController_t *Controller, 
    _In_ UsbTransaction_t *Transaction,
    _In_ size_t Address, 
    _In_ size_t Endpoint,
    _In_ UsbTransferType_t Type,
    _In_ UsbSpeed_t Speed)
{
    // Variables
    UhciTransferDescriptor_t *Td = NULL;

    // Allocate a new Td
    Td = UhciTdAllocate(Controller, Type);
    if (Td == NULL) {
        return NULL;
    }

    // Set no link
    Td->Link = UHCI_LINK_END;
    Td->LinkIndex = UHCI_NO_INDEX;

    // Setup td flags
    Td->Flags = UHCI_TD_ACTIVE;
    Td->Flags |= UHCI_TD_SETCOUNT(3);
    if (Speed == LowSpeed) {
        Td->Flags |= UHCI_TD_LOWSPEED;
    }

    // Setup td header
    Td->Header = UHCI_TD_PID_SETUP;
    Td->Header |= UHCI_TD_DEVICE_ADDR(Address);
    Td->Header |= UHCI_TD_EP_ADDR(Endpoint);
    Td->Header |= UHCI_TD_MAX_LEN((sizeof(UsbPacket_t) - 1));

    // Install the buffer
    Td->Buffer = Transaction->BufferAddress;

    // Store data
    Td->OriginalFlags = Td->Flags;
    Td->OriginalHeader = Td->Header;

    // Done
    return Td;
}

/* UhciTdIo 
 * Creates a new io token td and initializes all the members.
 * The Td is immediately ready for execution. */
UhciTransferDescriptor_t*
UhciTdIo(
    _In_ UhciController_t *Controller,
    _In_ UsbTransferType_t Type,
    _In_ uint32_t PId,
    _In_ int Toggle,
    _In_ size_t Address, 
    _In_ size_t Endpoint,
    _In_ size_t MaxPacketSize,
    _In_ UsbSpeed_t Speed,
    _In_ uintptr_t BufferAddress,
    _In_ size_t Length)
{
    // Variables
    UhciTransferDescriptor_t *Td = NULL;
    
    // Allocate a new Td
    Td = UhciTdAllocate(Controller, Type);
    if (Td == NULL) {
        return NULL;
    }

    // Set no link
    Td->Link = UHCI_LINK_END;
    Td->LinkIndex = UHCI_NO_INDEX;

    // Setup td flags
    Td->Flags = UHCI_TD_ACTIVE;
    Td->Flags |= UHCI_TD_SETCOUNT(3);
    if (Speed == LowSpeed) {
        Td->Flags |= UHCI_TD_LOWSPEED;
    }
    if (Type == IsochronousTransfer) {
        Td->Flags |= UHCI_TD_ISOCHRONOUS;
    }

    // We set SPD on in transfers, but NOT on zero length
    if (Type == ControlTransfer) {
        if (PId == UHCI_TD_PID_IN && Length > 0) {
            Td->Flags |= UHCI_TD_SHORT_PACKET;
        }
    }
    else if (PId == UHCI_TD_PID_IN) {
        Td->Flags |= UHCI_TD_SHORT_PACKET;
    }

    // Setup td header
    Td->Header = PId;
    Td->Header |= UHCI_TD_DEVICE_ADDR(Address);
    Td->Header |= UHCI_TD_EP_ADDR(Endpoint);

    // Set the data-toggle?
    if (Toggle) {
        Td->Header |= UHCI_TD_DATA_TOGGLE;
    }

    // Setup size
    if (Length > 0) {
        if (Length < MaxPacketSize && Type == InterruptTransfer) {
            Td->Header |= UHCI_TD_MAX_LEN((MaxPacketSize - 1));
        }
        else {
            Td->Header |= UHCI_TD_MAX_LEN((Length - 1));
        }
    }
    else {
        Td->Header |= UHCI_TD_MAX_LEN(0x7FF);
    }

    // Store buffer
    Td->Buffer = LODWORD(BufferAddress);

    // Store data
    Td->OriginalFlags = Td->Flags;
    Td->OriginalHeader = Td->Header;
    return Td;
}

/* UhciTdValidate
 * Checks the transfer descriptors for errors and updates the transfer that is attached
 * with the bytes transferred and error status. */
void
UhciTdValidate(
    _In_  UhciController_t*         Controller,
    _In_  UsbManagerTransfer_t*     Transfer,
    _In_  UhciTransferDescriptor_t* Td,
    _Out_ int*                      ShortTransfer)
{
    // Variables
    int ErrorCode = UhciConditionCodeToIndex(UHCI_TD_STATUS(Td->Flags));
    int i;

    // Sanitize active status
    if (Td->Flags & UHCI_TD_ACTIVE) {
        return;
    }
    Transfer->TransactionsExecuted++;

    // Now validate the code
    if (ErrorCode != 0) {
        Transfer->Status        = UhciGetStatusCode(ErrorCode);
    }

    // Calculate length transferred 
    // Take into consideration the N-1 
    if (Td->Buffer != 0) {
        int BytesTransferred    = UHCI_TD_ACTUALLENGTH(Td->Flags) + 1;
        int BytesRequested      = UHCI_TD_GET_LEN(Td->Header) + 1;
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
