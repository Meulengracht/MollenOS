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
COMPILE_TIME_ASSERT(sizeof(OhciQueueHead_t) == 48);

/* OhciQhAllocate
 * Allocates a new ED for usage with the transaction. If this returns
 * NULL we are out of ED's and we should wait till next transfer. */
OhciQueueHead_t*
OhciQhAllocate(
    _In_ OhciController_t*      Controller)
{
    // Variables
    OhciQueueHead_t *Qh = NULL;
    int i;
    
    // Now, we usually allocated new endpoints for interrupts
    // and isoc, but it doesn't make sense for us as we keep one
    // large pool of ED's, just allocate from that in any case
    SpinlockAcquire(&Controller->Base.Lock);
    for (i = 0; i < OHCI_POOL_QHS; i++) {
        // Skip in case already allocated
        if (Controller->QueueControl.QHPool[i].HcdInformation & OHCI_QH_ALLOCATED) {
            continue;
        }

        // We found a free ed - mark it allocated and end
        // but reset the ED first
        memset(&Controller->QueueControl.QHPool[i], 0, sizeof(OhciQueueHead_t));
        Controller->QueueControl.QHPool[i].HcdInformation = OHCI_QH_ALLOCATED;
        Controller->QueueControl.QHPool[i].ChildIndex = OHCI_NO_INDEX;
        Controller->QueueControl.QHPool[i].LinkIndex = OHCI_NO_INDEX;
        Controller->QueueControl.QHPool[i].Index = (int16_t)i;
        
        // Store pointer
        Qh = &Controller->QueueControl.QHPool[i];
        break;
    }
    SpinlockRelease(&Controller->Base.Lock);
    return Qh;
}

/* OhciQhInitialize
 * Initializes the queue head data-structure and the associated
 * hcd flags. Afterwards the queue head is ready for use. */
void
OhciQhInitialize(
    _In_ OhciController_t*      Controller,
    _In_ OhciQueueHead_t*       Qh,
    _In_ int16_t                HeadIndex,
    _In_ int16_t                IocIndex,
    _In_ UsbTransferType_t      Type,
    _In_ size_t                 Address,
    _In_ size_t                 Endpoint,
    _In_ size_t                 PacketSize,
    _In_ UsbSpeed_t             Speed)
{
    // Variables
    OhciTransferDescriptor_t *Td    = NULL;
    int16_t LastIndex               = HeadIndex;

    // Update index's
    if (HeadIndex == OHCI_NO_INDEX) {
        Qh->Current     = OHCI_LINK_HALTED;
        Qh->EndPointer  = 0;
    }
    else {
        Td              = &Controller->QueueControl.TDPool[HeadIndex];

        // Set physical of head and tail, set HALTED bit to not start yet
        Qh->Current     = OHCI_POOL_TDINDEX(Controller, HeadIndex) | OHCI_LINK_HALTED;
        while (Td->LinkIndex != OHCI_NO_INDEX) {
            if (Td->Index == IocIndex) {
                Qh->IocAddress = OHCI_POOL_TDINDEX(Controller, Td->Index);
            }
            LastIndex   = Td->LinkIndex;
            Td          = &Controller->QueueControl.TDPool[Td->LinkIndex];
        }
        Qh->EndPointer  = OHCI_POOL_TDINDEX(Controller, LastIndex);
    }

    // Update head-index
    Qh->ChildIndex      = (int16_t)HeadIndex;

    // Initialize flags
    Qh->Flags           = OHCI_QH_SKIP;
    Qh->Flags           |= OHCI_QH_ADDRESS(Address);
    Qh->Flags           |= OHCI_QH_ENDPOINT(Endpoint);
    Qh->Flags           |= OHCI_QH_DIRECTIONTD; // Retrieve from TD
    Qh->Flags           |= OHCI_QH_LENGTH(PacketSize);
    Qh->Flags           |= OHCI_QH_TYPE(Type);

    // Set conditional flags
    if (Speed == LowSpeed) {
        Qh->Flags       |= OHCI_QH_LOWSPEED;
    }
    if (Type == IsochronousTransfer) {
        Qh->Flags       |= OHCI_QH_ISOCHRONOUS;
    }
}

/* OhciQhValidate
 * Iterates the queue head and the attached transfer descriptors
 * for errors and updates the transfer that is attached. */
void
OhciQhValidate(
    _In_ OhciController_t*      Controller, 
    _In_ UsbManagerTransfer_t*  Transfer,
    _In_ OhciQueueHead_t*       Qh)
{
    // Variables
    OhciTransferDescriptor_t *Td    = NULL;
    int ShortTransfer               = 0;

    // The initial state of the transfer will be complete
    Transfer->Status                = TransferFinished;
    Td                              = &Controller->QueueControl.TDPool[Qh->ChildIndex];

    // Iterate all the td's in the queue head, skip null td
    while (Td->LinkIndex != OHCI_NO_INDEX) {
        OhciTdValidate(Controller, Transfer, Td, &ShortTransfer);
        if (ShortTransfer == 1) {
            Qh->HcdInformation |= OHCI_QH_SHORTTRANSFER;
        }

        // Next
        Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
    }
}
