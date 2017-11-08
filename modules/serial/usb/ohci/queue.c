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
#define __TRACE

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>
#include "ohci.h"

/* Includes
 * - Library */
#include <ds/list.h>
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

    // Initialize the null-td
    NullTd = &Queue->TDPool[OHCI_POOL_TDNULL];
    NullTd->BufferEnd = 0;
    NullTd->Cbp = 0;
    NullTd->Link = 0x0;
    NullTd->Flags = OHCI_TD_ALLOCATED;
    NullPhysical = OHCI_POOL_TDINDEX(Controller, OHCI_POOL_TDNULL);

    // Enumerate the ED pool and initialize them
    for (i = 0; i < (OHCI_POOL_QHS + 32); i++) {
        // Mark it skippable and set a NULL td
        Queue->QHPool[i].HcdInformation = 0;
        Queue->QHPool[i].Flags = OHCI_QH_SKIP;
        Queue->QHPool[i].EndPointer = NullPhysical;
        Queue->QHPool[i].Current = NullPhysical | OHCI_LINK_HALTED;
        Queue->QHPool[i].ChildIndex = OHCI_NO_INDEX;
        Queue->QHPool[i].LinkIndex = OHCI_NO_INDEX;
        Queue->QHPool[i].Index = (int16_t)i;
    }

    // Enumerate the TD pool and initialize them
    for (i = 0; i < OHCI_POOL_TDNULL; i++) {
        Queue->TDPool[i].Flags = 0;
        Queue->TDPool[i].Link = 0;
        Queue->TDPool[i].Index = (int16_t)i;
        Queue->TDPool[i].LinkIndex = OHCI_NO_INDEX;
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
    PoolSize = (OHCI_POOL_QHS + 32) * sizeof(OhciQueueHead_t);
    PoolSize += OHCI_POOL_TDS * sizeof(OhciTransferDescriptor_t);

    // Allocate both TD's and ED's pool
    if (MemoryAllocate(PoolSize, MEMORY_CLEAN | MEMORY_COMMIT
        | MEMORY_LOWFIRST | MEMORY_CONTIGIOUS, &Pool, &PoolPhysical) != OsSuccess) {
        ERROR("Failed to allocate memory for resource-pool");
        return OsError;
    }

    // Initialize pointers
    Queue->QHPool = (OhciQueueHead_t*)Pool;
    Queue->QHPoolPhysical = PoolPhysical;
    Queue->TDPool = (OhciTransferDescriptor_t*)((uint8_t*)Pool +
        ((OHCI_POOL_QHS + 32) * sizeof(OhciQueueHead_t)));
    Queue->TDPoolPhysical = PoolPhysical + 
        ((OHCI_POOL_QHS + 32) * sizeof(OhciQueueHead_t));

    // Initialize transaction counters
    // and the transaction list
    Queue->TransactionList = ListCreate(KeyInteger, LIST_SAFE);

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
    ListNode_t *tNode = NULL;

    // Debug
    TRACE("OhciQueueReset()");

    // Stop Controller
    OhciSetMode(Controller, OHCI_CONTROL_SUSPEND);

    // Iterate all queued transactions and dequeue
    _foreach(tNode, Controller->QueueControl.TransactionList) {
        OhciTransactionFinalize(Controller, 
            (UsbManagerTransfer_t*)tNode->Data, 0);
    }
    ListClear(Controller->QueueControl.TransactionList);

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
    PoolSize = (OHCI_POOL_QHS + 32) * sizeof(OhciQueueHead_t);
    PoolSize += OHCI_POOL_TDS * sizeof(OhciTransferDescriptor_t);

    // Cleanup resources
    ListDestroy(Controller->QueueControl.TransactionList);
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
    for (i = 0; i < 32; i++) {
        OhciQueueHead_t *Qh = &Controller->QueueControl.QHPool[OHCI_POOL_QHS + i];
        TRACE("0x%x -> ", (Qh->Flags & OHCI_QH_SKIP));

        // Enumerate links
        while (Qh->LinkIndex != OHCI_NO_INDEX) {
            Qh = &Controller->QueueControl.QHPool[Qh->LinkIndex];
            TRACE("0x%x -> ", (Qh->Flags & OHCI_QH_SKIP));
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
    _In_ OhciController_t *Controller,
    _In_ UsbTransferType_t Type,
    _In_ uint32_t PId,
    _In_ int Toggle,
    _In_ uintptr_t Address,
    _In_ size_t Length)
{
    // Variables
    OhciTransferDescriptor_t *Td = NULL;
    
    // Allocate a new Td
    Td = OhciTdAllocate(Controller);
    if (Td == NULL) {
        return NULL;
    }

    // Handle Isochronous Transfers a little bit differently
    // Max packet size is 1023 for isoc
    if (Type == IsochronousTransfer) {
        uintptr_t BufItr = 0;
        int FrameCount = DIVUP(Length, 1023);
        int FrameItr = 0;
        int Crossed = 0;

        // If direction is out and mod 1023 is 0
        // add a zero-length frame
        // If framecount is > 8, nono
        if (FrameCount > 8) {
            FrameCount = 8;
        }
        
        // Initialize flags
        Td->Flags |= OHCI_TD_FRAMECOUNT(FrameCount - 1);
        Td->Flags |= OHCI_TD_IOC_NONE;

        // Initialize buffer access
        Td->Cbp = Address;
        Td->BufferEnd = Td->Cbp + Length - 1;

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
        Td->OriginalFlags = Td->Flags;
        Td->OriginalCbp = Td->Cbp;

        // Setup done
        return Td;
    }

    // Set this is as end of chain
    Td->Link = 0;
    Td->LinkIndex = OHCI_NO_INDEX;

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
        Td->Cbp = Address;
        Td->BufferEnd = Td->Cbp + Length - 1;
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
    if (Interval > OHCI_BANDWIDTH_PHASES) {
        Interval = OHCI_BANDWIDTH_PHASES;
    }

    // Find the least loaded queue
    for (i = 0; i < Interval; i++) {
        if (Index < 0 || Queue->Bandwidth[Index] > Queue->Bandwidth[i]) {
            int    j;

            // Usb 1.1 says 90% of one frame must be isoc or intr
            for (j = i; j < OHCI_BANDWIDTH_PHASES; j += Interval) {
                if ((Queue->Bandwidth[j] + Bandwidth) > 900)
                    break;
            }

            // Sanity bounds of j
            if (j < OHCI_BANDWIDTH_PHASES) {
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
    OhciQueueHead_t *Qh     = &Queue->QHPool[QueueHeadIndex];
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
                ExistingQueueHead->LinkPointer = QhAddress;
                ExistingQueueHead->LinkIndex = QueueHeadIndex;
            }

            // Increase number of transactions waiting
            Queue->TransactionsWaitingControl++;
        }
        else {
            // Add it HcControl/BulkCurrentED
            Controller->Registers->HcControlHeadED =
                Controller->Registers->HcControlCurrentED = QhAddress;
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
    QhAddress = OHCI_POOL_QHINDEX(Controller, QueueHeadIndex);
    Queue = OhciCalculateQueue(Controller, Qh->Interval, Qh->Bandwidth);

    // Sanitize that it's valid queue
    if (Queue < 0) {
        return OsError;
    }

    // Now loop through the bandwidth-phases and link it
    for (i = Queue; i < OHCI_BANDWIDTH_PHASES; i += (int)Qh->Interval) {
        OhciQueueHead_t *QhTableIterator = 
            &Controller->QueueControl.QHPool[OHCI_POOL_QHS + i];

        // Find spot, sort by period
        while (QhTableIterator->LinkIndex != OHCI_NO_INDEX) {
            if (Qh->Interval > QhTableIterator->Interval) {
                break;
            }

            // Move on
            QhTableIterator = &Controller->QueueControl.
                QHPool[QhTableIterator->LinkIndex];
        }

        // Link us in


        OhciQueueHead_t *EdPtr = &Controller->QueueControl.QHPool[OHCI_POOL_QHS + i];
        OhciQueueHead_t **PrevEd = &EdPtr;
        OhciQueueHead_t *Here = *PrevEd;
        uint32_t *PrevPtr = (uint32_t*)&Controller->Hcca->InterruptTable[i];

        // Sorting each branch by period (slow before fast)
        // lets us share the faster parts of the tree.
        // (plus maybe: put interrupt eds _after_ iso)
        while (Here && Ep != Here) {
            if (Ep->Interval > Here->Interval) {
                break;
            }

            // Instantiate an ed pointer
            OhciEndpointDescriptor_t *CurrentEp = 
                (OhciEndpointDescriptor_t*)Here->LinkVirtual;
            
            // Get next
            PrevEd = &CurrentEp;
            PrevPtr = &Here->Link;
            Here = *PrevEd;
        }

        // Sanitize the found
        if (Ep != Here) {
            Ep->LinkVirtual = (uint32_t)Here;
            if (Here) {
                Ep->Link = *PrevPtr;
            }

            // Update the link with barriers
            MemoryBarrier();
            *PrevEd = Ep;
            *PrevPtr = EpAddress;
            MemoryBarrier();
        }

        // Increase the bandwidth
        Controller->QueueControl.Bandwidth[i] += Ep->Bandwidth;
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
    _In_ int EndpointDescriptorIndex)
{
    // Variables
    OhciEndpointDescriptor_t *Ep = 
        &Controller->QueueControl.EDPool[EndpointDescriptorIndex];
    int Queue = OHCI_ED_GET_QUEUE(Ep->HcdFlags);
    int i;

    // Iterate the bandwidth phases
    for (i = Queue; i < OHCI_BANDWIDTH_PHASES; i += (int)Ep->Interval) {
        OhciEndpointDescriptor_t *EdPtr = &Controller->QueueControl.EDPool[OHCI_POOL_EDS + i];
        OhciEndpointDescriptor_t *Temp = NULL;
        OhciEndpointDescriptor_t **PrevEd = &EdPtr;
        uint32_t *PrevPtr = (uint32_t*)&Controller->Hcca->InterruptTable[i];

        // Iterate till we find the endpoint descriptor
        while (*PrevEd && (Temp = *PrevEd) != Ep) {
            // Instantiate a ed pointer
            OhciEndpointDescriptor_t *CurrentEp = 
                (OhciEndpointDescriptor_t*)Temp->LinkVirtual;
            PrevPtr = &Temp->Link;
            PrevEd = &CurrentEp;
        }

        // Make sure we actually found it
        if (*PrevEd) {
            *PrevPtr = Ep->Link;
            *PrevEd = (OhciEndpointDescriptor_t*)Ep->LinkVirtual;
        }

        // Decrease the bandwidth
        Controller->QueueControl.Bandwidth[i] -= Ep->Bandwidth;
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
                Controller->Registers->HcControlCurrentED = QhPhysical;
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
                Controller->Registers->HcBulkCurrentED = QhPhysical;
            Controller->Registers->HcCommandStatus |= OHCI_COMMAND_BULK_ACTIVE;
        }

        // Reset bulk queue
        Controller->QueueControl.TransactionQueueBulkIndex = OHCI_NO_INDEX;
        Controller->QueueControl.TransactionsWaitingBulk = 0;
    }
}

/* OhciProcessDoneQueue
 * Iterates all active transfers and handles completion/error events */
void
OhciProcessDoneQueue(
    _In_ OhciController_t *Controller, 
    _In_ uintptr_t DoneHeadAddress)
{
    // Variables
    OhciTransferDescriptor_t *Td = NULL, *Td2 = NULL;
    List_t *Transactions = Controller->QueueControl.TransactionList;

    // Go through active transactions and locate the EP that was done
    foreach(Node, Transactions) {

        // Instantiate the usb-transfer pointer, and then the EP
        UsbManagerTransfer_t *Transfer = 
            (UsbManagerTransfer_t*)Node->Data;
        OhciEndpointDescriptor_t *EndpointDescriptor = 
            (OhciEndpointDescriptor_t*)Transfer->EndpointDescriptor;

        // Skip?
        if (Transfer->Cleanup != 0) {
            continue;
        }

        // Iterate through all td's in this transaction
        // and find the guilty
        Td = &Controller->QueueControl.TDPool[EndpointDescriptor->HeadIndex];
        while (Td) {
            // Retrieve the physical address
            uintptr_t TdPhysical = OHCI_POOL_TDINDEX(
                Controller->QueueControl.TDPoolPhysical, Td->Index);

            // Does the addresses match?
            if (DoneHeadAddress == TdPhysical) {
                // Which kind of transfer is this?
                if (Transfer->Transfer.Type == ControlTransfer
                    || Transfer->Transfer.Type == BulkTransfer) {
                    // Reload and finalize transfer
                    OhciReloadControlBulk(Controller, Transfer->Transfer.Type);
                    // Instead of finalizing here, wakeup a finalizer thread?
                    Transfer->Cleanup = 1;
                }
                else {
                    // Reload td's and synchronize toggles
                    uintptr_t BufferBase, BufferStep;
                    int SwitchToggles = Transfer->TransactionCount % 2;
                    int ErrorTransfer = 0;

                    // Do some extra processing for periodics
                    if (Transfer->Transfer.Type == InterruptTransfer) {
                        BufferBase = Transfer->Transfer.Transactions[0].BufferAddress;
                        BufferStep = Transfer->Transfer.Transactions[0].Length;
                    }

                    // Re-iterate all td's
                    Td2 = &Controller->QueueControl.TDPool[EndpointDescriptor->HeadIndex];
                    while (Td2) {
                        // Extract error code
                        int ErrorCode = OHCI_TD_GET_CC(Td2->Flags);
                        
                        // Sanitize the error code
                        if ((ErrorCode != 0 && ErrorCode != 15)
                            || ErrorTransfer) {
                            ErrorTransfer = 1;
                            Transfer->Status = OhciGetStatusCode(ErrorCode);
                        }
                        else {
                            // Update toggle if neccessary (in original data)
                            if (Transfer->Transfer.Type == InterruptTransfer 
                                && SwitchToggles) {
                                int Toggle = UsbManagerGetToggle(
                                    Transfer->Device, Transfer->Pipe);
                                
                                // First clear toggle, then get if we should set it
                                Td2->OriginalFlags &= ~OHCI_TD_TOGGLE;
                                Td2->OriginalFlags |= (Toggle << 24);

                                // Update again if it's not dummy
                                if (Td2->LinkIndex != -1) {
                                    UsbManagerSetToggle(Transfer->Device, 
                                        Transfer->Pipe, Toggle ^ 1);
                                }
                            }

                            // Adjust buffers if interrupt in
                            if (Transfer->Transfer.Type == InterruptTransfer) {
                                uintptr_t BufferBaseUpdated = ADDLIMIT(BufferBase, Td2->OriginalCbp, 
                                    BufferStep, BufferBase + Transfer->Transfer.PeriodicBufferSize);
                                Td2->OriginalCbp = LODWORD(BufferBaseUpdated);
                            }
                            
                            // Restart Td
                            Td2->Flags = Td2->OriginalFlags;
                            Td2->Cbp = Td2->OriginalCbp;
                        }

                        // Go to next td or terminate
                        if (Td2->LinkIndex != -1) {
                            Td2 = &Controller->QueueControl.TDPool[Td2->LinkIndex];
                        }
                        else {
                            break;
                        }
                    }
                    
                    // Notify process of transfer of the status
                    if (Transfer->Transfer.Type == InterruptTransfer) {
                        if (Transfer->Transfer.UpdatesOn) {
                            InterruptDriver(Transfer->Requester, 
                                (size_t)Transfer->Transfer.PeriodicData, 
                                (size_t)((ErrorTransfer == 0) ? TransferFinished : Transfer->Status), 
                                Transfer->PeriodicDataIndex, 0);
                        }

                        // Increase
                        Transfer->PeriodicDataIndex = ADDLIMIT(0, Transfer->PeriodicDataIndex,
                            Transfer->Transfer.Transactions[0].Length, Transfer->Transfer.PeriodicBufferSize);
                    }
                    
                    // Restart endpoint
                    if (!ErrorTransfer) {
                        EndpointDescriptor->Current = 
                            OHCI_POOL_TDINDEX(Controller->QueueControl.TDPoolPhysical, 
                                EndpointDescriptor->HeadIndex); 
                    }
                }

                // Break out
                break;
            }

            // Go to next td or terminate
            if (Td->LinkIndex != -1) {
                Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
            }
            else {
                break;
            }
        }
    }
}

/* Process Transactions 
 * This code unlinks / links pending endpoint descriptors. 
 * Should be called from interrupt-context */
void
OhciProcessTransactions(
    _In_ OhciController_t *Controller)
{
    // Variables
    List_t *Transactions = Controller->QueueControl.TransactionList;

    // Iterate all active transactions and see if any
    // one of them needs unlinking or linking
    foreach(Node, Transactions) {
        
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
            // Only interrupt and isoc requests unscheduling
            // And remove the unschedule flag @todo
            OhciUnlinkPeriodic(Controller, QueueHead->Index);
            QueueHead->HcdInformation &= ~OHCI_QH_UNSCHEDULE;
            OhciTransactionFinalize(Controller, Transfer, 0);
        }
    }
}
