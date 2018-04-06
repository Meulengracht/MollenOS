/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * Todo:
 * Power Management
 */
//#define __TRACE

/* Includes
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>
#include "uhci.h"

/* Includes
 * - Library */
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/* UhciErrorMessages
 * Textual representations of the possible error codes */
const char *UhciErrorMessages[] = {
    "No Error",
    "Bitstuff Error",
    "CRC/Timeout Error",
    "NAK Recieved",
    "Babble Detected",
    "Data Buffer Error",
    "Stalled",
    "Active"
};

/* UhciFFS
 * This function calculates the first free set of bits in a value */
size_t
UhciFFS(
    _In_ size_t                 Value)
{
    // Variables
    size_t Set = 0;

    if (!(Value & 0xFFFF)) { // 16 Bits
        Set += 16;
        Value >>= 16;
    }
    if (!(Value & 0xFF)) { // 8 Bits
        Set += 8;
        Value >>= 8;
    }
    if (!(Value & 0xF)) { // 4 Bits
        Set += 4;
        Value >>= 4;
    }
    if (!(Value & 0x3)) { // 2 Bits
        Set += 2;
        Value >>= 2;
    }
    if (!(Value & 0x1)) { // 1 Bit
        Set++;
    }
    return Set;
}

/* UhciDetermineInterruptQh
 * Determine Qh for Interrupt Transfer */
reg32_t
UhciDetermineInterruptQh(
    _In_ UhciController_t*      Controller, 
    _In_ size_t                 Frame)
{
    // Variables
    int Index = 0;

    // Determine index from first free bit 8 queues
    Index = 8 - UhciFFS(Frame | UHCI_NUM_FRAMES);

    // If we are out of bounds then assume async queue
    if (Index < 2 || Index > 8) {
        Index = UHCI_POOL_QH_ASYNC;
    }

    // Retrieve physical address of the calculated qh
    return (UHCI_POOL_QHINDEX(Controller, Index) | UHCI_LINK_QH);
}

/* UhciGetStatusCode
 * Retrieves a status-code from a given condition code */
UsbTransferStatus_t
UhciGetStatusCode(
    _In_ int                    ConditionCode)
{
    // One huuuge if/else
    if (ConditionCode == 0) {
        return TransferFinished;
    }
    else if (ConditionCode == 6) {
        return TransferStalled;
    }
    else if (ConditionCode == 1) {
        return TransferInvalidToggles;
    }
    else if (ConditionCode == 2) {
        return TransferNotResponding;
    }
    else if (ConditionCode == 3) {
        return TransferNAK;
    }
    else if (ConditionCode == 4) {
        return TransferBabble;
    }
    else if (ConditionCode == 5) {
        return TransferBufferError;
    }
    else {
        TRACE("Error: 0x%x (%s)", ConditionCode, UhciErrorMessages[ConditionCode]);
        return TransferInvalid;
    }
}

/* UhciQueueResetInternalData
 * Removes and cleans up any existing transfers, then reinitializes. */
OsStatus_t
UhciQueueResetInternalData(
    _In_ UhciController_t*      Controller)
{
    // Variables
    UhciTransferDescriptor_t *NullTd    = NULL;
    UhciControl_t *Queue                = &Controller->QueueControl;
    uintptr_t NullQhPhysical            = 0;
    uintptr_t NullTdPhysical            = 0;
    int i;

    // Reset all tds and qhs
    memset((void*)Queue->QHPool, 0, sizeof(UhciQueueHead_t) * UHCI_POOL_QHS);
    memset((void*)Queue->TDPool, 0, sizeof(UhciTransferDescriptor_t) * UHCI_POOL_TDS);
    NullTd                                      = &Queue->TDPool[UHCI_POOL_TD_NULL];
    NullTdPhysical                              = UHCI_POOL_TDINDEX(Controller, UHCI_POOL_TD_NULL);

    // Initialize interrupt-queue
    for (i = UHCI_POOL_QH_ISOCHRONOUS + 1; i < UHCI_POOL_QH_ASYNC; i++) {
        // Initialize qh
        Queue->QHPool[i].Flags                  = UHCI_QH_TYPE(2) | UHCI_QH_ALLOCATED;
        Queue->QHPool[i].Queue                  = (i & 0xFF);
        Queue->QHPool[i].Index                  = (int16_t)i;

        // All interrupts queues need to end in the async-head
        Queue->QHPool[i].Link                   = (UHCI_POOL_QHINDEX(Controller, UHCI_POOL_QH_ASYNC) | UHCI_LINK_QH);
        Queue->QHPool[i].LinkIndex              = UHCI_POOL_QH_ASYNC;

        Queue->QHPool[i].Child                  = UHCI_LINK_END;
        Queue->QHPool[i].ChildIndex             = UHCI_NO_INDEX;
    }

    // Initialize the null QH
    NullQhPhysical                              = UHCI_POOL_QHINDEX(Controller, UHCI_POOL_QH_NULL);
    Queue->QHPool[UHCI_POOL_QH_NULL].Link       = (NullQhPhysical | UHCI_LINK_QH);
    Queue->QHPool[UHCI_POOL_QH_NULL].LinkIndex  = UHCI_POOL_QH_NULL;
    Queue->QHPool[UHCI_POOL_QH_NULL].Child      = NullTdPhysical;
    Queue->QHPool[UHCI_POOL_QH_NULL].ChildIndex = UHCI_POOL_TD_NULL;
    Queue->QHPool[UHCI_POOL_QH_NULL].Flags      = UHCI_QH_ALLOCATED;
    Queue->QHPool[UHCI_POOL_QH_NULL].Index      = UHCI_POOL_QH_NULL;

    // Initialize the async QH
    Queue->QHPool[UHCI_POOL_QH_ASYNC].Link      = UHCI_LINK_END;
    Queue->QHPool[UHCI_POOL_QH_ASYNC].LinkIndex = UHCI_NO_INDEX;
    Queue->QHPool[UHCI_POOL_QH_ASYNC].Child     = NullTdPhysical;
    Queue->QHPool[UHCI_POOL_QH_ASYNC].ChildIndex = UHCI_POOL_TD_NULL;
    Queue->QHPool[UHCI_POOL_QH_ASYNC].Flags     = UHCI_QH_ALLOCATED;
    Queue->QHPool[UHCI_POOL_QH_ASYNC].Index     = UHCI_POOL_QH_ASYNC;
    
    // Initialize null-td
    NullTd->Header                              = (reg32_t)(UHCI_TD_PID_IN | UHCI_TD_DEVICE_ADDR(0x7F) | UHCI_TD_MAX_LEN(0x7FF));
    NullTd->Link                                = UHCI_LINK_END;
    NullTd->LinkIndex                           = UHCI_NO_INDEX;
    NullTd->HcdFlags                            = UHCI_TD_ALLOCATED;
    NullTd->Index                               = UHCI_POOL_TD_NULL;
    
    // 1024 Entries
    // Set all entries to the 8 interrupt queues, and we
    // want them interleaved such that some queues get visited more than others
    for (i = 0; i < UHCI_NUM_FRAMES; i++) {
        Queue->FrameList[i]             = UhciDetermineInterruptQh(Controller, (size_t)i);
    }
    return OsSuccess;
}

/* UhciQueueInitialize
 * Initialize the controller's queue resources and resets counters */
OsStatus_t
UhciQueueInitialize(
    _In_ UhciController_t*      Controller)
{
    // Variables
    UhciControl_t *Queue    = &Controller->QueueControl;
    uintptr_t PoolPhysical  = 0;
    size_t PoolSize         = 0;
    void *Pool              = NULL;

    // Trace
    TRACE("UhciQueueInitialize()");

    // Null out queue-control
    memset(Queue, 0, sizeof(UhciControl_t));

    // Calculate how many bytes of memory we will need
    PoolSize                            = 0x1000;
    PoolSize                            += UHCI_POOL_QHS * sizeof(UhciQueueHead_t);
    PoolSize                            += UHCI_POOL_TDS * sizeof(UhciTransferDescriptor_t);
    Controller->QueueControl.PoolBytes  = PoolSize;

    // Perform the allocation
    if (MemoryAllocate(NULL, PoolSize, MEMORY_CLEAN | MEMORY_COMMIT
        | MEMORY_LOWFIRST | MEMORY_CONTIGIOUS, &Pool, &PoolPhysical) != OsSuccess) {
        ERROR("Failed to allocate memory for resource-pool");
        return OsError;
    }

    // Physical address of pool must be below 4gb
    assert(PoolPhysical < 0xFFFFFFFF);

    // Initialize pointers
    Queue->QHPool           = (UhciQueueHead_t*)((uint8_t*)Pool + 0x1000);
    Queue->QHPoolPhysical   = PoolPhysical + 0x1000;
    Queue->TDPool           = (UhciTransferDescriptor_t*)((uint8_t*)Pool + 0x1000 + (UHCI_POOL_QHS * sizeof(UhciTransferDescriptor_t)));
    Queue->TDPoolPhysical   = PoolPhysical + 0x1000 + (UHCI_POOL_QHS * sizeof(UhciTransferDescriptor_t));
    
    // Update frame-list
    Queue->FrameList        = (reg32_t*)Pool;
    Queue->FrameListPhysical = (reg32_t)PoolPhysical;

    // Initialize the usb scheduler
    Controller->Scheduler   = UsbSchedulerInitialize(UHCI_NUM_FRAMES, 900, 1);

    // Initialize internal data structures
    return UhciQueueResetInternalData(Controller);
}

/* UhciQueueReset
 * Removes and cleans up any existing transfers, then reinitializes. */
OsStatus_t
UhciQueueReset(
    _In_ UhciController_t*      Controller)
{
    // Variables
    CollectionItem_t *tNode = NULL;

    // Debug
    TRACE("UhciQueueReset()");

    // Stop Controller
    if (UhciStop(Controller) != OsSuccess) {
        ERROR("Failed to stop the controller");
        return OsError;
    }

    // Iterate all queued transactions and dequeue
    _foreach(tNode, Controller->Base.TransactionList) {
        UhciTransactionFinalize(Controller, (UsbManagerTransfer_t*)tNode->Data, 0);
    }
    CollectionClear(Controller->Base.TransactionList);
    return UhciQueueResetInternalData(Controller);
}

/* UhciQueueDestroy
 * Cleans up any resources allocated by QueueInitialize */
OsStatus_t
UhciQueueDestroy(
    _In_ UhciController_t*      Controller)
{
    // Debug
    TRACE("UhciQueueDestroy()");

    // Reset first
    UhciQueueReset(Controller);

    // Cleanup resources
    UsbSchedulerDestroy(Controller->Scheduler);
    CollectionDestroy(Controller->Base.TransactionList);
    MemoryFree(Controller->QueueControl.FrameList, Controller->QueueControl.PoolBytes);
    return OsSuccess;
}

/* UhciUpdateCurrentFrame
 * Updates the current frame and stores it in the controller given.
 * OBS: Needs to be called regularly */
void
UhciUpdateCurrentFrame(
    _In_ UhciController_t*      Controller)
{
    // Variables
    uint16_t FrameNo    = 0;
    int Delta           = 0;

    // Read the current frame, and use the last read frame to calculate the delta
    // then add to current frame
    FrameNo                         = UhciRead16(Controller, UHCI_REGISTER_FRNUM);
    Delta                           = (FrameNo - Controller->QueueControl.Frame) & UHCI_FRAME_MASK;
    Controller->QueueControl.Frame += Delta;
}

/* UhciConditionCodeToIndex
 * Converts the given condition-code in a TD to a string-index */
int
UhciConditionCodeToIndex(
    _In_ int                    ConditionCode)
{
    // Variables
    int bCount  = 0;
    int Cc      = ConditionCode;

    // Keep bit-shifting and count which bit is set
    for (; Cc != 0;) {
        bCount++;
        Cc >>= 1;
    }
    if (bCount >= 8) {
        bCount = 0;
    }
    return bCount;
}

/* UhciUnlinkGeneric
 * Dequeues an generic queue-head from the scheduler. This does not do
 * any cleanup. */
OsStatus_t
UhciUnlinkGeneric(
    _In_ UhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer,
    _In_ UhciQueueHead_t*       Qh)
{
    // Handle unlinking based on type of transfer
    if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
        UhciQueueHead_t *PrevQh = &Controller->QueueControl.QHPool[UHCI_POOL_QH_ASYNC];
        while (PrevQh->LinkIndex != Qh->Index) {
            if (PrevQh->LinkIndex == UHCI_NO_INDEX) {
                break;
            }
            PrevQh = &Controller->QueueControl.QHPool[PrevQh->LinkIndex];
        }

        // Check that the qh even exists
        if (PrevQh->LinkIndex != Qh->Index) {
            ERROR("UHCI: Couldn't find Qh in frame-list");
        }
        else {
            // Transfer the link to previous
            PrevQh->Link        = Qh->Link;
            PrevQh->LinkIndex   = Qh->LinkIndex;
            MemoryBarrier();
        }
    }
    else if (Transfer->Transfer.Type == InterruptTransfer) {
        UhciQueueHead_t *PrevQh     = NULL;
        UhciQueueHead_t *ItrQh      = &Controller->QueueControl.QHPool[Qh->Queue];

        // Get initial qh of the queue and find the correct spot
        while (ItrQh != Qh) {
            if (ItrQh->LinkIndex == UHCI_POOL_QH_NULL || ItrQh->LinkIndex == UHCI_NO_INDEX) {
                ItrQh = NULL;
                break;
            }
            PrevQh                  = ItrQh;
            ItrQh                   = &Controller->QueueControl.QHPool[ItrQh->LinkIndex];
        }

        // If ItrQh is null it didn't exist
        if (ItrQh == NULL) {
            TRACE("UHCI: Tried to unschedule a queue-qh that didn't exist in queue");
        }
        else {
            // If there is a previous transfer link, should always happen
            if (PrevQh != NULL) {
                PrevQh->Link = Qh->Link;
                PrevQh->LinkIndex = Qh->LinkIndex;
                MemoryBarrier();
            }
            UsbSchedulerReleaseBandwidth(Controller->Scheduler, Qh->Period, Qh->Bandwidth, Qh->StartFrame, 0);
        }
    }
    return OsSuccess;
}

/* UhciLinkIsochronous
 * Queue's up a isochronous transfer. The bandwidth must be allocated
 * before this function is called. */
OsStatus_t
UhciLinkIsochronous(
    _In_ UhciController_t*      Controller,
    _In_ UhciQueueHead_t*       Qh)
{
    // Variables
    UhciTransferDescriptor_t *Td    = NULL;
    uintptr_t PhysicalAddress       = 0;
    reg32_t *FrameList              = (reg32_t*)Controller->QueueControl.FrameList;
    size_t Frame                    = Qh->StartFrame;

    // Iterate through all td's in this transaction
    // and find the guilty
    Td                          = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    PhysicalAddress             = UHCI_POOL_TDINDEX(Controller, Qh->ChildIndex);
    while (Td) {
        Td->Frame               = Frame;
        Frame                   += Qh->Queue;

        // Insert td at start of frame
        Td->Link                = FrameList[Td->Frame];
        MemoryBarrier();
        FrameList[Td->Frame]    = (reg32_t)PhysicalAddress;

        // Go to next td or terminate
        if (Td->LinkIndex != UHCI_NO_INDEX) {
            Td                  = &Controller->QueueControl.TDPool[Td->LinkIndex];
            PhysicalAddress     = UHCI_POOL_TDINDEX(Controller, Td->LinkIndex);
        }
        else {
            break;
        }
    }
    return OsSuccess;
}

/* UhciUnlinkIsochronous
 * Dequeues an isochronous queue-head from the scheduler. This does not do
 * any cleanup. */
OsStatus_t
UhciUnlinkIsochronous(
    _In_ UhciController_t*      Controller,
    _In_ UhciQueueHead_t*       Qh)
{
    // Variables
    UhciTransferDescriptor_t *Td    = NULL;
    uintptr_t PhysicalAddress       = 0;
    reg32_t *Frames                 = Controller->QueueControl.FrameList;

    // Iterate through all td's in this transaction
    // and find the guilty
    Td                          = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    PhysicalAddress             = UHCI_POOL_TDINDEX(Controller, Qh->ChildIndex);
    while (Td) {
        if (Frames[Td->Frame] == (reg32_t)PhysicalAddress) {
            Frames[Td->Frame]   = Td->Link;
        }

        // Go to next td or terminate
        if (Td->LinkIndex != UHCI_NO_INDEX) {
            Td                  = &Controller->QueueControl.TDPool[Td->LinkIndex];
            PhysicalAddress     = UHCI_POOL_TDINDEX(Controller, Td->LinkIndex);
        }
        else {
            break;
        }
    }
    return OsSuccess;
}

/* UhciFixupToggles
 * Fixup toggles for a failed transaction, where the stored toggle might have
 * left the endpoint unsynchronized. */
void
UhciFixupToggles(
    _In_ UhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer,
    _In_ int                    Single)
{
    // Variables
    UhciTransferDescriptor_t *Td    = NULL;
    UhciQueueHead_t *Qh             = NULL;
    int Toggle                      = 0;

    // Get qh from transfer
    Qh = (UhciQueueHead_t*)Transfer->EndpointDescriptor;

    // Now we iterate untill it went wrong, storing
    // the toggle-state, and when we reach the wrong
    // we flip the toggle and set it to EP
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    while (Td) {
        // If the td is processed switch toggle
        if (!(Td->Flags & UHCI_TD_ACTIVE)) {
            if (Td->Header & UHCI_TD_DATA_TOGGLE) {
                Toggle = 1;
            }
            else {
                Toggle = 0;
            }
        }

        // Go to next td or terminate
        if (Td->LinkIndex != UHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
        }
        else {
            break;
        }
    }

    // If the recursive call is active - skip next step and just update toggle
    if (Single) {
        UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, Toggle ^ 1);
        return;
    }

    // Update endpoint toggle
    Toggle ^= 1;
    if (UsbManagerGetToggle(Transfer->DeviceId, Transfer->Pipe) != Toggle) {
        UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, Toggle);

        // Update all queued transfers for same endpoint
        foreach(Node, Controller->Base.TransactionList) {
            UsbManagerTransfer_t *NodeTransfer = (UsbManagerTransfer_t*)Node->Data;
            if (NodeTransfer != Transfer && 
                NodeTransfer->Pipe == Transfer->Pipe &&
                NodeTransfer->Status == TransferQueued) {
                UhciFixupToggles(Controller, NodeTransfer, 1);
            }
        }
    }
}

/* UhciProcessRequest
 * Processes a transfer, and schedules it for finalization or reschuldes
 * a transfer if neccessary. */
OsStatus_t
UhciProcessRequest(
    _In_ UhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer,
    _In_ CollectionItem_t*      Node,
    _In_ int                    FixupToggles)
{
    // Debug
    TRACE("UhciProcessRequest()");

    // If neccessary fixup toggles
    if ((Transfer->Transfer.Type == BulkTransfer || 
         Transfer->Transfer.Type == InterruptTransfer) && FixupToggles) {
        UhciFixupToggles(Controller, Transfer, 0);
    }

    // Handle the transfer
    if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
        return UhciTransactionFinalize(Controller, Transfer, 1);
    }
    else if (Transfer->Transfer.Type == InterruptTransfer) {
        UhciQhRestart(Controller, Transfer);
        return OsError; // No unlink
    }
    else
    {
        // Variables
        UhciTransferDescriptor_t *Td    = NULL;
        UhciQueueHead_t *Qh             = NULL;
        reg32_t StartFrame              = 0;

        // Enumerate td's and restart them
        Qh = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
        Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
        while (Td) {
            // Restore
            Td->Header  = Td->OriginalHeader;
            Td->Flags   = Td->OriginalFlags;

            // Restore IoC
            if (Td->LinkIndex == UHCI_NO_INDEX) {
                Td->Flags |= UHCI_TD_IOC;
            }

            // Go to next td
            if (Td->LinkIndex != UHCI_NO_INDEX) {
                Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
            }
            else {
                break;
            }
        }
        
        // Link the isochronous request in again
        if (Transfer->Status == TransferFinished) {
            StartFrame          = Qh->StartFrame;
            Qh->StartFrame      = (reg32_t)Controller->QueueControl.Frame;
            Qh->StartFrame      += Qh->Queue;
            UhciLinkIsochronous(Controller, Qh);
            Qh->StartFrame      = StartFrame;
            Transfer->Status    = TransferQueued;
        }
        else {
            UsbManagerSendNotification(Transfer);
        }
        return OsError; // no unlink
    }
}

/* UhciProcessTransfers 
 * Goes through all active transfers and handles them if they require
 * any handling. */
void
UhciProcessTransfers(
    _In_ UhciController_t*  Controller)
{
    // Debug
    TRACE("UhciProcessTransfers()");

    // Update current frame
    UhciUpdateCurrentFrame(Controller);

    // Iterate all active transactions and see if any
    // one of them needs unlinking or linking
    foreach_nolink(Node, Controller->Base.TransactionList) {
        UsbManagerTransfer_t *Transfer  = (UsbManagerTransfer_t*)Node->Data;
        UhciQueueHead_t *Qh             = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
        int FixupToggles                = 0;
        if (Transfer->Status != TransferQueued) {
            Node = CollectionNext(Node);
            continue;
        }

        UhciQhValidate(Controller, Transfer, Qh);
        if (Transfer->Status != TransferQueued) {
            if (Transfer->Transfer.Type == BulkTransfer || Transfer->Transfer.Type == InterruptTransfer) {
                if ((Qh->Flags & UHCI_QH_SHORTTRANSFER) || Transfer->Status != TransferFinished) {
                    FixupToggles = 1;
                }
            }
            if (UhciProcessRequest(Controller, Transfer, Node, FixupToggles) == OsSuccess) {
                CollectionItem_t *NextNode = CollectionUnlinkNode(Controller->Base.TransactionList, Node);
                CollectionDestroyNode(Controller->Base.TransactionList, Node);
                Node = NextNode;
                free(Transfer);
                continue;
            }
        }
        
        // Go to next
        Node = CollectionNext(Node);
    }
}
