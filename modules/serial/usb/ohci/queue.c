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
 * MollenOS MCore - Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>
#include "ohci.h"

/* Includes
 * - Library */
#include <ds/collection.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* OhciErrorMessages
 * Textual representations of the possible error codes */
const char *OhciErrorMessages[] = {
    "No Error",
    "CRC Error",
    "Bit Stuffing Violation",
    "Data Toggle Mismatch",
    "Stall PID recieved",
    "Device Not Responding",
    "PID Check Failure",
    "Unexpected PID",
    "Data Overrun",
    "Data Underrun",
    "Reserved",
    "Reserved",
    "Buffer Overrun",
    "Buffer Underrun",
    "Not Accessed",
    "Not Accessed"
};

// Queue Balancing - Not Used since refactoring 
/*static const int _Balance[] = { 
    0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 
}; */

/* OhciQueueResetInternalData
 * Removes and cleans up any existing transfers, then reinitializes. */
OsStatus_t
OhciQueueResetInternalData(
    _In_ OhciController_t *Controller)
{
    // Variables
    OhciTransferDescriptor_t *NullTd    = NULL;
    OhciControl_t *Queue                = &Controller->QueueControl;
    uintptr_t NullPhysical              = 0;
    int i;
    
    // Reset indexes
    Queue->TransactionQueueBulkIndex = OHCI_NO_INDEX;
    Queue->TransactionQueueControlIndex = OHCI_NO_INDEX;
    for (i = 0; i < OHCI_FRAMELIST_SIZE; i++) {
        Queue->RootTable[i] = NULL;
    }

    // Initialize the null-td
    NullTd = &Queue->TDPool[OHCI_POOL_TDNULL];
    NullTd->BufferEnd = 0;
    NullTd->Cbp     = 0;
    NullTd->Link    = 0;
    NullTd->Flags   = OHCI_TD_ALLOCATED;
    NullPhysical    = OHCI_POOL_TDINDEX(Controller, OHCI_POOL_TDNULL);

    // Enumerate the ED pool and initialize them
    for (i = 0; i < OHCI_POOL_QHS; i++) {
        // Mark it skippable and set a NULL td
        Queue->QHPool[i].HcdInformation = 0;
        Queue->QHPool[i].Flags      = OHCI_QH_SKIP;
        Queue->QHPool[i].EndPointer = LODWORD(NullPhysical);
        Queue->QHPool[i].Current    = LODWORD((NullPhysical | OHCI_LINK_HALTED));
        Queue->QHPool[i].ChildIndex = OHCI_NO_INDEX;
        Queue->QHPool[i].LinkIndex  = OHCI_NO_INDEX;
        Queue->QHPool[i].Index      = (int16_t)i;
    }

    // Enumerate the TD pool and initialize them
    for (i = 0; i < OHCI_POOL_TDNULL; i++) {
        Queue->TDPool[i].Flags      = 0;
        Queue->TDPool[i].Link       = 0;
        Queue->TDPool[i].Index      = (int16_t)i;
        Queue->TDPool[i].LinkIndex  = OHCI_NO_INDEX;
    }

    // Done
    return OsSuccess;
}

/* OhciQueueInitialize
 * Initialize the controller's queue resources and resets counters */
OsStatus_t
OhciQueueInitialize(
    _In_ OhciController_t *Controller)
{
    // Variables
    OhciControl_t *Queue    = &Controller->QueueControl;
    uintptr_t PoolPhysical  = 0;
    size_t PoolSize         = 0;
    void *Pool              = NULL;

    // Trace
    TRACE("OhciQueueInitialize()");

    // Null out queue-control
    memset(Queue, 0, sizeof(OhciControl_t));

    // Calculate how many bytes of memory we will need
    PoolSize    = OHCI_POOL_QHS * sizeof(OhciQueueHead_t);
    PoolSize    += OHCI_POOL_TDS * sizeof(OhciTransferDescriptor_t);

    // Allocate both TD's and ED's pool
    if (MemoryAllocate(NULL, PoolSize, MEMORY_CLEAN | MEMORY_COMMIT
        | MEMORY_LOWFIRST | MEMORY_CONTIGIOUS, &Pool, &PoolPhysical) != OsSuccess) {
        ERROR("Failed to allocate memory for resource-pool");
        return OsError;
    }

    // Initialize pointers
    Queue->QHPool = (OhciQueueHead_t*)Pool;
    Queue->QHPoolPhysical = LODWORD(PoolPhysical);
    Queue->TDPool = (OhciTransferDescriptor_t*)((uint8_t*)Pool + (OHCI_POOL_QHS * sizeof(OhciQueueHead_t)));
    Queue->TDPoolPhysical = LODWORD((PoolPhysical + (OHCI_POOL_QHS * sizeof(OhciQueueHead_t))));
    assert(PoolPhysical < 0xFFFFFFFF);

    // Initialize transaction counters
    // and the transaction list
    Queue->TransactionList = CollectionCreate(KeyInteger);

    // Initialize the internal data structures
    return OhciQueueResetInternalData(Controller);
}

/* OhciQueueReset
 * Removes and cleans up any existing transfers, then reinitializes. */
OsStatus_t
OhciQueueReset(
    _In_ OhciController_t *Controller)
{
    // Variables
    CollectionItem_t *tNode = NULL;

    // Debug
    TRACE("OhciQueueReset()");

    // Stop Controller
    OhciSetMode(Controller, OHCI_CONTROL_SUSPEND);

    // Iterate all queued transactions and dequeue
    _foreach(tNode, Controller->QueueControl.TransactionList) {
        OhciTransactionFinalize(Controller, (UsbManagerTransfer_t*)tNode->Data, 0);
    }
    CollectionClear(Controller->QueueControl.TransactionList);

    // Reinitialize internal data
    return OhciQueueResetInternalData(Controller);
}

/* OhciQueueDestroy
 * Unschedules any scheduled ed's and frees all resources allocated
 * by the initialize function */
OsStatus_t
OhciQueueDestroy(
    _In_ OhciController_t *Controller)
{
    // Variables
    size_t PoolSize = 0;

    // Debug
    TRACE("OhciQueueDestroy()");

    // Reset first
    OhciQueueReset(Controller);

    // Calculate how many bytes of memory we will need to free
    PoolSize = OHCI_POOL_QHS * sizeof(OhciQueueHead_t);
    PoolSize += OHCI_POOL_TDS * sizeof(OhciTransferDescriptor_t);

    // Cleanup resources
    CollectionDestroy(Controller->QueueControl.TransactionList);
    MemoryFree(Controller->QueueControl.QHPool, PoolSize);
    return OsSuccess;
}

/* OhciVisualizeQueue
 * Visualizes (by text..) the current interrupt table queue 
 * by going through all 32 base nodes and their links */
void
OhciVisualizeQueue(
    _In_ OhciController_t *Controller)
{
    // Variables
    int i;

    // Enumerate the 32 entries
    for (i = 0; i < OHCI_FRAMELIST_SIZE; i++) {
        OhciQueueHead_t *Qh = Controller->QueueControl.RootTable[i];
        if (Qh != NULL) {
            TRACE("0x%x -> ", (Qh->Flags & OHCI_QH_SKIP));
            // Enumerate links
            while (Qh->LinkIndex != OHCI_NO_INDEX) {
                Qh = &Controller->QueueControl.QHPool[Qh->LinkIndex];
                TRACE("0x%x -> ", (Qh->Flags & OHCI_QH_SKIP));
            }
        }
    }
}

/* OhciQhAllocate
 * Allocates a new ED for usage with the transaction. If this returns
 * NULL we are out of ED's and we should wait till next transfer. */
OhciQueueHead_t*
OhciQhAllocate(
    _In_ OhciController_t *Controller)
{
    // Variables
    OhciQueueHead_t *Qh = NULL;
    int i;

    // Lock access to the queue
    SpinlockAcquire(&Controller->Base.Lock);

    // Now, we usually allocated new endpoints for interrupts
    // and isoc, but it doesn't make sense for us as we keep one
    // large pool of ED's, just allocate from that in any case
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
    
    // Release the lock, let others pass
    SpinlockRelease(&Controller->Base.Lock);
    return Qh;
}

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

    // Lock access to the queue
    SpinlockAcquire(&Controller->Base.Lock);

    // Now, we usually allocated new descriptors for interrupts
    // and isoc, but it doesn't make sense for us as we keep one
    // large pool of Td's, just allocate from that in any case
    for (i = 0; i < OHCI_POOL_TDNULL; i++) {
        // Skip ahead if allocated, skip twice if isoc
        if (Controller->QueueControl.TDPool[i].Flags & OHCI_TD_ALLOCATED) {
            continue;
        }

        // Found one, reset
        memset(&Controller->QueueControl.TDPool[i], 0, sizeof(OhciTransferDescriptor_t));
        Controller->QueueControl.TDPool[i].Flags = OHCI_TD_ALLOCATED;
        Controller->QueueControl.TDPool[i].Index = (int16_t)i;
        Controller->QueueControl.TDPool[i].LinkIndex = OHCI_NO_INDEX;
        Td = &Controller->QueueControl.TDPool[i];
        break;
    }

    // Release the lock, let others pass
    SpinlockRelease(&Controller->Base.Lock);
    return Td;
}

/* OhciQhInitialize
 * Initializes and sets up the endpoint descriptor with 
 * the given values */
void
OhciQhInitialize(
    _In_ OhciController_t *Controller,
    _Out_ OhciQueueHead_t *Qh, 
    _In_ int HeadIndex, 
    _In_ UsbTransferType_t Type,
    _In_ size_t Address, 
    _In_ size_t Endpoint, 
    _In_ size_t PacketSize,
    _In_ UsbSpeed_t Speed)
{
    // Variables
    OhciTransferDescriptor_t *Td    = NULL;
    int16_t LastIndex               = HeadIndex;

    // Update index's
    if (HeadIndex == OHCI_NO_INDEX) {
        Qh->Current = OHCI_LINK_HALTED;
        Qh->EndPointer = 0;
    }
    else {
        Td = &Controller->QueueControl.TDPool[HeadIndex];

        // Set physical of head and tail, set HALTED bit to not start yet
        Qh->Current = OHCI_POOL_TDINDEX(Controller, HeadIndex) | OHCI_LINK_HALTED;
        while (Td->LinkIndex != OHCI_NO_INDEX) {
            LastIndex = Td->LinkIndex;
            Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
        }
        Qh->EndPointer = OHCI_POOL_TDINDEX(Controller, LastIndex);
    }

    // Update head-index
    Qh->ChildIndex = (int16_t)HeadIndex;

    // Initialize flags
    Qh->Flags = OHCI_QH_SKIP;
    Qh->Flags |= OHCI_QH_ADDRESS(Address);
    Qh->Flags |= OHCI_QH_ENDPOINT(Endpoint);
    Qh->Flags |= OHCI_QH_DIRECTIONTD; // Retrieve from TD
    Qh->Flags |= OHCI_QH_LENGTH(PacketSize);
    Qh->Flags |= OHCI_QH_TYPE(Type);

    // Set conditional flags
    if (Speed == LowSpeed) {
        Qh->Flags |= OHCI_QH_LOWSPEED;
    }
    if (Type == IsochronousTransfer) {
        Qh->Flags |= OHCI_QH_ISOCHRONOUS;
    }
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
        return NULL;
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

    // Done
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
    
    // Allocate a new Td
    Td = OhciTdAllocate(Controller);
    if (Td == NULL) {
        return NULL;
    }

    // Debug
    TRACE("OhciTdIo(Type %u, Id %u, Toggle %i, Address 0x%x, Length 0x%x",
        Type, PId, Toggle, Address, Length);

    // Handle Isochronous Transfers a little bit differently
    // Max packet size is 1023 for isoc
    if (Type == IsochronousTransfer) {
        uintptr_t BufItr = 0;
        int FrameCount  = DIVUP(Length, 1023);
        int FrameItr    = 0;
        int Crossed     = 0;

        // If direction is out and mod 1023 is 0
        // add a zero-length frame
        // If framecount is > 8, nono
        if (FrameCount > 8) {
            FrameCount = 8;
        }
        
        // Initialize flags
        Td->Flags       |= OHCI_TD_FRAMECOUNT(FrameCount - 1);
        Td->Flags       |= OHCI_TD_IOC_NONE;

        // Initialize buffer access
        Td->Cbp         = LODWORD(Address);
        Td->BufferEnd   = Td->Cbp + Length - 1;

        // Iterate frames and setup
        while (FrameCount) {
            // Set offset 0 and increase bufitr
            Td->Offsets[FrameItr] = (BufItr & 0xFFF);
            Td->Offsets[FrameItr] = ((Crossed & 0x1) << 12);
            BufItr += 1023;

            // Sanity on page-crossover
            if (BufItr >= 0x1000) {
                BufItr -= 0x1000;
                Crossed = 1;
            }

            // Update iterators
            FrameItr++;
            FrameCount--;
        }

        // Set this is as end of chain
        Td->Link = 0;
        Td->LinkIndex = OHCI_NO_INDEX;

        // Store copy of original content
        Td->OriginalFlags   = Td->Flags;
        Td->OriginalCbp     = Td->Cbp;
        return Td;
    }

    // Set this is as end of chain
    Td->Link        = 0;
    Td->LinkIndex   = OHCI_NO_INDEX;

    // Initialize flags as a IO Td
    Td->Flags |= PId;
    Td->Flags |= OHCI_TD_IOC_NONE;
    Td->Flags |= OHCI_TD_TOGGLE_LOCAL;
    Td->Flags |= OHCI_TD_ACTIVE;

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
    Td->OriginalFlags = Td->Flags;
    Td->OriginalCbp = Td->Cbp;

    // Setup done
    return Td;
}

/* OhciGetStatusCode
 * Retrieves a status-code from a given condition code */
UsbTransferStatus_t
OhciGetStatusCode(
    _In_ int ConditionCode)
{
    // One huuuge if/else
    if (ConditionCode == 0) {
        return TransferFinished;
    }
    else if (ConditionCode == 4) {
        return TransferStalled;
    }
    else if (ConditionCode == 3) {
        return TransferInvalidToggles;
    }
    else if (ConditionCode == 2 || ConditionCode == 1) {
        return TransferBabble;
    }
    else if (ConditionCode == 5) {
        return TransferNotResponding;
    }
    else if (ConditionCode == 15) {
        return TransferNotProcessed;
    }
    else {
        TRACE("Error: 0x%x (%s)", ConditionCode, OhciErrorMessages[ConditionCode]);
        return TransferInvalid;
    }
}

/* OhciCalculateQueue
 * Calculates the queue the ed should get linked to by analyzing the
 * current bandwidth load, and the requested load. Returns -1 on error */
int
OhciCalculateQueue(
    _In_ OhciController_t *Controller, 
    _In_ size_t Interval, 
    _In_ size_t Bandwidth)
{
    // Variables
    OhciControl_t *Queue    = &Controller->QueueControl;
    int Index               = -1;
    size_t i;

    // iso periods can be huge; iso tds specify frame numbers
    if (Interval == 0) {
        Interval = 1;
    }
    if (Interval > OHCI_FRAMELIST_SIZE) {
        Interval = OHCI_FRAMELIST_SIZE;
    }

    // Find the least loaded queue
    for (i = 0; i < Interval; i++) {
        if (Index < 0 || Queue->Bandwidth[Index] > Queue->Bandwidth[i]) {
            int    j;

            // Usb 1.1 says 90% of one frame must be isoc or intr
            for (j = i; j < OHCI_FRAMELIST_SIZE; j += Interval) {
                if ((Queue->Bandwidth[j] + Bandwidth) > 900)
                    break;
            }

            // Sanity bounds of j
            if (j < OHCI_FRAMELIST_SIZE) {
                continue;
            }

            // Update queue index
            Index = i;
        }
    }

    // Return found index
    return Index;
}

/* OhciLinkGeneric
 * Queue's up a generic transfer in the form of Control or Bulk.
 * Both interrupt and isoc-transfers are not handled by this. */
OsStatus_t
OhciLinkGeneric(
    _In_ OhciController_t *Controller,
    _In_ UsbTransferType_t Type,
    _In_ int QueueHeadIndex)
{
    // Variables
    OhciControl_t *Queue    = &Controller->QueueControl;
    uintptr_t QhAddress     = 0;

    // Lookup physical
    QhAddress = OHCI_POOL_QHINDEX(Controller, QueueHeadIndex);

    // Switch based on type of transfer
    if (Type == ControlTransfer) {
        if (Queue->TransactionsWaitingControl > 0) {
            // Insert into front if 0
            if (Queue->TransactionQueueControlIndex == OHCI_NO_INDEX) {
                Queue->TransactionQueueControlIndex = QueueHeadIndex;
            }
            else {
                // Iterate to end of descriptor-chain
                OhciQueueHead_t *ExistingQueueHead = 
                    &Queue->QHPool[Queue->TransactionQueueControlIndex];

                // Iterate until end of chain
                while (ExistingQueueHead->LinkIndex != OHCI_NO_INDEX) {
                    ExistingQueueHead = &Queue->QHPool[ExistingQueueHead->LinkIndex];
                }

                // Insert it
                ExistingQueueHead->LinkPointer  = LODWORD(QhAddress);
                ExistingQueueHead->LinkIndex    = QueueHeadIndex;
            }

            // Increase number of transactions waiting
            Queue->TransactionsWaitingControl++;
        }
        else {
            // Add it HcControl/BulkCurrentED
            Controller->Registers->HcControlHeadED =
                Controller->Registers->HcControlCurrentED = LODWORD(QhAddress);
            Queue->TransactionsWaitingControl++;

            // Enable control list
            Controller->Registers->HcCommandStatus |= OHCI_COMMAND_CONTROL_ACTIVE;
        }

        // Done
        return OsSuccess;
    }
    else if (Type == BulkTransfer) {
        if (Queue->TransactionsWaitingBulk > 0) {
            // Insert into front if 0
            if (Queue->TransactionQueueBulkIndex == OHCI_NO_INDEX) {
                Queue->TransactionQueueBulkIndex = QueueHeadIndex;
            }
            else {
                // Iterate to end of descriptor-chain
                OhciQueueHead_t *ExistingQueueHead = 
                    &Queue->QHPool[Queue->TransactionQueueBulkIndex];

            // Iterate until end of chain
            while (ExistingQueueHead->LinkIndex != OHCI_NO_INDEX) {
                ExistingQueueHead = &Queue->QHPool[ExistingQueueHead->LinkIndex];
            }

            // Insert it
            ExistingQueueHead->LinkPointer = QhAddress;
            ExistingQueueHead->LinkIndex = QueueHeadIndex;
            }

            // Increase waiting count
            Queue->TransactionsWaitingBulk++;
        }
        else {
            // Add it HcControl/BulkCurrentED
            Controller->Registers->HcBulkHeadED =
                Controller->Registers->HcBulkCurrentED = QhAddress;
            Queue->TransactionsWaitingBulk++;
            
            // Enable bulk list
            Controller->Registers->HcCommandStatus |= OHCI_COMMAND_BULK_ACTIVE;
        }

        // Done
        return OsSuccess;
    }

    // Wrong kind of transaction
    return OsError;
}

/* OhciLinkPeriodic
 * Queue's up a periodic/isochronous transfer. If it was not possible
 * to schedule the transfer with the requested bandwidth, it returns
 * OsError */
OsStatus_t
OhciLinkPeriodic(
    _In_ OhciController_t *Controller,
    _In_ int QueueHeadIndex)
{
    // Variables
    OhciQueueHead_t *Qh     = &Controller->QueueControl.QHPool[QueueHeadIndex];
    uintptr_t QhAddress     = 0;
    int Queue               = 0;
    int i;

    // Lookup physical and calculate the queue
    QhAddress   = OHCI_POOL_QHINDEX(Controller, QueueHeadIndex);
    Queue       = OhciCalculateQueue(Controller, Qh->Interval, Qh->Bandwidth);

    // Sanitize that it's valid queue
    if (Queue < 0) {
        return OsError;
    }

    // Now loop through the bandwidth-phases and link it
    for (i = Queue; i < OHCI_FRAMELIST_SIZE; i += (int)Qh->Interval) {
        // Two cases, first or not first
        if (Controller->QueueControl.RootTable[i] == NULL) {
            Controller->QueueControl.RootTable[i]   = Qh;
            Controller->Hcca->InterruptTable[i]     = LODWORD(QhAddress);
        }
        else {
            // Ok, so we need to index us by Interval
            // Lowest interval must come last
            OhciQueueHead_t *ExistingQh = Controller->QueueControl.RootTable[i];

            // Two cases, sort us or insert us
            if (Qh->Interval > ExistingQh->Interval) {
                // Insertion Case
                Qh->LinkPointer = Controller->Hcca->InterruptTable[i];
                Qh->LinkIndex = ExistingQh->Index;
                MemoryBarrier();
                Controller->QueueControl.RootTable[i]   = Qh;
                Controller->Hcca->InterruptTable[i]     = LODWORD(QhAddress);
            }
            else {
                // Sort case
                while (ExistingQh->LinkIndex != OHCI_NO_INDEX 
                        && ExistingQh->LinkIndex != Qh->Index) {
                    if (Qh->Interval > ExistingQh->Interval) {
                        break;
                    }
                    ExistingQh = &Controller->QueueControl.QHPool[ExistingQh->LinkIndex];
                }

                // Only insert if we aren't already present in queue
                if (ExistingQh->LinkIndex != Qh->Index) {
                    Qh->LinkPointer         = ExistingQh->LinkPointer;
                    Qh->LinkIndex           = ExistingQh->LinkIndex;
                    MemoryBarrier();
                    ExistingQh->LinkIndex   = Qh->Index;
                    ExistingQh->LinkPointer = LODWORD(QhAddress);
                }
            }
        }

        // Increase the bandwidth
        Controller->QueueControl.Bandwidth[i] += Qh->Bandwidth;
    }

    // Store the resulting queue 
    Qh->Queue = (uint8_t)(Queue & 0xFF);
    return OsSuccess;
}

/* OhciUnlinkPeriodic
 * Removes an already queued up periodic transfer (interrupt/isoc) from the
 * controllers scheduler. Also unallocates the bandwidth */
void
OhciUnlinkPeriodic(
    _In_ OhciController_t *Controller, 
    _In_ int QueueHeadIndex)
{
    // Variables
    OhciQueueHead_t *Qh     = &Controller->QueueControl.QHPool[QueueHeadIndex];
    int Queue               = (int)Qh->Queue;
    int i;

    // Iterate the bandwidth phases
    for (i = Queue; i < OHCI_FRAMELIST_SIZE; i += (int)Qh->Interval) {
        OhciQueueHead_t *ExistingQh = Controller->QueueControl.RootTable[i];

        // Two cases, root or not
        if (ExistingQh == Qh) {
            if (Qh->LinkIndex != OHCI_NO_INDEX) {
                Controller->QueueControl.RootTable[i] = 
                    &Controller->QueueControl.QHPool[Qh->LinkIndex];
                Controller->Hcca->InterruptTable[i] = 
                    OHCI_POOL_QHINDEX(Controller, Qh->LinkIndex);
            }
            else {
                Controller->QueueControl.RootTable[i] = NULL;
                Controller->Hcca->InterruptTable[i] = 0;
            }
        }
        else {
            // Not root, iterate down chain
            while (ExistingQh->LinkIndex != Qh->Index
                    && ExistingQh->LinkIndex != OHCI_NO_INDEX) {
                ExistingQh = &Controller->QueueControl.QHPool[ExistingQh->LinkIndex];
            }

            // Unlink if found
            if (ExistingQh->LinkIndex == Qh->Index) {
                ExistingQh->LinkIndex = Qh->LinkIndex;
                ExistingQh->LinkPointer = Qh->LinkPointer;
            }
        }

        // Decrease the bandwidth
        Controller->QueueControl.Bandwidth[i] -= Qh->Bandwidth;
    }
}

/* OhciReloadControlBulk
 * Reloads the control and bulk lists with new transactions that
 * are waiting in queue for execution. */
void
OhciReloadControlBulk(
    _In_ OhciController_t *Controller, 
    _In_ UsbTransferType_t TransferType)
{
    // So now, before waking up a sleeper we see if Transactions are pending
    // if they are, we simply copy the queue over to the current

    // Now the step is to check whether or not there is any
    // transfers awaiting for the requested type
    if (TransferType == ControlTransfer) {
        if (Controller->QueueControl.TransactionsWaitingControl > 0) {
            // Retrieve the physical address
            uintptr_t QhPhysical = OHCI_POOL_QHINDEX(Controller, 
                Controller->QueueControl.TransactionQueueControlIndex);
            
            // Update new start and kick off queue
            Controller->Registers->HcControlHeadED =
                Controller->Registers->HcControlCurrentED = LODWORD(QhPhysical);
            Controller->Registers->HcCommandStatus |= OHCI_COMMAND_CONTROL_ACTIVE;
        }

        // Reset control queue
        Controller->QueueControl.TransactionQueueControlIndex = OHCI_NO_INDEX;
        Controller->QueueControl.TransactionsWaitingControl = 0;
    }
    else if (TransferType == BulkTransfer) {
        if (Controller->QueueControl.TransactionsWaitingBulk > 0) {
            // Retrieve the physical address
            uintptr_t QhPhysical = OHCI_POOL_QHINDEX(Controller, 
                Controller->QueueControl.TransactionQueueBulkIndex);

            // Update new start and kick off queue
            Controller->Registers->HcBulkHeadED =
                Controller->Registers->HcBulkCurrentED = LODWORD(QhPhysical);
            Controller->Registers->HcCommandStatus |= OHCI_COMMAND_BULK_ACTIVE;
        }

        // Reset bulk queue
        Controller->QueueControl.TransactionQueueBulkIndex = OHCI_NO_INDEX;
        Controller->QueueControl.TransactionsWaitingBulk = 0;
    }
}

/* OhciReloadPeriodic
 * Reloads a periodic transfer that already exists in queue. 
 * Reloads transfer-descriptors, queue head and synchronizes toggles. */
void
OhciReloadPeriodic(
    _In_ OhciController_t *Controller,
    _In_ UsbManagerTransfer_t *Transfer)
{
    // Variables
    OhciTransferDescriptor_t *Td    = NULL;
    OhciQueueHead_t *Qh             = NULL;
    uintptr_t BufferBase            = 0;
    uintptr_t BufferStep            = 0;
    int SwitchToggles               = 0;
    int ErrorTransfer               = 0;

    // Initiate values
    Qh = (OhciQueueHead_t*)Transfer->EndpointDescriptor;

    // Do some extra processing for periodics
    if (Transfer->Transfer.Type == InterruptTransfer) {
        SwitchToggles = Transfer->TransactionsTotal % 2;
        BufferBase = Transfer->Transfer.Transactions[0].BufferAddress;
        BufferStep = Transfer->Transfer.Endpoint.MaxPacketSize;
    }

    // Re-iterate all td's
    // Almost everything must be restored due to the HC relinking
    // things to the done-queue.
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    while (Td->LinkIndex != OHCI_NO_INDEX) {
        // Extract error code
        int ErrorCount = OHCI_TD_ERRORCOUNT(Td->Flags);
        int ErrorCode = OHCI_TD_ERRORCODE(Td->Flags);
        
        // Sanitize the error code
        if ((ErrorCount == 2 && ErrorCode != 0) && !ErrorTransfer) {
            Transfer->Status = OhciGetStatusCode(ErrorCode);
            ErrorTransfer = 1;
        }
        
        // Interrupt transfers need some additional processing
        // Update toggle if neccessary (in original data)
        if (Transfer->Transfer.Type == InterruptTransfer) {
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

        // Restart Td
        Td->Flags       = Td->OriginalFlags;
        Td->Cbp         = Td->OriginalCbp;
        Td->BufferEnd   = Td->Cbp + (BufferStep - 1);
        Td->Link        = OHCI_POOL_TDINDEX(Controller, Td->LinkIndex);

        // Go to next td
        Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
    }

    // Notify process of transfer of the status
    if (Transfer->Transfer.Type == InterruptTransfer) {
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

    // Restart endpoint
    if (!ErrorTransfer) {
        Qh->Current = OHCI_POOL_TDINDEX(Controller, Qh->ChildIndex); 
    }
}

/* OhciProcessDoneQueue
 * Iterates all active transfers and handles completion/error events */
void
OhciProcessDoneQueue(
    _In_ OhciController_t*  Controller, 
    _In_ uintptr_t          DoneHeadAddress)
{
    // Variables
    OhciTransferDescriptor_t *Td = NULL;

    // Go through active transactions and locate the EP that was done
    foreach(Node, Controller->QueueControl.TransactionList) {
        // Instantiate the usb-transfer pointer, and then the EP
        UsbManagerTransfer_t *Transfer = (UsbManagerTransfer_t*)Node->Data;
        OhciQueueHead_t *QueueHead = (OhciQueueHead_t*)Transfer->EndpointDescriptor;

        // Iterate through all td's in this transaction
        // and find the guilty
        Td = &Controller->QueueControl.TDPool[QueueHead->ChildIndex];
        while (Td) {
            // Retrieve the physical address
            uintptr_t TdPhysical = OHCI_POOL_TDINDEX(Controller, Td->Index);

            // Does the addresses match?
            if (DoneHeadAddress == TdPhysical) {
                // Which kind of transfer is this?
                if (Transfer->Transfer.Type == ControlTransfer
                    || Transfer->Transfer.Type == BulkTransfer) {
                    // Reload and finalize transfer
                    OhciReloadControlBulk(Controller, Transfer->Transfer.Type);
                    
                    // Handle completion of transfer
                    // Finalize transaction and remove from list
                    if (OhciTransactionFinalize(Controller, Transfer, 1) == OsSuccess) {
                        CollectionRemoveByNode(Controller->QueueControl.TransactionList, Node);
                        CollectionDestroyNode(Controller->QueueControl.TransactionList, Node);
                    }
                }
                else {
                    OhciReloadPeriodic(Controller, Transfer);
                }
                return;
            }

            // Go to next td or terminate
            if (Td->LinkIndex != OHCI_NO_INDEX) {
                Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
            }
            else {
                break;
            }
        }
    }
}

/* OhciIsTransferComplete
 * Checks a transfer if it has either completed or failed. If it hasn't
 * been fully processed yet we return OsError */
OsStatus_t
OhciIsTransferComplete(
    _In_ OhciController_t*  Controller,
    _In_ OhciQueueHead_t*   QueueHead)
{
    // Variables
    OhciTransferDescriptor_t *Td = NULL;

    // Iterate through all td's except for null in this transaction and find the guilty
    Td = &Controller->QueueControl.TDPool[QueueHead->ChildIndex];
    while (Td->LinkIndex != OHCI_NO_INDEX) {
        int ErrorCode               = OHCI_TD_ERRORCODE(Td->Flags);
        UsbTransferStatus_t Status  = OhciGetStatusCode(ErrorCode);

        // Break if we encounter unprocessed
        if (Status == TransferNotProcessed) {
            return OsError;
        }

        // Go to next td or terminate
        Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
    }
    return OsSuccess;
}

/* OhciCheckDoneQueue
 * Iterates all active transfers and handles completion/error events */
void
OhciCheckDoneQueue(
    _In_ OhciController_t *Controller)
{
    // Go through active transactions and locate the EP that was done
    foreach(Node, Controller->QueueControl.TransactionList) {
        // Instantiate the usb-transfer pointer, and then the EP
        UsbManagerTransfer_t *Transfer  = (UsbManagerTransfer_t*)Node->Data;
        OhciQueueHead_t *QueueHead      = (OhciQueueHead_t*)Transfer->EndpointDescriptor;

        // Only check control/bulk for completion
        if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
            if (QueueHead != NULL && OhciIsTransferComplete(Controller, QueueHead) == OsSuccess) {
                // Reload and finalize transfer
                OhciReloadControlBulk(Controller, Transfer->Transfer.Type);
                
                // Handle completion of transfer
                // Finalize transaction and remove from list
                if (OhciTransactionFinalize(Controller, Transfer, 1) == OsSuccess) {
                    CollectionRemoveByNode(Controller->QueueControl.TransactionList, Node);
                    CollectionDestroyNode(Controller->QueueControl.TransactionList, Node);
                }
            }
        }
    }
}

/* Process Transactions 
 * This code unlinks / links pending endpoint descriptors. 
 * Should be called from interrupt-context */
void
OhciProcessTransactions(
    _In_ OhciController_t *Controller,
    _In_ int               Finalize)
{
    // Iterate all active transactions and see if any
    // one of them needs unlinking or linking
    foreach(Node, Controller->QueueControl.TransactionList) {
        
        // Instantiate the usb-transfer pointer, and then the EP
        UsbManagerTransfer_t *Transfer = 
            (UsbManagerTransfer_t*)Node->Data;
        OhciQueueHead_t *QueueHead = 
            (OhciQueueHead_t*)Transfer->EndpointDescriptor;

        // Check flags for any requests, either schedule or unschedule
        if (QueueHead->HcdInformation & OHCI_QH_SCHEDULE) {
            if (Transfer->Transfer.Type == ControlTransfer
                || Transfer->Transfer.Type == BulkTransfer) {
                OhciLinkGeneric(Controller, Transfer->Transfer.Type, QueueHead->Index);
            }
            else {
                // Link it in, and activate list
                OhciLinkPeriodic(Controller, QueueHead->Index);
                Controller->Registers->HcControl |= OHCI_CONTROL_PERIODIC_ACTIVE 
                    | OHCI_CONTROL_ISOC_ACTIVE;
            }

            // Remove the schedule flag
            QueueHead->HcdInformation &= ~OHCI_QH_SCHEDULE;
        }
        else if (QueueHead->HcdInformation & OHCI_QH_UNSCHEDULE) {
            // Handle completion of transfer
            if (Finalize == 1) {
                QueueHead->HcdInformation &= ~OHCI_QH_UNSCHEDULE;
                if (OhciTransactionFinalize(Controller, Transfer, 0) == OsSuccess) {
                    CollectionRemoveByNode(Controller->QueueControl.TransactionList, Node);
                    CollectionDestroyNode(Controller->QueueControl.TransactionList, Node);
                }
            }
            else {
                // Only interrupt and isoc requests unscheduling
                // And remove the unschedule flag
                OhciUnlinkPeriodic(Controller, QueueHead->Index);
            }
        }
    }
}
