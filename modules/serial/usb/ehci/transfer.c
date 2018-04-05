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
#define __TRACE

/* Includes
 * - System */
#include <os/utils.h>
#include "ehci.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* EhciDebugTransfer
 * Dumps transactions and transfer information for inspection. */
void
EhciDebugTransfer(
    _In_ EhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
{
    // Variables
    EhciTransferDescriptor_t *Td    = NULL;
    EhciQueueHead_t *Qh             = NULL;
    uintptr_t QhAddress             = 0;

    // Initialize pointers
    Qh                      = (EhciQueueHead_t *)Transfer->EndpointDescriptor;
    Td                      = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    QhAddress               = EHCI_POOL_QHINDEX(Controller, Qh->Index);

    // Trace
    TRACE("EHCI: QH at 0x%x, FirstTd 0x%x, NextQh 0x%x", QhAddress, Qh->CurrentTD, Qh->LinkPointer);
    TRACE("      Bandwidth %u, StartFrame %u, Flags 0x%x", Qh->Bandwidth, Qh->sFrame, Qh->Flags);

    while (Td != NULL) {
        uintptr_t TdPhysical = EHCI_POOL_TDINDEX(Controller, Td->Index);
        TRACE("EHCI: TD(0x%x), Link(0x%x), AltLink(0x%x), Status(0x%x), Token(0x%x)",
            TdPhysical, Td->Link, Td->AlternativeLink, Td->Status, Td->Token);
        TRACE("          Length(0x%x), Buffer0(0x%x:0x%x), Buffer1(0x%x:0x%x)",
            Td->Length, Td->ExtBuffers[0], Td->Buffers[0], Td->ExtBuffers[1], Td->Buffers[1]);
        TRACE("          Buffer2(0x%x:0x%x), Buffer3(0x%x:0x%x), Buffer4(0x%x:0x%x)", 
            Td->ExtBuffers[2], Td->Buffers[2], Td->ExtBuffers[3], Td->Buffers[3], Td->ExtBuffers[4], Td->Buffers[4]);
        if (Td->LinkIndex != EHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
        }
        else {
            Td = NULL;
        }
    }
}

/* EhciTransactionDispatch
 * Queues the transfer up in the controller hardware, after finalizing the
 * transactions and preparing them. */
UsbTransferStatus_t
EhciTransactionDispatch(
    _In_ EhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
{
    // Variables
    EhciTransferDescriptor_t *Td        = NULL;
    EhciQueueHead_t *Qh                 = NULL;
    uintptr_t QhAddress                 = 0;

    EhciIsochronousDescriptor_t *iTd    = NULL;

    // Debug
    TRACE("EhciTransactionDispatch()");

    // Initialize values
    Transfer->Status                    = TransferNotProcessed;

    // Transfer specific
    if (Transfer->Transfer.Type != IsochronousTransfer) {
        Qh                              = (EhciQueueHead_t*)Transfer->EndpointDescriptor;
        Td                              = &Controller->QueueControl.TDPool[Qh->ChildIndex];
        QhAddress                       = EHCI_POOL_QHINDEX(Controller, Qh->Index);
        
        memset(&Qh->Overlay, 0, sizeof(EhciQueueHeadOverlay_t));
        Qh->Overlay.NextTD              = EHCI_POOL_TDINDEX(Controller, Td->Index);
        Qh->Overlay.NextAlternativeTD   = EHCI_LINK_END;

#ifdef __TRACE
        EhciDebugTransfer(Controller, Transfer);
#endif
    }
    else {
        iTd                             = (EhciIsochronousDescriptor_t*)Transfer->EndpointDescriptor;
    }

    // Acquire the spinlock for atomic queue access
    SpinlockAcquire(&Controller->Base.Lock);
    EhciSetPrefetching(Controller, Transfer->Transfer.Type, 0);

    // Bulk and control are asynchronous transfers
    if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
        // Transfer existing links
        Qh->LinkPointer = Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkPointer;
        Qh->LinkIndex   = Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkIndex;
        MemoryBarrier();

        // Insert at the start of queue
        Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkIndex   = Qh->Index;
        Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkPointer = QhAddress | EHCI_LINK_QH;

        // Enable the asynchronous scheduler
        Controller->QueueControl.AsyncTransactions++;
    }
    else {
        // Isochronous or interrupt, but handle each differently
        if (Transfer->Transfer.Type == InterruptTransfer) {
            EhciLinkPeriodicQh(Controller, Qh);
        }
        else {
            while (iTd) {
                EhciLinkPeriodicIsoc(Controller, iTd);
                // End of chain?
                if (iTd->QueueIndex != EHCI_NO_INDEX) {
                    iTd = &Controller->QueueControl.ITDPool[iTd->QueueIndex];
                }
                else {
                    break;
                }
            }
        }
    }

    // All queue operations are now done
    EhciSetPrefetching(Controller, Transfer->Transfer.Type, 1);
    EhciEnableScheduler(Controller, Transfer->Transfer.Type);
    SpinlockRelease(&Controller->Base.Lock);
    Transfer->Status = TransferQueued;
    return TransferQueued;
}

/* HciDequeueTransfer 
 * Removes a queued transfer from the controller's transfer list */
UsbTransferStatus_t
HciDequeueTransfer(
    _In_ UsbManagerTransfer_t*      Transfer)
{
    // Variables
    EhciController_t *Controller    = NULL;

    // Debug
    TRACE("HciDequeueTransfer()");

    // Get Controller
    Controller = (EhciController_t *)UsbManagerGetController(Transfer->DeviceId);
    if (Controller == NULL) {
        return TransferInvalid;
    }

    // Mark for unscheduling
    if (Transfer->Transfer.Type != IsochronousTransfer) {
        EhciTransactionFinalizeGeneric(Controller, Transfer);
    }
    else {
        EhciTransactionFinalizeIsoc(Controller, Transfer);
    }
    EhciRingDoorbell(Controller);
    return TransferFinished;
}
