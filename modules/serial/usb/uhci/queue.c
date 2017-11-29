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
 * Finish the FSBR implementation, right now there is no guarantee of order ls/fs/bulk
 */
//#define __TRACE

/* Includes
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>
#include "uhci.h"

/* Includes
 * - Library */
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
    _In_ size_t Value)
{
    // Variables
    size_t RetNum = 0;

    if (!(Value & 0xFFFF)) { // 16 Bits
        RetNum += 16;
        Value >>= 16;
    }
    if (!(Value & 0xFF)) { // 8 Bits
        RetNum += 8;
        Value >>= 8;
    }
    if (!(Value & 0xF)) { // 4 Bits
        RetNum += 4;
        Value >>= 4;
    }
    if (!(Value & 0x3)) { // 2 Bits
        RetNum += 2;
        Value >>= 2;
    }
    if (!(Value & 0x1)) { // 1 Bit
        RetNum++;
    }

    // Done
    return RetNum;
}

/* UhciDetermineInterruptQh
 * Determine Qh for Interrupt Transfer */
reg32_t
UhciDetermineInterruptQh(
    _In_ UhciController_t *Controller, 
    _In_ size_t Frame)
{
    // Variables
    int Index = 0;

    // Determine index from first free bit 8 queues
    Index = 8 - UhciFFS(Frame | UHCI_NUM_FRAMES);

    // If we are out of bounds then assume async queue
    if (Index < 2 || Index > 8) {
        Index = UHCI_QH_ASYNC;
    }

    // Retrieve physical address of the calculated qh
    return (UHCI_POOL_QHINDEX(Controller, Index) | UHCI_LINK_QH);
}

/* UhciGetStatusCode
 * Retrieves a status-code from a given condition code */
UsbTransferStatus_t
UhciGetStatusCode(
    _In_ int ConditionCode)
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
    _In_ UhciController_t *Controller)
{
    // Variables
    UhciTransferDescriptor_t *NullTd = NULL;
    uintptr_t NullTdPhysical = 0, NullQhPhysical = 0;
    UhciControl_t *Queue = &Controller->QueueControl;
    int i;
    
    // Initialize null-td
    NullTd = &Queue->TDPool[UHCI_POOL_TDNULL];
    NullTd->Header = (uint32_t)(UHCI_TD_PID_IN | UHCI_TD_DEVICE_ADDR(0x7F) | UHCI_TD_MAX_LEN(0x7FF));
    NullTd->Link = UHCI_LINK_END;
    NullTdPhysical = UHCI_POOL_TDINDEX(Controller, UHCI_POOL_TDNULL);

    // Enumerate all Qh's and initialize them
    for (i = 0; i < UHCI_POOL_QHS; i++) {
        Queue->QHPool[i].Flags = UHCI_QH_INDEX(i);
    }

    // Initialize interrupt-queue
    for (i = UHCI_QH_ISOCHRONOUS + 1; i < UHCI_QH_ASYNC; i++) {
        // All interrupts queues need to end in the async-head
        Queue->QHPool[i].Link = (UHCI_POOL_QHINDEX(Controller, UHCI_QH_ASYNC) 
            | UHCI_LINK_QH);
        Queue->QHPool[i].LinkIndex = UHCI_QH_ASYNC;

        // Initialize qh
        Queue->QHPool[i].Child = UHCI_LINK_END;
        Queue->QHPool[i].ChildIndex = UHCI_NO_INDEX;
        Queue->QHPool[i].Flags |= (UHCI_QH_SET_QUEUE(i) | UHCI_QH_ACTIVE);
    }

    // Initialize the null QH
    NullQhPhysical = UHCI_POOL_QHINDEX(Controller, UHCI_QH_NULL);
    Queue->QHPool[UHCI_QH_NULL].Link = (NullQhPhysical | UHCI_LINK_QH);
    Queue->QHPool[UHCI_QH_NULL].LinkIndex = UHCI_QH_NULL;
    Queue->QHPool[UHCI_QH_NULL].Child = NullTdPhysical;
    Queue->QHPool[UHCI_QH_NULL].ChildIndex = UHCI_POOL_TDNULL;
    Queue->QHPool[UHCI_QH_NULL].Flags |= UHCI_QH_ACTIVE;

    // Initialize the async QH
    Queue->QHPool[UHCI_QH_ASYNC].Link = UHCI_LINK_END;
    Queue->QHPool[UHCI_QH_ASYNC].LinkIndex = UHCI_NO_INDEX;
    Queue->QHPool[UHCI_QH_ASYNC].Child = NullTdPhysical;
    Queue->QHPool[UHCI_QH_ASYNC].ChildIndex = UHCI_POOL_TDNULL;
    Queue->QHPool[UHCI_QH_ASYNC].Flags |= UHCI_QH_ACTIVE;

    // 1024 Entries
    // Set all entries to the 8 interrupt queues, and we
    // want them interleaved such that some queues get visited more than others
    for (i = 0; i < UHCI_NUM_FRAMES; i++) {
        Queue->FrameList[i] = UhciDetermineInterruptQh(Controller, (size_t)i);
    }
    
    // No errors
    return OsSuccess;
}

/* UhciQueueInitialize
 * Initialize the controller's queue resources and resets counters */
OsStatus_t
UhciQueueInitialize(
    _In_ UhciController_t *Controller)
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
    PoolSize = 0x1000;
    PoolSize += UHCI_POOL_QHS * sizeof(UhciQueueHead_t);
    PoolSize += UHCI_POOL_TDS * sizeof(UhciTransferDescriptor_t);

    // Perform the allocation
    if (MemoryAllocate(PoolSize, MEMORY_CLEAN | MEMORY_COMMIT
        | MEMORY_LOWFIRST | MEMORY_CONTIGIOUS, &Pool, &PoolPhysical) != OsSuccess) {
        ERROR("Failed to allocate memory for resource-pool");
        return OsError;
    }

    // Initialize pointers
    Queue->QHPool = (UhciQueueHead_t*)((uint8_t*)Pool + 0x1000);
    Queue->QHPoolPhysical = PoolPhysical + 0x1000;
    Queue->TDPool = (UhciTransferDescriptor_t*)((uint8_t*)Pool + 0x1000 +
        (UHCI_POOL_QHS * sizeof(UhciTransferDescriptor_t)));
    Queue->TDPoolPhysical = PoolPhysical + 0x1000 +
        (UHCI_POOL_QHS * sizeof(UhciTransferDescriptor_t));
    
    // Update frame-list
    Queue->FrameList = (uintptr_t*)Pool;
    Queue->FrameListPhysical = PoolPhysical;

    // Initialize the transaction list
    Queue->TransactionList = CollectionCreate(KeyInteger);

    // Initialize the usb scheduler
    Controller->Scheduler = UsbSchedulerInitialize(UHCI_NUM_FRAMES, 900, 1);

    // Initialize internal data structures
    return UhciQueueResetInternalData(Controller);
}

/* UhciQueueReset
 * Removes and cleans up any existing transfers, then reinitializes. */
OsStatus_t
UhciQueueReset(
    _In_ UhciController_t *Controller)
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
    _foreach(tNode, Controller->QueueControl.TransactionList) {
        UhciTransactionFinalize(Controller, 
            (UsbManagerTransfer_t*)tNode->Data, 0);
    }
    CollectionClear(Controller->QueueControl.TransactionList);

    // Reinitialize internal data
    return UhciQueueResetInternalData(Controller);
}

/* UhciQueueDestroy
 * Cleans up any resources allocated by QueueInitialize */
OsStatus_t
UhciQueueDestroy(
    _In_ UhciController_t *Controller)
{
    // Variables
    size_t PoolSize = 0;

    // Debug
    TRACE("UhciQueueDestroy()");

    // Reset first
    UhciQueueReset(Controller);

    // Calculate how many bytes of memory we will need to free
    PoolSize = 0x1000;
    PoolSize += UHCI_POOL_QHS * sizeof(UhciQueueHead_t);
    PoolSize += UHCI_POOL_TDS * sizeof(UhciTransferDescriptor_t);

    // Cleanup resources
    UsbSchedulerDestroy(Controller->Scheduler);
    CollectionDestroy(Controller->QueueControl.TransactionList);
    MemoryFree(Controller->QueueControl.FrameList, PoolSize);
    return OsSuccess;
}

/* UhciUpdateCurrentFrame
 * Updates the current frame and stores it in the controller given.
 * OBS: Needs to be called regularly */
void
UhciUpdateCurrentFrame(
    _In_ UhciController_t *Controller)
{
    // Variables
    uint16_t FrameNo = 0;
    int Delta = 0;

    // Read the current frame, and use the last read frame to calculate the delta
    // then add to current frame
    FrameNo = UhciRead16(Controller, UHCI_REGISTER_FRNUM);
    Delta = (FrameNo - Controller->QueueControl.Frame) & UHCI_FRAME_MASK;
    Controller->QueueControl.Frame += Delta;
}

/* UhciConditionCodeToIndex
 * Converts the given condition-code in a TD to a string-index */
int
UhciConditionCodeToIndex(
    _In_ int ConditionCode)
{
    // Variables
    int bCount = 0;
    int Cc = ConditionCode;

    // Keep bit-shifting and count which bit is set
    for (; Cc != 0;) {
        bCount++;
        Cc >>= 1;
    }

    // Boundary check
    if (bCount >= 8) {
        bCount = 0;
    }

    // Done
    return bCount;
}

/* UhciGetTransferStatus
 * Determine an universal transfer-status from a given transfer-descriptor */
UsbTransferStatus_t
UhciGetTransferStatus(
    _In_ UhciTransferDescriptor_t *Td)
{
    // Variables
    int ConditionIndex = 0;
    
    // Convert to index, then status-code
    ConditionIndex = UhciConditionCodeToIndex(UHCI_TD_STATUS(Td->Flags));
    return UhciGetStatusCode(ConditionIndex);
}

/* UhciQhAllocate 
 * Allocates and prepares a new Qh for a usb-transfer. */
UhciQueueHead_t*
UhciQhAllocate(
    _In_ UhciController_t *Controller,
    _In_ UsbTransferType_t Type,
    _In_ UsbSpeed_t Speed)
{
    // Variables
    UhciQueueHead_t *Qh = NULL;
    int i;

    // Lock access to the queue
    SpinlockAcquire(&Controller->Base.Lock);

    // Now, we usually allocated new endpoints for interrupts
    // and isoc, but it doesn't make sense for us as we keep one
    // large pool of QH's, just allocate from that in any case
    for (i = UHCI_POOL_START; i < UHCI_POOL_QHS; i++) {
        // Skip in case already allocated
        if (Controller->QueueControl.QHPool[i].Flags & UHCI_QH_ACTIVE) {
            continue;
        }

        // We found a free qh - mark it allocated and end
        // but reset the QH first
        memset(&Controller->QueueControl.QHPool[i], 0, sizeof(UhciQueueHead_t));
        Controller->QueueControl.QHPool[i].Flags = UHCI_QH_ACTIVE 
            | UHCI_QH_INDEX(i) | UHCI_QH_TYPE((uint32_t)Type);
        
        // Determine which queue-priority
        if (Speed == LowSpeed && Type == ControlTransfer) {
            Controller->QueueControl.QHPool[i].Flags |= UHCI_QH_SET_QUEUE(UHCI_QH_LCTRL);
        }
        else if (Speed == FullSpeed && Type == ControlTransfer) {
            Controller->QueueControl.QHPool[i].Flags |= UHCI_QH_SET_QUEUE(UHCI_QH_FCTRL) | UHCI_QH_FSBR;
        }
        else if (Type == BulkTransfer) {
            Controller->QueueControl.QHPool[i].Flags |= UHCI_QH_SET_QUEUE(UHCI_QH_FBULK) | UHCI_QH_FSBR;
        }
        else {
            Controller->QueueControl.QHPool[i].Flags |= UHCI_QH_BANDWIDTH_ALLOC;
        }
        
        // Store pointer
        Qh = &Controller->QueueControl.QHPool[i];
        break;
    }
    
    // Release the lock, let others pass
    SpinlockRelease(&Controller->Base.Lock);
    return Qh;
}

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

/* UhciQhInitialize 
 * Initializes the links of the QH. */
void
UhciQhInitialize(
    _In_ UhciController_t *Controller,
    _In_ UhciQueueHead_t *Qh, 
    _In_ int HeadIndex)
{
    // Initialize link
    Qh->Link = UHCI_LINK_END;
    Qh->LinkIndex = UHCI_NO_INDEX;

    // Initialize children
    if (HeadIndex != -1) {
        Qh->Child = (reg32_t)UHCI_POOL_TDINDEX(Controller, HeadIndex);
        Qh->ChildIndex = (int16_t)HeadIndex;
    }
    else {
        Qh->Child = 0;
        Qh->ChildIndex = UHCI_NO_INDEX;
    }
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
    Td->Buffer = BufferAddress;

    // Store data
    Td->OriginalFlags = Td->Flags;
    Td->OriginalHeader = Td->Header;

    // Done
    return Td;
}

/* UhciUnlinkGeneric
 * Dequeues an generic queue-head from the scheduler. This does not do
 * any cleanup. */
OsStatus_t
UhciUnlinkGeneric(
    _In_ UhciController_t       *Controller,
    _In_ UsbManagerTransfer_t   *Transfer,
    _In_ UhciQueueHead_t        *Qh)
{
    // Variables
    int QhIndex     = -1;

    QhIndex         = UHCI_QH_GET_INDEX(Qh->Flags);
    if (Transfer->Transfer.Type == ControlTransfer
        || Transfer->Transfer.Type == BulkTransfer)
    {
        // Variables
        UhciQueueHead_t *PrevQh = &Controller->QueueControl.QHPool[UHCI_QH_ASYNC];

        // Iterate untill the current qh
        while (PrevQh->LinkIndex != QhIndex) {
            if (PrevQh->LinkIndex == UHCI_NO_INDEX) {
                break;
            }
            PrevQh = &Controller->QueueControl.QHPool[PrevQh->LinkIndex];
        }

        // Check that the qh even exists
        if (PrevQh->LinkIndex != QhIndex) {
            ERROR("UHCI: Couldn't find Qh in frame-list");
        }
        else {
            // Transfer the link to previous
            PrevQh->Link = Qh->Link;
            PrevQh->LinkIndex = Qh->LinkIndex;
            MemoryBarrier();

#ifdef UHCI_FSBR
            /* Get */
            int PrevQueue = UHCI_QH_GET_QUEUE(PrevQh->Flags);

            /* Deactivate FSBR? */
            if (PrevQueue < UHCI_POOL_FSBR
                && Queue >= UHCI_POOL_FSBR) {
                /* Link NULL to the next in line */
                Ctrl->QhPool[UHCI_POOL_NULL]->Link = Qh->Link;
                Ctrl->QhPool[UHCI_POOL_NULL]->LinkVirtual = Qh->LinkVirtual;

                /* Link last QH to NULL */
                PrevQh = Ctrl->QhPool[UHCI_POOL_ASYNC];
                while (PrevQh->LinkVirtual != 0)
                    PrevQh = (UhciQueueHead_t*)PrevQh->LinkVirtual;
                PrevQh->Link = (Ctrl->QhPoolPhys[UHCI_POOL_NULL] | UHCI_TD_LINK_QH);
                PrevQh->LinkVirtual = (uint32_t)Ctrl->QhPool[UHCI_POOL_NULL];
            }
#endif
        }
    }
    else if (Transfer->Transfer.Type == InterruptTransfer) {
        
        // Variables
        UhciQueueHead_t *ItrQh = NULL, *PrevQh = NULL;
        int Queue = UHCI_QH_GET_QUEUE(Qh->Flags);

        // Get initial qh of the queue
        // and find the correct spot
        ItrQh = &Controller->QueueControl.QHPool[Queue];
        while (ItrQh != Qh) {
            if (ItrQh->LinkIndex == UHCI_QH_NULL
                || ItrQh->LinkIndex == UHCI_NO_INDEX) {
                ItrQh = NULL;
                break;
            }

            // Go to next
            PrevQh = ItrQh;
            ItrQh = &Controller->QueueControl.QHPool[ItrQh->LinkIndex];
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
        }

        // Release bandwidth
        UsbSchedulerReleaseBandwidth(Controller->Scheduler, Qh->Period,
            Qh->Bandwidth, Qh->StartFrame, 1);
    }
    return OsSuccess;
}

/* UhciLinkIsochronous
 * Queue's up a isochronous transfer. The bandwidth must be allocated
 * before this function is called. */
OsStatus_t
UhciLinkIsochronous(
    _In_ UhciController_t *Controller,
    _In_ UhciQueueHead_t *Qh)
{
    // Variables
    uint32_t *Frames = (uint32_t*)Controller->QueueControl.FrameList;
    UhciTransferDescriptor_t *Td = NULL;
    uintptr_t PhysicalAddress = 0;
    size_t Frame = Qh->StartFrame;

    // Iterate through all td's in this transaction
    // and find the guilty
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    PhysicalAddress = UHCI_POOL_TDINDEX(Controller, Qh->ChildIndex);
    while (Td) {
        
        // Calculate the next frame
        Td->Frame = Frame;
        Frame += UHCI_QH_GET_QUEUE(Qh->Flags);

        // Insert td at start of frame
        Td->Link = Frames[Td->Frame];
        MemoryBarrier();
        Frames[Td->Frame] = PhysicalAddress;

        // Go to next td or terminate
        if (Td->LinkIndex != UHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
            PhysicalAddress = UHCI_POOL_TDINDEX(Controller, Td->LinkIndex);
        }
        else {
            break;
        }
    }

    // Done
    return OsSuccess;
}

/* UhciUnlinkIsochronous
 * Dequeues an isochronous queue-head from the scheduler. This does not do
 * any cleanup. */
OsStatus_t
UhciUnlinkIsochronous(
    _In_ UhciController_t *Controller,
    _In_ UhciQueueHead_t *Qh)
{
    // Variables
    uint32_t *Frames = (uint32_t*)Controller->QueueControl.FrameList;
    UhciTransferDescriptor_t *Td = NULL;
    uintptr_t PhysicalAddress = 0;

    // Iterate through all td's in this transaction
    // and find the guilty
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    PhysicalAddress = UHCI_POOL_TDINDEX(Controller, Qh->ChildIndex);
    while (Td) {
        if (Frames[Td->Frame] == PhysicalAddress) {
            Frames[Td->Frame] = Td->Link;
        }

        // Go to next td or terminate
        if (Td->LinkIndex != UHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
            PhysicalAddress = UHCI_POOL_TDINDEX(Controller, Td->LinkIndex);
        }
        else {
            break;
        }
    }

    // Done
    return OsSuccess;
}

/* UhciFixupToggles
 * Fixup toggles for a failed transaction, where the stored toggle might have
 * left the endpoint unsynchronized. */
void
UhciFixupToggles(
    _In_ UhciController_t *Controller,
    _In_ UsbManagerTransfer_t *Transfer)
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

    // Update endpoint toggle
    UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, Toggle ^ 1);

    // @todo update all queued transfers for same endpoint
    foreach(Node, Controller->QueueControl.TransactionList) {
        // Instantiate the usb-transfer pointer, and then the EP
        UsbManagerTransfer_t *NodeTransfer = (UsbManagerTransfer_t*)Node->Data;
        if (NodeTransfer != Transfer && NodeTransfer->Pipe == Transfer->Pipe) {
            //int Toggle = UsbManagerGetToggle(Transfer->DeviceId, Transfer->Pipe);
            Qh = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
            Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
        }
    }
}

/* UhciProcessRequest
 * Processes a transfer, and schedules it for finalization or reschuldes
 * a transfer if neccessary. */
void
UhciProcessRequest(
    _In_ UhciController_t *Controller,
    _In_ UsbManagerTransfer_t *Transfer,
    _In_ CollectionItem_t *Node,
    _In_ int FixupToggles,
    _In_ int ErrorTransfer,
    _In_ int RestartOnly)
{
    // Variables
    UhciTransferDescriptor_t *Td = NULL;
    UhciQueueHead_t *Qh = NULL;

    // Debug
    TRACE("UhciProcessRequest()");

    // Handle the transfer
    if (Transfer->Transfer.Type == ControlTransfer
        || Transfer->Transfer.Type == BulkTransfer) {

        // If neccessary fixup toggles
        if (Transfer->Transfer.Type == BulkTransfer && FixupToggles) {
            UhciFixupToggles(Controller, Transfer);
        }

        // Finalize transaction and remove from list
        if (UhciTransactionFinalize(Controller, Transfer, 1) == OsSuccess) {
            CollectionRemoveByNode(Controller->QueueControl.TransactionList, Node);
            CollectionDestroyNode(Controller->QueueControl.TransactionList, Node);
        }
    }
    else if (Transfer->Transfer.Type == InterruptTransfer) {

        // Variables
        uintptr_t BufferBase, BufferStep, BufferBaseUpdated;
        int SwitchToggles = 0;
        
        // Setup some variables
        Qh = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
        Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
        SwitchToggles = (DIVUP(Transfer->Transfer.Transactions[0].Length, 
            Transfer->Transfer.Endpoint.MaxPacketSize)) % 2;

        // Do we need to fix toggles?
        if (FixupToggles) {
            UhciFixupToggles(Controller, Transfer);
        }
        
        // Do some extra processing for periodics
        BufferBase = Transfer->Transfer.Transactions[0].BufferAddress;
        BufferStep = Transfer->Transfer.Endpoint.MaxPacketSize;

        // Restart transfer
        while (Td) {
            // Fixup toggles if not dividable by 2
            if (SwitchToggles || FixupToggles) {
                int Toggle = UsbManagerGetToggle(Transfer->DeviceId, Transfer->Pipe);
                Td->OriginalHeader &= ~UHCI_TD_DATA_TOGGLE;
                if (Toggle) {
                    Td->OriginalHeader |= UHCI_TD_DATA_TOGGLE;
                }
                UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, Toggle ^ 1);
            }
            
            // Adjust buffer
            if (!RestartOnly) {
                BufferBaseUpdated = ADDLIMIT(BufferBase, Td->Buffer, 
                    BufferStep, BufferBase + Transfer->Transfer.PeriodicBufferSize);
                Td->Buffer = LODWORD(BufferBaseUpdated);
            }

            // Restore
            Td->Header = Td->OriginalHeader;
            Td->Flags = Td->OriginalFlags;

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
        
        // Notify process of transfer of the status
        if (Transfer->Transfer.Type == InterruptTransfer && !RestartOnly) {
            if (Transfer->Transfer.UpdatesOn) {
                InterruptDriver(Transfer->Requester, 
                    (size_t)Transfer->Transfer.PeriodicData, 
                    (size_t)((ErrorTransfer == 0) ? TransferFinished : Transfer->Status), 
                    Transfer->CurrentDataIndex, 0);
            }

            // Increase
            Transfer->CurrentDataIndex = ADDLIMIT(0, Transfer->CurrentDataIndex,
                BufferStep, Transfer->Transfer.PeriodicBufferSize);
        }

        // Reinitialize the queue-head
        Qh->Child = UHCI_POOL_TDINDEX(Controller, Qh->ChildIndex);
    }
    else
    {
        // Variables
        reg32_t StartFrame = 0;

        // Enumerate td's and restart them
        Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
        while (Td) {
            // Restore
            Td->Header = Td->OriginalHeader;
            Td->Flags = Td->OriginalFlags;

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
        if (ErrorTransfer == 0) {
            StartFrame = Qh->StartFrame;
            Qh->StartFrame = (reg32_t)Controller->QueueControl.Frame;
            Qh->StartFrame += + UHCI_QH_GET_QUEUE(Qh->Flags);
            UhciLinkIsochronous(Controller, Qh);
            Qh->StartFrame = StartFrame;
        }
    }
}

/* UhciProcessTransfers 
 * Goes through all active transfers and handles them if they require
 * any handling. */
void
UhciProcessTransfers(
    _In_ UhciController_t  *Controller,
    _In_ int                Checkup)
{
    // Variables
    UhciTransferDescriptor_t *Td    = NULL;
    int ProcessQh                   = 0;
    
    // Debug
    TRACE("UhciProcessTransfers()");

    // Update current frame
    UhciUpdateCurrentFrame(Controller);

    // Iterate all active transactions and see if any
    // one of them needs unlinking or linking
    foreach(Node, Controller->QueueControl.TransactionList) {
        
        // Instantiate the usb-transfer pointer, and then the EP
        UsbManagerTransfer_t *Transfer = 
            (UsbManagerTransfer_t*)Node->Data;
        UhciQueueHead_t *Qh = 
            (UhciQueueHead_t*)Transfer->EndpointDescriptor;

        // Variables
        int ShortTransfer = 0;
        int ErrorTransfer = 0;
        int FixupToggles = 0;
        int RestartOnly = 0;
        ProcessQh = 0;

        // Iterate through all td's in this transaction
        // and find the guilty
        Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
        while (Td) {
            // Extract information from the td
            int CondCode = UhciConditionCodeToIndex(UHCI_TD_STATUS(Td->Flags));
            int BytesTransfered = UHCI_TD_ACTUALLENGTH(Td->Flags);
            int BytesRequest = UHCI_TD_GET_LEN(Td->Header);
  
            // Ignore active td's
            if (!(Td->Flags & UHCI_TD_ACTIVE)) {
                ProcessQh = 1;
                if (BytesRequest != UHCI_TD_LENGTH_MASK
                    && BytesTransfered < BytesRequest) {
                    ShortTransfer = 1;
                }

                // Do we simply need to restart transfer?
                if (CondCode == 3) {
                    // NAK Transfer - No data, just restart
                    RestartOnly = 1;
                    break;
                }

                // Sanitize the condition-code (!Done + !NAK)
                if (CondCode != 0 && CondCode != 3) {
                    Transfer->Status = UhciGetStatusCode(CondCode);
                    ErrorTransfer = 1;
                    break; // No need to continue
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

        // If bulk or interrupt, and error transfer, we might have
        // to fix the endpoint toggle
        if (Transfer->Transfer.Type == BulkTransfer
            || Transfer->Transfer.Type == InterruptTransfer) {
            if (ShortTransfer == 1 || ErrorTransfer == 1) {
                FixupToggles = 1;
            }
        }

        // Process transfer?
        if (ProcessQh) {
            UhciProcessRequest(Controller, Transfer, 
                Node, FixupToggles, ErrorTransfer, RestartOnly);
        }
    }
}
