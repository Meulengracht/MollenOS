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

/* EhciQhInitialize
 * This initiates any periodic scheduling information that might be needed */
OsStatus_t
EhciQhInitialize(
    _In_ EhciController_t*          Controller,
    _In_ UsbTransfer_t*             Transfer,
    _In_ EhciQueueHead_t*           Qh,
    _In_ size_t                     Address,
    _In_ size_t                     Endpoint)
{
    // Variables
    size_t EpBandwidth = MAX(3, Transfer->Endpoint.Bandwidth);

    // Initialize the QH
    Qh->Flags   = EHCI_QH_DEVADDR(Address);
    Qh->Flags  |= EHCI_QH_EPADDR(Endpoint);
    Qh->Flags  |= EHCI_QH_DTC;

    // The thing with maxlength is
    // that it needs to be MIN(TransferLength, MPS)
    Qh->Flags  |= EHCI_QH_MAXLENGTH(Transfer->Endpoint.MaxPacketSize);
    if (Transfer->Type == InterruptTransfer) {
        Qh->State = EHCI_QH_MULTIPLIER(EpBandwidth);
    }
    else {
        Qh->State = EHCI_QH_MULTIPLIER(1);
    }

    // Now, set additionals depending on speed
    if (Transfer->Speed == LowSpeed || Transfer->Speed == FullSpeed) {
        if (Transfer->Type == ControlTransfer) {
            Qh->Flags |= EHCI_QH_CONTROLEP;
        }

        // On low-speed, set this bit
        if (Transfer->Speed == LowSpeed) {
            Qh->Flags |= EHCI_QH_LOWSPEED;
        }

        // Set nak-throttle to 0
        Qh->Flags |= EHCI_QH_RL(0);

        // We need to fill the TT's hub-addr
        // and port-addr (@todo)
        ERROR("EHCI::Scheduling low/full-speed");
        for (;;);
    }
    else {
        // High speed device, no transaction translator
        Qh->Flags |= EHCI_QH_HIGHSPEED;

        // Set nak-throttle to 4 if control or bulk
        if (Transfer->Type == ControlTransfer || Transfer->Type == BulkTransfer) {
            Qh->Flags |= EHCI_QH_RL(4);
        }
        else {
            Qh->Flags |= EHCI_QH_RL(0);
        }
    }
                
    // Allocate bandwith if interrupt qh
    if (Transfer->Type == InterruptTransfer) {
        size_t TransactionsPerFrame = DIVUP(Transfer->Transactions[0].Length, Transfer->Endpoint.MaxPacketSize);
        Qh->Bandwidth               = (reg32_t)NS_TO_US(UsbCalculateBandwidth(Transfer->Speed, 
            Transfer->Endpoint.Direction, Transfer->Type, Transfer->Transactions[0].Length));
        
        // If highspeed calculate period as 2^(Interval-1)
        if (Transfer->Speed == HighSpeed) {
            Qh->Interval            = (1 << Transfer->Endpoint.Interval);
        }
        else {
            Qh->Interval            = Transfer->Endpoint.Interval;
        }

        // If we use completion masks we'll need another transfer for start
        if (Transfer->Speed != HighSpeed) {
            TransactionsPerFrame++;
        }

        // Allocate the needed bandwidth
        if (UsbSchedulerReserveBandwidth(Controller->Scheduler, Qh->Interval, 
            Qh->Bandwidth, TransactionsPerFrame, &Qh->sFrame, &Qh->sMask) != OsSuccess) {
            return OsError;
        }

        // Calculate both the frame start and completion mask
        Qh->FrameStartMask  = (uint8_t)FirstSetBit(Qh->sMask);
        if (Transfer->Speed != HighSpeed) {
            Qh->FrameCompletionMask = (uint8_t)(Qh->sMask & 0xFF);
            Qh->FrameCompletionMask &= ~(1 << Qh->FrameStartMask);
        }
        else {
            Qh->FrameCompletionMask = 0;
        }
    }





        Qh                              = (EhciQueueHead_t*)Transfer->EndpointDescriptor;
        Td                              = &Controller->QueueControl.TDPool[Qh->ChildIndex];
        QhAddress                       = EHCI_POOL_QHINDEX(Controller, Qh->Index);
        
        memset(&Qh->Overlay, 0, sizeof(EhciQueueHeadOverlay_t));
        Qh->Overlay.NextTD              = EHCI_POOL_TDINDEX(Controller, Td->Index);
        Qh->Overlay.NextAlternativeTD   = EHCI_LINK_END;
    return OsSuccess;
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
    WARNING("EHCI: QH at 0x%x, FirstTd 0x%x, NextQh 0x%x", PhysicalAddress, Qh->Current, Qh->LinkPointer);
    WARNING("      Bandwidth %u, StartFrame %u, Flags 0x%x", Qh->Object.Bandwidth, Qh->Object.StartFrame, Qh->Flags);
}

/* EhciRestartQh
 * Restarts an interrupt QH by resetting it to it's start state */
void
EhciRestartQh(
    EhciController_t*               Controller, 
    UsbManagerTransfer_t*           Transfer)
{
    // Variables
    EhciTransferDescriptor_t *Td    = NULL;
    EhciQueueHead_t *Qh             = NULL;
    
    // Setup some variables
    Qh                  = (EhciQueueHead_t*)Transfer->EndpointDescriptor;
    Td                  = &Controller->QueueControl.TDPool[Qh->ChildIndex];

    // Iterate td's
    while (Td) {
        EhciRestartTd(Controller, Transfer, Td);
        if (Td->LinkIndex != EHCI_NO_INDEX) {
            Td                      = &Controller->QueueControl.TDPool[Td->LinkIndex];
        }
        else {
            break;
        }
    }

    // Set Qh to point to first
    Td                              = &Controller->QueueControl.TDPool[Qh->ChildIndex];

    // Zero out overlay
    memset(&Qh->Overlay, 0, sizeof(EhciQueueHeadOverlay_t));

    // Update links
    Qh->Overlay.NextTD              = EHCI_POOL_TDINDEX(Controller, Td->Index);
    Qh->Overlay.NextAlternativeTD   = EHCI_LINK_END;
    
    // Reset transfer status
    Transfer->Status = TransferQueued;
}

/* EhciScanQh
 * Scans a chain of td's to check whether or not it has been processed. Returns 1
 * if there was work done - otherwise 0 if untouched. */
int
EhciScanQh(
    _In_ EhciController_t*          Controller, 
    _In_ UsbManagerTransfer_t*      Transfer)
{
    // Variables
    EhciTransferDescriptor_t *Td    = NULL;
    EhciQueueHead_t *Qh             = NULL;
    int ProcessQh                   = 0;
    int ShortTransfer               = 0;
    
    // Setup some variables
    Qh                  = (EhciQueueHead_t*)Transfer->EndpointDescriptor;
    Td                  = &Controller->QueueControl.TDPool[Qh->ChildIndex];

    // Is Qh already in process of being unmade?
    if (Qh->HcdFlags & EHCI_HCDFLAGS_UNSCHEDULE) {
        return 0;
    }
    
    // Iterate td's
    while (Td) {
        int Code = EhciScanTd(Controller, Transfer, Td, &ShortTransfer);
        if (ShortTransfer == 1) {
            Qh->HcdFlags |= EHCI_HCDFLAGS_SHORTTRANSFER;
        }
        
        if (Code != 0) { // Error transfer
            break;
        }
        else if (Code == 0 && Transfer->Status != TransferFinished) { // Not touched
            break;
        }
        
        ProcessQh           = 1;
        if (Td->LinkIndex != EHCI_NO_INDEX) {
            Td              = &Controller->QueueControl.TDPool[Td->LinkIndex];
        }
        else {
            break;
        }
    }
    return ProcessQh;
}
/* EhciLinkGeneric
 * Generic link to asynchronous list */
__EXTERN
void
EhciLinkGeneric(
    _In_ EhciController_t*  Controller, 
    _In_ EhciQueueHead_t*   Qh)
{
    // Transfer existing links
    Qh->LinkPointer = Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkPointer;
    Qh->LinkIndex   = Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkIndex;
    MemoryBarrier();

    // Insert at the start of queue
    Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkIndex   = Qh->Index;
    Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkPointer = QhAddress | EHCI_LINK_QH;

    // Enable the asynchronous scheduler
    Controller->AsyncTransactions++;
}

/* EhciUnlinkGeneric
 * Generic unlink from asynchronous list */
void
EhciUnlinkGeneric(
    _In_ EhciController_t*  Controller, 
    _In_ EhciQueueHead_t*   Qh)
{
    // Variables
    EhciQueueHead_t *PrevQh = NULL;
    
    // Perform an asynchronous memory unlink
    PrevQh = &Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC];
    while (PrevQh->LinkIndex != Qh->Index
        && PrevQh->LinkIndex != EHCI_NO_INDEX) {
        PrevQh = &Controller->QueueControl.QHPool[PrevQh->LinkIndex];
    }

    // Now make sure we skip over our qh
    PrevQh->LinkPointer = Qh->LinkPointer;
    PrevQh->LinkIndex   = Qh->LinkIndex;
}
