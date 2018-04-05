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

/* OhciQueueResetInternalData
 * Removes and cleans up any existing transfers, then reinitializes. */
OsStatus_t
OhciQueueResetInternalData(
    _In_ OhciController_t*  Controller)
{
    // Variables
    OhciTransferDescriptor_t *NullTd    = NULL;
    OhciControl_t *Queue                = &Controller->QueueControl;
    uintptr_t NullPhysical              = 0;
    int i;
    
    // Reset indexes
    Queue->TransactionQueueBulkIndex    = OHCI_NO_INDEX;
    Queue->TransactionQueueControlIndex = OHCI_NO_INDEX;
    for (i = 0; i < OHCI_FRAMELIST_SIZE; i++) {
        Queue->RootTable[i]             = NULL;
    }

    // Initialize the null-td
    NullTd                  = &Queue->TDPool[OHCI_POOL_TDNULL];
    NullTd->BufferEnd       = 0;
    NullTd->Cbp             = 0;
    NullTd->Link            = 0;
    NullTd->Flags           = OHCI_TD_ALLOCATED;
    NullPhysical            = OHCI_POOL_TDINDEX(Controller, OHCI_POOL_TDNULL);

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
    return OsSuccess;
}

/* OhciQueueInitialize
 * Initialize the controller's queue resources and resets counters */
OsStatus_t
OhciQueueInitialize(
    _In_ OhciController_t*  Controller)
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
    Queue->QHPool           = (OhciQueueHead_t*)Pool;
    Queue->QHPoolPhysical   = LODWORD(PoolPhysical);
    Queue->TDPool           = (OhciTransferDescriptor_t*)((uint8_t*)Pool + (OHCI_POOL_QHS * sizeof(OhciQueueHead_t)));
    Queue->TDPoolPhysical   = LODWORD((PoolPhysical + (OHCI_POOL_QHS * sizeof(OhciQueueHead_t))));
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
    _In_ OhciController_t*  Controller)
{
    // Variables
    CollectionItem_t *tNode = NULL;

    // Debug
    TRACE("OhciQueueReset()");

    // Stop Controller
    OhciSetMode(Controller, OHCI_CONTROL_SUSPEND);

    // Iterate all queued transactions and dequeue
    _foreach(tNode, Controller->Base.TransactionList) {
        OhciTransactionFinalize(Controller, (UsbManagerTransfer_t*)tNode->Data, 0);
    }
    CollectionClear(Controller->Base.TransactionList);

    // Reinitialize internal data
    return OhciQueueResetInternalData(Controller);
}

/* OhciQueueDestroy
 * Unschedules any scheduled ed's and frees all resources allocated
 * by the initialize function */
OsStatus_t
OhciQueueDestroy(
    _In_ OhciController_t*  Controller)
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
    CollectionDestroy(Controller->Base.TransactionList);
    MemoryFree(Controller->QueueControl.QHPool, PoolSize);
    return OsSuccess;
}

/* OhciVisualizeQueue
 * Visualizes (by text..) the current interrupt table queue 
 * by going through all 32 base nodes and their links */
void
OhciVisualizeQueue(
    _In_ OhciController_t*  Controller)
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

/* OhciGetStatusCode
 * Retrieves a status-code from a given condition code */
UsbTransferStatus_t
OhciGetStatusCode(
    _In_ int                ConditionCode)
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
    _In_ OhciController_t*  Controller, 
    _In_ size_t             Interval, 
    _In_ size_t             Bandwidth)
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
    _In_ OhciController_t*  Controller,
    _In_ UsbTransferType_t  Type,
    _In_ int                QueueHeadIndex)
{
    // Variables
    OhciControl_t *Queue    = &Controller->QueueControl;
    uintptr_t QhAddress     = 0;

    // Lookup physical
    QhAddress               = OHCI_POOL_QHINDEX(Controller, QueueHeadIndex);

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
        return OsSuccess;
    }
    return OsError;
}

/* OhciLinkPeriodic
 * Queue's up a periodic/isochronous transfer. If it was not possible
 * to schedule the transfer with the requested bandwidth, it returns
 * OsError */
OsStatus_t
OhciLinkPeriodic(
    _In_ OhciController_t*  Controller,
    _In_ int                QueueHeadIndex)
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
                while (ExistingQh->LinkIndex != OHCI_NO_INDEX && ExistingQh->LinkIndex != Qh->Index) {
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
    Qh->Queue = (uint16_t)(Queue & 0xFFFF);
    return OsSuccess;
}

/* OhciUnlinkPeriodic
 * Removes an already queued up periodic transfer (interrupt/isoc) from the
 * controllers scheduler. Also unallocates the bandwidth */
void
OhciUnlinkPeriodic(
    _In_ OhciController_t*  Controller, 
    _In_ int                QueueHeadIndex)
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
                Controller->QueueControl.RootTable[i]   = &Controller->QueueControl.QHPool[Qh->LinkIndex];
                Controller->Hcca->InterruptTable[i]     = OHCI_POOL_QHINDEX(Controller, Qh->LinkIndex);
            }
            else {
                Controller->QueueControl.RootTable[i]   = NULL;
                Controller->Hcca->InterruptTable[i]     = 0;
            }
        }
        else {
            // Not root, iterate down chain
            while (ExistingQh->LinkIndex != Qh->Index && ExistingQh->LinkIndex != OHCI_NO_INDEX) {
                ExistingQh = &Controller->QueueControl.QHPool[ExistingQh->LinkIndex];
            }

            // Unlink if found
            if (ExistingQh->LinkIndex == Qh->Index) {
                ExistingQh->LinkIndex   = Qh->LinkIndex;
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
    _In_ OhciController_t*  Controller, 
    _In_ UsbTransferType_t  TransferType)
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
        Controller->QueueControl.TransactionQueueControlIndex   = OHCI_NO_INDEX;
        Controller->QueueControl.TransactionsWaitingControl     = 0;
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
        Controller->QueueControl.TransactionQueueBulkIndex  = OHCI_NO_INDEX;
        Controller->QueueControl.TransactionsWaitingBulk    = 0;
    }
}

/* OhciReloadPeriodic
 * Reloads a periodic transfer that already exists in queue. 
 * Reloads transfer-descriptors, queue head and synchronizes toggles. */
void
OhciReloadPeriodic(
    _In_ OhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer)
{
    // Variables
    OhciTransferDescriptor_t *Td    = NULL;
    OhciQueueHead_t *Qh             = NULL;

    // Initiate values
    Qh                  = (OhciQueueHead_t*)Transfer->EndpointDescriptor;

    // Almost everything must be restored due to the HC relinking
    // things to the done-queue.
    Td                  = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    while (Td->LinkIndex != OHCI_NO_INDEX) {
        OhciTdRestart(Controller, Transfer, Td);
        Td              = &Controller->QueueControl.TDPool[Td->LinkIndex];
    }

    // Notify process of transfer of the status
    if (Transfer->Transfer.Type == InterruptTransfer) {
        UsbManagerSendNotification(Transfer);
    }

    // Restart endpoint
    Qh->Current         = OHCI_POOL_TDINDEX(Controller, Qh->ChildIndex); 
    Transfer->Status    = TransferQueued;
}

/* OhciProcessDoneQueue
 * Iterates all active transfers and handles completion/error events */
void
OhciProcessDoneQueue(
    _In_ OhciController_t*  Controller, 
    _In_ uintptr_t          DoneHeadAddress)
{
    // Variables
    reg32_t IocAddress                  = LODWORD(DoneHeadAddress);

    // Go through active transactions and locate the EP that was done
    foreach(Node, Controller->Base.TransactionList) {
        // Instantiate the usb-transfer pointer, and then the EP
        UsbManagerTransfer_t *Transfer  = (UsbManagerTransfer_t*)Node->Data;
        OhciQueueHead_t *QueueHead      = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
        if (QueueHead->IocAddress == IocAddress) {
            OhciQhValidate(Controller, Transfer, QueueHead);
            if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
                // Reload if this queue head was last in active queue
                if (QueueHead->LinkIndex == OHCI_NO_INDEX) {
                    OhciReloadControlBulk(Controller, Transfer->Transfer.Type);
                }

                if (OhciTransactionFinalize(Controller, Transfer, 1) == OsSuccess) {
                    CollectionRemoveByNode(Controller->Base.TransactionList, Node);
                    CollectionDestroyNode(Controller->Base.TransactionList, Node);
                }
            }
            else {
                OhciReloadPeriodic(Controller, Transfer);
            }
            return;
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
    foreach_nolink(Node, Controller->Base.TransactionList) {
        // Instantiate the usb-transfer pointer, and then the EP
        UsbManagerTransfer_t *Transfer  = (UsbManagerTransfer_t*)Node->Data;
        OhciQueueHead_t *QueueHead      = (OhciQueueHead_t*)Transfer->EndpointDescriptor;

        // Only check control/bulk for completion
        if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
            if (QueueHead != NULL && OhciIsTransferComplete(Controller, QueueHead) == OsSuccess) {
                OhciQhValidate(Controller, Transfer, QueueHead);
                
                // Reload if this was the last in the active queue
                if (QueueHead->LinkIndex == OHCI_NO_INDEX) {
                    OhciReloadControlBulk(Controller, Transfer->Transfer.Type);
                }
                
                // Handle completion of transfer
                // Finalize transaction and remove from list
                if (OhciTransactionFinalize(Controller, Transfer, 1) == OsSuccess) {
                    CollectionItem_t *NextNode  = CollectionUnlinkNode(Controller->Base.TransactionList, Node);
                    CollectionDestroyNode(Controller->Base.TransactionList, Node);
                    Node                        = NextNode;
                    continue;
                }
            }
        }
        
        // Go to next
        Node = CollectionNext(Node);
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
    foreach_nolink(Node, Controller->Base.TransactionList) {
        UsbManagerTransfer_t *Transfer  = (UsbManagerTransfer_t*)Node->Data;
        OhciQueueHead_t *QueueHead      = (OhciQueueHead_t*)Transfer->EndpointDescriptor;

        // Check flags for any requests, either schedule or unschedule
        if (QueueHead->HcdInformation & OHCI_QH_SCHEDULE) {
            if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
                OhciLinkGeneric(Controller, Transfer->Transfer.Type, QueueHead->Index);
            }
            else {
                // Link it in, and activate list
                OhciLinkPeriodic(Controller, QueueHead->Index);
                Controller->Registers->HcControl |= OHCI_CONTROL_PERIODIC_ACTIVE | OHCI_CONTROL_ISOC_ACTIVE;
            }

            // Remove the schedule flag
            QueueHead->HcdInformation &= ~OHCI_QH_SCHEDULE;
        }
        else if (QueueHead->HcdInformation & OHCI_QH_UNSCHEDULE) {
            // Handle completion of transfer
            if (Finalize == 1) {
                QueueHead->HcdInformation &= ~OHCI_QH_UNSCHEDULE;
                if (OhciTransactionFinalize(Controller, Transfer, 0) == OsSuccess) {
                    CollectionItem_t *NextNode  = CollectionUnlinkNode(Controller->Base.TransactionList, Node);
                    CollectionDestroyNode(Controller->Base.TransactionList, Node);
                    Node                        = NextNode;
                    continue;
                }
            }
            else {
                // Only interrupt and isoc requests unscheduling
                // this will be called again so don't remove unscheduling flag
                OhciUnlinkPeriodic(Controller, QueueHead->Index);
            }
        }

        // Go to next
        Node = CollectionNext(Node);
    }
}
