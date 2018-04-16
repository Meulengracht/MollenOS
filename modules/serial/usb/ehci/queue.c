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
 * - FSTN Transport
 * - Split-Isochronous Transport
 * - Transaction Translator Support
 */
#define __TRACE

/* Includes
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>
#include "ehci.h"

/* Includes
 * - Library */
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/* Globals 
 * Error messages for codes that might appear in transfers */
const char *EhciErrorMessages[] = {
    "No Error",
    "Ping State/PERR",
    "Split Transaction State",
    "Missed Micro-Frame",
    "Transaction Error (CRC, Timeout)",
    "Babble Detected",
    "Data Buffer Error",
    "Halted, Stall",
    "Active"
};

/* EhciQueueResetInternalData
 * Removes and cleans up any existing transfers, then reinitializes. */
OsStatus_t
EhciQueueResetInternalData(
    _In_ EhciController_t *Controller)
{
    // Variables
    EhciControl_t *Queue    = NULL;
    int i;

    // Shorthand the queue controller
    Queue                   = &Controller->QueueControl;

    // Initialize frame lists
    for (i = 0; i < Queue->FrameLength; i++) {
        Queue->VirtualList[i]                   = 0;
        Queue->FrameList[i]                     = EHCI_LINK_END;
    }

    // Initialize the QH pool
    for (i = 0; i < EHCI_POOL_NUM_QH; i++) {
        Queue->QHPool[i].HcdFlags               = 0;
        Queue->QHPool[i].Index                  = i;
        Queue->QHPool[i].LinkIndex              = EHCI_NO_INDEX;
        Queue->QHPool[i].ChildIndex             = EHCI_NO_INDEX;
    }

    // Initialize the ITD pool
    for (i = 0; i < EHCI_POOL_NUM_ITD; i++) {
        Queue->ITDPool[i].HcdFlags              = 0;
        Queue->ITDPool[i].Index                 = i;
        Queue->ITDPool[i].LinkIndex             = EHCI_NO_INDEX;
    }

    // Initialize the TD pool
    for (i = 0; i < EHCI_POOL_NUM_TD; i++) {
        Queue->TDPool[i].HcdFlags               = 0;
        Queue->TDPool[i].Index                  = i;
        Queue->TDPool[i].LinkIndex              = EHCI_NO_INDEX;
        Queue->TDPool[i].AlternativeLinkIndex   = EHCI_NO_INDEX;
    }

    // Initialize the dummy (async) queue-head that we use for end-link
    // It must be a circular queue, so must always point back to itself
    memset(&Queue->QHPool[EHCI_POOL_QH_ASYNC], 0, sizeof(EhciQueueHead_t));
    Queue->QHPool[EHCI_POOL_QH_ASYNC].LinkPointer               = (EHCI_POOL_QHINDEX(Controller, EHCI_POOL_QH_ASYNC)) | EHCI_LINK_QH;
    Queue->QHPool[EHCI_POOL_QH_ASYNC].LinkIndex                 = EHCI_POOL_QH_ASYNC;
    Queue->QHPool[EHCI_POOL_QH_ASYNC].Overlay.NextTD            = EHCI_POOL_TDINDEX(Controller, EHCI_POOL_TD_ASYNC) | EHCI_LINK_END;
    Queue->QHPool[EHCI_POOL_QH_ASYNC].Overlay.NextAlternativeTD = EHCI_POOL_TDINDEX(Controller, EHCI_POOL_TD_ASYNC);
    Queue->QHPool[EHCI_POOL_QH_ASYNC].Overlay.Status            = EHCI_TD_HALTED;
    Queue->QHPool[EHCI_POOL_QH_ASYNC].Flags                     = EHCI_QH_RECLAMATIONHEAD;
    Queue->QHPool[EHCI_POOL_QH_ASYNC].HcdFlags                  = EHCI_HCDFLAGS_ALLOCATED;
    Queue->QHPool[EHCI_POOL_QH_ASYNC].Index                     = EHCI_POOL_QH_ASYNC;

    // Initialize the dummy (async) transfer-descriptor that we use for queuing
    memset(&Queue->TDPool[EHCI_POOL_TD_ASYNC], 0, sizeof(EhciTransferDescriptor_t));
    Queue->TDPool[EHCI_POOL_TD_ASYNC].Token                     = EHCI_TD_IN;
    Queue->TDPool[EHCI_POOL_TD_ASYNC].Status                    = EHCI_TD_HALTED;
    Queue->TDPool[EHCI_POOL_TD_ASYNC].Link                      = EHCI_LINK_END;
    Queue->TDPool[EHCI_POOL_TD_ASYNC].AlternativeLink           = EHCI_LINK_END;
    Queue->TDPool[EHCI_POOL_TD_ASYNC].HcdFlags                  = EHCI_HCDFLAGS_ALLOCATED;
    Queue->TDPool[EHCI_POOL_TD_ASYNC].Index                     = EHCI_POOL_TD_ASYNC;
    Queue->TDPool[EHCI_POOL_TD_ASYNC].LinkIndex                 = EHCI_POOL_TD_ASYNC;
    Queue->TDPool[EHCI_POOL_TD_ASYNC].AlternativeLinkIndex      = EHCI_POOL_TD_ASYNC;
    return OsSuccess;
}

/* EhciQueueInitialize
 * Initialize the controller's queue resources and resets counters */
OsStatus_t
EhciQueueInitialize(
    _In_ EhciController_t *Controller)
{
    // Variables
    EhciControl_t *Queue    = NULL;
    uintptr_t RequiredSpace = 0, 
              PoolPhysical  = 0;
    void *Pool              = NULL;

    // Trace
    TRACE("EhciQueueInitialize()");

    // Shorthand the queue controller
    Queue                       = &Controller->QueueControl;

    // Null out queue-control
    memset(Queue, 0, sizeof(EhciControl_t));
    if (Controller->CParameters & (EHCI_CPARAM_VARIABLEFRAMELIST | EHCI_CPARAM_32FRAME_SUPPORT)) {
        if (Controller->CParameters & EHCI_CPARAM_32FRAME_SUPPORT) {
            Queue->FrameLength  = 32;
        }
        else {
            Queue->FrameLength  = 256;
        }
    }
    else {
        Queue->FrameLength      = 1024;
    }

    // Allocate a virtual list for keeping track of added
    // queue-heads in virtual space first.
    Queue->VirtualList          = (reg32_t*)malloc(Queue->FrameLength * sizeof(reg32_t));

    // Add up all the size we are going to need for pools and
    // the actual frame list
#define ADD_SPACE(SizeOfType, Count)    RequiredSpace += (SizeOfType * Count)
    ADD_SPACE(Queue->FrameLength, sizeof(reg32_t));
    ADD_SPACE(sizeof(EhciQueueHead_t), EHCI_POOL_NUM_QH);
    ADD_SPACE(sizeof(EhciIsochronousDescriptor_t), EHCI_POOL_NUM_ITD);
    ADD_SPACE(sizeof(EhciTransferDescriptor_t), EHCI_POOL_NUM_TD);
    Queue->PoolBytes            = RequiredSpace;
#undef ADD_SPACE

    // Perform the allocation
    if (MemoryAllocate(NULL, RequiredSpace, MEMORY_CLEAN | MEMORY_COMMIT
        | MEMORY_LOWFIRST | MEMORY_CONTIGIOUS, &Pool, &PoolPhysical) != OsSuccess) {
        ERROR("Failed to allocate memory for resource-pool");
        return OsError;
    }
    assert(PoolPhysical < 0xFFFFFFFF);

    // Store the physical address for the frame
    Queue->FrameList            = (reg32_t*)Pool;
    Queue->FrameListPhysical    = PoolPhysical;

    // Initialize addresses for pools
#define INIT_SPACE_POINTER(Pointer, PointerType, PreviousPointer, PreviousSize) Pointer = (PointerType)((uint8_t*)PreviousPointer + (PreviousSize))
    INIT_SPACE_POINTER(Queue->QHPool, EhciQueueHead_t*, Pool, Queue->FrameLength * sizeof(reg32_t));
    INIT_SPACE_POINTER(Queue->QHPoolPhysical, uintptr_t, PoolPhysical, Queue->FrameLength * sizeof(reg32_t));
    
    INIT_SPACE_POINTER(Queue->ITDPool, EhciIsochronousDescriptor_t*, Queue->QHPool, sizeof(EhciQueueHead_t) * EHCI_POOL_NUM_QH);
    INIT_SPACE_POINTER(Queue->ITDPoolPhysical, uintptr_t, Queue->QHPoolPhysical, sizeof(EhciQueueHead_t) * EHCI_POOL_NUM_QH);

    INIT_SPACE_POINTER(Queue->TDPool, EhciTransferDescriptor_t*, Queue->ITDPool, sizeof(EhciIsochronousDescriptor_t) * EHCI_POOL_NUM_ITD);
    INIT_SPACE_POINTER(Queue->TDPoolPhysical, uintptr_t, Queue->ITDPoolPhysical, sizeof(EhciIsochronousDescriptor_t) * EHCI_POOL_NUM_ITD);
#undef INIT_SPACE_POINTER

    //Controller->Scheduler       = UsbSchedulerInitialize(Queue->FrameLength, EHCI_MAX_BANDWIDTH, 8);
    return EhciQueueResetInternalData(Controller);
}

/* EhciQueueReset
 * Removes and cleans up any existing transfers, then reinitializes. */
OsStatus_t
EhciQueueReset(
    EhciController_t *Controller)
{
    // Variables
    CollectionItem_t *tNode = NULL;

    // Debug
    TRACE("EhciQueueReset()");

    // Stop Controller
    EhciHalt(Controller);

    // Iterate all queued transactions and dequeue
    _foreach(tNode, Controller->Base.TransactionList) {
        if (((UsbManagerTransfer_t*)tNode->Data)->Transfer.Type == IsochronousTransfer) {
            EhciTransactionFinalizeIsoc(Controller, (UsbManagerTransfer_t*)tNode->Data);
        }
        else {
            EhciTransactionFinalizeGeneric(Controller, (UsbManagerTransfer_t*)tNode->Data);
        }
    }
    CollectionClear(Controller->Base.TransactionList);
    return EhciQueueResetInternalData(Controller);
}

/* EhciQueueDestroy
 * Unschedules any scheduled ed's and frees all resources allocated
 * by the initialize function */
OsStatus_t
EhciQueueDestroy(
    _In_ EhciController_t*  Controller)
{
    // Debug
    TRACE("EhciQueueDestroy()");

    // Reset first
    EhciQueueReset(Controller);

    // Cleanup resources
    CollectionDestroy(Controller->Base.TransactionList);
    MemoryFree(Controller->QueueControl.QHPool, Controller->QueueControl.PoolBytes);
    return OsSuccess;
}

/* EhciConditionCodeToIndex
 * Converts a given condition bit-index to number */
int
EhciConditionCodeToIndex(
    _In_ unsigned           ConditionCode)
{
    // Variables
    unsigned Cc = ConditionCode;
    int bCount  = 0;

    // Shift untill we reach 0, count number of shifts
    for (; Cc != 0;) {
        bCount++;
        Cc >>= 1;
    }
    return bCount;
}

/* EhciSetPrefetching
 * Disables the prefetching related to the transfer-type. */
OsStatus_t
EhciSetPrefetching(
    _In_ EhciController_t*  Controller,
    _In_ UsbTransferType_t  Type,
    _In_ int                Set)
{
    // Variables
    reg32_t Command         = Controller->OpRegisters->UsbCommand;
    if (!(Controller->CParameters & EHCI_CPARAM_HWPREFETCH)) {
        return OsError;
    }
    
    // Detect type of prefetching
    if (Type == ControlTransfer || Type == BulkTransfer) {
        if (!Set) {
            Command &= ~(EHCI_COMMAND_ASYNC_PREFETCH);
            Controller->OpRegisters->UsbCommand = Command;
            MemoryBarrier();
            while (Controller->OpRegisters->UsbCommand & EHCI_COMMAND_ASYNC_PREFETCH);
        }
        else {
            Command |= EHCI_COMMAND_ASYNC_PREFETCH;
            Controller->OpRegisters->UsbCommand = Command;
        }
    }
    else {
        if (!Set) {
            Command &= ~(EHCI_COMMAND_PERIOD_PREFECTCH);
            Controller->OpRegisters->UsbCommand = Command;
            MemoryBarrier();
            while (Controller->OpRegisters->UsbCommand & EHCI_COMMAND_PERIOD_PREFECTCH);
        }
        else {
            Command |= EHCI_COMMAND_PERIOD_PREFECTCH;
            Controller->OpRegisters->UsbCommand = Command;
        }
    }
    return OsSuccess;
}

/* EhciEnableScheduler
 * Enables the relevant scheduler if it is not enabled already */
void
EhciEnableScheduler(
    _In_ EhciController_t*  Controller,
    _In_ UsbTransferType_t  Type)
{
    // Variables
    reg32_t Temp    = 0;

    // Sanitize the current status
    if (Type == ControlTransfer || Type == BulkTransfer) {
        if (Controller->OpRegisters->UsbStatus & EHCI_STATUS_ASYNC_ACTIVE) {
            return;
        }

        // Fire the enable command
        Temp                                = Controller->OpRegisters->UsbCommand;
        Temp                                |= EHCI_COMMAND_ASYNC_ENABLE;
        Controller->OpRegisters->UsbCommand = Temp;
    }
    else {
        if (Controller->OpRegisters->UsbStatus & EHCI_STATUS_PERIODIC_ACTIVE) {
            return;
        }

        // Fire the enable command
        Temp                                = Controller->OpRegisters->UsbCommand;
        Temp                                |= EHCI_COMMAND_PERIODIC_ENABLE;
        Controller->OpRegisters->UsbCommand = Temp;
    }
}

/* EhciDisableScheduler
 * Disables the sheduler if it is not disabled already */
void
EhciDisableScheduler(
    _In_ EhciController_t*  Controller,
    _In_ UsbTransferType_t  Type)
{
    // Variables
    reg32_t Temp    = 0;

    // Sanitize its current status
    if (Type == ControlTransfer || Type == BulkTransfer) {
        if (!(Controller->OpRegisters->UsbStatus & EHCI_STATUS_ASYNC_ACTIVE)) {
            return;
        }

        // Fire off disable command
        Temp                                = Controller->OpRegisters->UsbCommand;
        Temp                                &= ~(EHCI_COMMAND_ASYNC_ENABLE);
        Controller->OpRegisters->UsbCommand = Temp;
    }
    else {
        if (!(Controller->OpRegisters->UsbStatus & EHCI_STATUS_PERIODIC_ACTIVE)) {
            return;
        }

        // Fire off disable command
        Temp                                = Controller->OpRegisters->UsbCommand;
        Temp                                &= ~(EHCI_COMMAND_PERIODIC_ENABLE);
        Controller->OpRegisters->UsbCommand = Temp;
    }
}

/* EhciRingDoorbell
 * This functions rings the bell */
void
EhciRingDoorbell(
    _In_ EhciController_t*  Controller)
{
    // If the bell is already ringing, force a re-bell
    if (!Controller->QueueControl.BellIsRinging) {
        Controller->QueueControl.BellIsRinging  = 1;
        Controller->OpRegisters->UsbCommand     |= EHCI_COMMAND_IOC_ASYNC_DOORBELL;
    }
    else {
        Controller->QueueControl.BellReScan     = 1;
    }
}

/* EhciNextGenericLink
 * Get's a pointer to the next virtual link, only Qh's have this implemented 
 * right now and will need modifications */
EhciGenericLink_t*
EhciNextGenericLink(
    _In_ EhciController_t*   Controller,
    _In_ EhciGenericLink_t*  Link, 
    _In_ int                 Type)
{
    switch (Type) {
        case EHCI_LINK_iTD:
            return (EhciGenericLink_t*)&Controller->QueueControl.ITDPool[Link->iTd->LinkIndex];
        case EHCI_LINK_QH:
            return (EhciGenericLink_t*)&Controller->QueueControl.QHPool[Link->Qh->LinkIndex];
        //case EHCI_LINK_FSTN:
        //    return (EhciGenericLink_t*)&Link->FSTN->PathPointer;
        //case EHCI_LINK_siTD:
        //    return (EhciGenericLink_t*)&Link->siTd->Link;
        default: {
            ERROR("Unsupported link of type %i requested", Type);
            return NULL;
        }
    }
}

/* EhciUnlinkPeriodic
 * Generic unlink from periodic list needs a bit more information as it
 * is used for all formats */
void
EhciUnlinkPeriodic(
    _In_ EhciController_t*  Controller, 
    _In_ uintptr_t          Address, 
    _In_ size_t             Period, 
    _In_ size_t             StartFrame)
{
    // Variables
    size_t i;

    // Sanity the period, it must be _atleast_ 1
    if (Period == 0) {
        Period = 1;
    }

    // We should mark Qh->Flags |= EHCI_QH_INVALIDATE_NEXT 
    // and wait for next frame
    for (i = StartFrame; i < Controller->QueueControl.FrameLength; i += Period) {
        // Retrieve a virtual pointer and a physical
        EhciGenericLink_t *VirtualLink  = (EhciGenericLink_t*)&Controller->QueueControl.VirtualList[i];
        reg32_t *PhysicalLink           = &Controller->QueueControl.FrameList[i];
        EhciGenericLink_t This          = *VirtualLink;
        reg32_t Type                    = 0;

        // Find previous handle that points to our qh
        while (This.Address && This.Address != Address) {
            Type            = EHCI_LINK_TYPE(*PhysicalLink);
            VirtualLink     = EhciNextGenericLink(Controller, VirtualLink, Type);
            PhysicalLink    = &This.Address;
            This            = *VirtualLink;
        }

        // Sanitize end of list, it didn't exist
        if (!This.Address) {
            return;
        }

        // Perform the unlinking
        Type            = EHCI_LINK_TYPE(*PhysicalLink);
        *VirtualLink    = *EhciNextGenericLink(Controller, &This, Type);
        if (*(&This.Address) != EHCI_LINK_END) {
            *PhysicalLink = *(&This.Address);
        }
    }
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

/* EhciGetStatusCode
 * Retrieves a status-code from a given condition code */
UsbTransferStatus_t
EhciGetStatusCode(
    _In_ int                ConditionCode)
{
    // One huuuge if/else
    if (ConditionCode == 0) {
        return TransferFinished;
    }
    else if (ConditionCode == 4) {
        return TransferNotResponding;
    }
    else if (ConditionCode == 5) {
        return TransferBabble;
    }
    else if (ConditionCode == 6) {
        return TransferBufferError;
    }
    else if (ConditionCode == 7) {
        return TransferStalled;
    }
    else {
        WARNING("EHCI-Error: 0x%x (%s)", ConditionCode, EhciErrorMessages[ConditionCode]);
        return TransferInvalid;
    }
}

/* EhciProcessTransfers
 * For transaction progress this involves done/error transfers */
void
EhciProcessTransfers(
    _In_ EhciController_t *Controller)
{
    // Iterate active transfers
    foreach(Node, Controller->Base.TransactionList) {
        // Instantiate a transaction pointer
        UsbManagerTransfer_t *Transfer  = (UsbManagerTransfer_t*)Node->Data;
        int Processed                   = 0;

        // Handle different transfer types
        if (Transfer->Transfer.Type != IsochronousTransfer) {
            Processed = EhciScanQh(Controller, Transfer);
            if (Processed) {
                if (Transfer->Transfer.Type == InterruptTransfer) {
                    EhciRestartQh(Controller, Transfer);
                }
                else {
                    EhciTransactionFinalizeGeneric(Controller, Transfer);
                }
            }
        }
        else {
            Processed = EhciScanIsocTd(Controller, Transfer);
            if (Processed) {
                EhciRestartIsocTd(Controller, Transfer);
            }
        }

        if (Processed && 
            (Transfer->Transfer.Type == InterruptTransfer || Transfer->Transfer.Type == IsochronousTransfer)) {
            UsbManagerSendNotification(Transfer);
        }
    }
}

/* EhciProcessDoorBell
 * This makes sure to schedule and/or unschedule transfers */
void
EhciProcessDoorBell(
    _In_ EhciController_t*  Controller,
    _In_ int                InitialScan)
{
    // Variables
    CollectionItem_t *Node = NULL;

Scan:
    // As soon as we enter the scan area we reset the re-scan
    // to allow other threads to set it again
    Controller->QueueControl.BellReScan = 0;

    // Iterate active transfers
    _foreach_nolink(Node, Controller->Base.TransactionList) {
        // Instantiate a transaction pointer
        UsbManagerTransfer_t *Transfer  = (UsbManagerTransfer_t*)Node->Data;
        if (Transfer->Transfer.Type != IsochronousTransfer) {
            EhciQueueHead_t *Qh         = (EhciQueueHead_t*)Transfer->EndpointDescriptor;
            if ((Qh->HcdFlags & EHCI_HCDFLAGS_UNSCHEDULE) && InitialScan != 0) {
                if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
                    // Nothing to do here really for now
                }
            }
            else if ((Qh->HcdFlags & EHCI_HCDFLAGS_UNSCHEDULE) && InitialScan == 0) {
                Qh->HcdFlags &= ~(EHCI_HCDFLAGS_UNSCHEDULE);
                if (EchiCleanupTransferGeneric(Controller, Transfer) == OsSuccess) {
                    CollectionItem_t *NextNode = CollectionUnlinkNode(Controller->Base.TransactionList, Node);
                    CollectionDestroyNode(Controller->Base.TransactionList, Node);
                    Node = NextNode;
                    free(Transfer);
                    continue;
                }
            }
        }
        else { // Isochronous transfers
            EhciIsochronousDescriptor_t *iTd = (EhciIsochronousDescriptor_t*)Transfer->EndpointDescriptor;
            if ((iTd->HcdFlags & EHCI_HCDFLAGS_UNSCHEDULE) && InitialScan != 0) {
                // Nothing to do here really for now
            }
            else if ((iTd->HcdFlags & EHCI_HCDFLAGS_UNSCHEDULE) && InitialScan == 0) {
                iTd->HcdFlags &= ~(EHCI_HCDFLAGS_UNSCHEDULE);
                if (EchiCleanupTransferIsoc(Controller, Transfer) == OsSuccess) {
                    CollectionItem_t *NextNode = CollectionUnlinkNode(Controller->Base.TransactionList, Node);
                    CollectionDestroyNode(Controller->Base.TransactionList, Node);
                    Node = NextNode;
                    free(Transfer);
                    continue;
                }
            }
        }
        
        // Go to next
        Node = CollectionNext(Node);
    }

    // If someone has rung the bell while 
    // the door was opened, we should not close the door yet
    if (Controller->QueueControl.BellReScan != 0) {
        goto Scan;
    }

    // Bell is no longer ringing
    Controller->QueueControl.BellIsRinging = 0;
}
