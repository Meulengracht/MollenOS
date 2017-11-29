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
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 * - FSTN Transport
 * - Isochronous Transport
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

/* Disable the warning about conditional
 * expressions being constant, they are intentional */
#ifdef _MSC_VER
#pragma warning(disable:4127)
#endif

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
	EhciControl_t *Queue = NULL;
	int i;

	// Shorthand the queue controller
	Queue = &Controller->QueueControl;

    // Initialize frame lists
	for (i = 0; i < Queue->FrameLength; i++) {
		Queue->VirtualList[i] = Queue->FrameList[i] = EHCI_LINK_END;
	}

	// Initialize the QH pool
	for (i = 0; i < EHCI_POOL_NUM_QH; i++) {
        Queue->QHPool[i].HcdFlags = 0;
		Queue->QHPool[i].Index = i;
		Queue->QHPool[i].LinkIndex = EHCI_NO_INDEX;
        Queue->QHPool[i].ChildIndex = EHCI_NO_INDEX;
	}

	// Initialize the TD pool
	for (i = 0; i < EHCI_POOL_NUM_TD; i++) {
        Queue->TDPool[i].HcdFlags = 0;
		Queue->TDPool[i].Index = i;
		Queue->TDPool[i].LinkIndex = EHCI_NO_INDEX;
		Queue->TDPool[i].AlternativeLinkIndex = EHCI_NO_INDEX;
	}

	// Initialize the dummy (null) queue-head that we use for end-link
    memset(&Queue->QHPool[EHCI_POOL_QH_NULL], 0, sizeof(EhciQueueHead_t));
	Queue->QHPool[EHCI_POOL_QH_NULL].Overlay.NextTD = EHCI_LINK_END;
	Queue->QHPool[EHCI_POOL_QH_NULL].Overlay.NextAlternativeTD = EHCI_LINK_END;
	Queue->QHPool[EHCI_POOL_QH_NULL].LinkPointer = EHCI_LINK_END;
	Queue->QHPool[EHCI_POOL_QH_NULL].HcdFlags = EHCI_QH_ALLOCATED;
    Queue->QHPool[EHCI_POOL_QH_NULL].Index = EHCI_POOL_QH_NULL;
    Queue->QHPool[EHCI_POOL_QH_NULL].LinkIndex = EHCI_NO_INDEX;
    Queue->QHPool[EHCI_POOL_QH_NULL].ChildIndex = EHCI_NO_INDEX;

	// Initialize the dummy (async) transfer-descriptor that we use for queuing
    memset(&Queue->TDPool[EHCI_POOL_TD_ASYNC], 0, sizeof(EhciTransferDescriptor_t));
    Queue->TDPool[EHCI_POOL_TD_ASYNC].Index = EHCI_POOL_TD_ASYNC;
	Queue->TDPool[EHCI_POOL_TD_ASYNC].Link = EHCI_LINK_END;
    Queue->TDPool[EHCI_POOL_TD_ASYNC].LinkIndex = EHCI_NO_INDEX;
    Queue->TDPool[EHCI_POOL_TD_ASYNC].AlternativeLink = EHCI_LINK_END;
    Queue->TDPool[EHCI_POOL_TD_ASYNC].AlternativeLinkIndex = EHCI_NO_INDEX;
    Queue->TDPool[EHCI_POOL_TD_ASYNC].HcdFlags = EHCI_TD_ALLOCATED;
    Queue->TDPool[EHCI_POOL_TD_ASYNC].Token = EHCI_TD_IN;
    Queue->TDPool[EHCI_POOL_TD_ASYNC].Status = EHCI_TD_HALTED;

	// Initialize the dummy (async) queue-head that we use for end-link
    // It must be a circular queue, so must always point back to itself
    memset(&Queue->QHPool[EHCI_POOL_QH_ASYNC], 0, sizeof(EhciQueueHead_t));
	Queue->QHPool[EHCI_POOL_QH_ASYNC].LinkPointer = 
		(EHCI_POOL_QHINDEX(Controller, EHCI_POOL_QH_ASYNC)) | EHCI_LINK_QH;
	Queue->QHPool[EHCI_POOL_QH_ASYNC].LinkIndex = EHCI_POOL_QH_ASYNC;
    
	Queue->QHPool[EHCI_POOL_QH_ASYNC].Overlay.NextTD = 
		EHCI_POOL_TDINDEX(Controller, EHCI_POOL_TD_ASYNC) | EHCI_LINK_END;
    Queue->TDPool[EHCI_POOL_TD_ASYNC].LinkIndex = EHCI_POOL_TD_ASYNC;
	Queue->QHPool[EHCI_POOL_QH_ASYNC].Overlay.NextAlternativeTD =
		EHCI_POOL_TDINDEX(Controller, EHCI_POOL_TD_ASYNC);
    Queue->TDPool[EHCI_POOL_TD_ASYNC].AlternativeLinkIndex = EHCI_POOL_TD_ASYNC;

	Queue->QHPool[EHCI_POOL_QH_ASYNC].Overlay.Status = EHCI_TD_HALTED;
	Queue->QHPool[EHCI_POOL_QH_ASYNC].Flags = EHCI_QH_RECLAMATIONHEAD;
	Queue->QHPool[EHCI_POOL_QH_ASYNC].HcdFlags = EHCI_QH_ALLOCATED;
    Queue->QHPool[EHCI_POOL_QH_ASYNC].Index = EHCI_POOL_QH_ASYNC;

    // Done
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
	Queue = &Controller->QueueControl;

	// Null out queue-control
	memset(Queue, 0, sizeof(EhciControl_t));

	// The first thing we want to do is 
	// to determine the size of the frame list, if we can control it ourself
	// we set it to the shortest available (not 32 tho)
	if (Controller->CParameters & EHCI_CPARAM_VARIABLEFRAMELIST) {
		Queue->FrameLength = 256;
	}
 	else {
		Queue->FrameLength = 1024;
	}

	// Allocate a virtual list for keeping track of added
	// queue-heads in virtual space first.
	Queue->VirtualList  = (reg32_t*)malloc(Queue->FrameLength * sizeof(reg32_t));

	// Add up all the size we are going to need for pools and
	// the actual frame list
	RequiredSpace       += Queue->FrameLength * sizeof(reg32_t);        // Framelist
	RequiredSpace       += sizeof(EhciQueueHead_t) * EHCI_POOL_NUM_QH;  // Qh-pool
	RequiredSpace       += sizeof(EhciTransferDescriptor_t) * EHCI_POOL_NUM_TD; // Td-pool
    Queue->PoolBytes    = RequiredSpace;

	// Perform the allocation
	if (MemoryAllocate(RequiredSpace, MEMORY_CLEAN | MEMORY_COMMIT
		| MEMORY_LOWFIRST | MEMORY_CONTIGIOUS, &Pool, &PoolPhysical) != OsSuccess) {
		ERROR("Failed to allocate memory for resource-pool");
		return OsError;
	}

	// Store the physical address for the frame
	Queue->FrameList = (reg32_t*)Pool;
	Queue->FrameListPhysical = PoolPhysical;

	// Initialize addresses for pools
	Queue->QHPool = (EhciQueueHead_t*)
		((uint8_t*)Pool + (Queue->FrameLength * sizeof(reg32_t)));
	Queue->QHPoolPhysical = PoolPhysical + (Queue->FrameLength * sizeof(reg32_t));
	Queue->TDPool = (EhciTransferDescriptor_t*)
		((uint8_t*)Queue->QHPool + (sizeof(EhciQueueHead_t) * EHCI_POOL_NUM_QH));
	Queue->TDPoolPhysical = Queue->QHPoolPhysical + (sizeof(EhciQueueHead_t) * EHCI_POOL_NUM_QH);

	// Allocate the transaction list
	Queue->TransactionList = CollectionCreate(KeyInteger);

	// Initialize a bandwidth scheduler
	Controller->Scheduler = UsbSchedulerInitialize(Queue->FrameLength, EHCI_MAX_BANDWIDTH, 8);

    // Reset data structures before updating HW lists
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
    _foreach(tNode, Controller->QueueControl.TransactionList) {
        EhciTransactionFinalize(Controller, 
            (UsbManagerTransfer_t*)tNode->Data, 0);
    }
    CollectionClear(Controller->QueueControl.TransactionList);

    // Reinitialize internal data
    return EhciQueueResetInternalData(Controller);
}

/* EhciQueueDestroy
 * Unschedules any scheduled ed's and frees all resources allocated
 * by the initialize function */
OsStatus_t
EhciQueueDestroy(
	_In_ EhciController_t *Controller)
{
    // Debug
    TRACE("EhciQueueDestroy()");

    // Reset first
    EhciQueueReset(Controller);

    // Cleanup resources
    CollectionDestroy(Controller->QueueControl.TransactionList);
    MemoryFree(Controller->QueueControl.QHPool, 
        Controller->QueueControl.PoolBytes);
	return OsSuccess;
}

/* EhciConditionCodeToIndex
 * Converts a given condition bit-index to number */
int
EhciConditionCodeToIndex(
	_In_ unsigned ConditionCode)
{
    // Variables
	unsigned Cc = ConditionCode;
	int bCount = 0;

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
    _In_ EhciController_t   *Controller,
    _In_ UsbTransferType_t   Type,
    _In_ int                 Set)
{
    // Variables
	reg32_t Command         = Controller->OpRegisters->UsbCommand;
    
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

/* EhciEnableAsyncScheduler
 * Enables the async scheduler if it is not enabled already */
void
EhciEnableAsyncScheduler(
    _In_ EhciController_t *Controller)
{
	// Variables
	reg32_t Temp = 0;

	// Sanitize the current status
	if (Controller->OpRegisters->UsbStatus & EHCI_STATUS_ASYNC_ACTIVE) {
		return;
    }

	// Fire the enable command
	Temp = Controller->OpRegisters->UsbCommand;
	Temp |= EHCI_COMMAND_ASYNC_ENABLE;
	Controller->OpRegisters->UsbCommand = Temp;
}

/* EhciDisableAsyncScheduler
 * Disables the async sheduler if it is not disabled already */
void
EhciDisableAsyncScheduler(
    _In_ EhciController_t *Controller)
{
    // Variables
    reg32_t Temp = 0;

	// Sanitize its current status
	if (!(Controller->OpRegisters->UsbStatus & EHCI_STATUS_ASYNC_ACTIVE)) {
		return;
    }

	// Fire off disable command
	Temp = Controller->OpRegisters->UsbCommand;
	Temp &= ~(EHCI_COMMAND_ASYNC_ENABLE);
	Controller->OpRegisters->UsbCommand = Temp;
}

/* EhciRingDoorbell
 * This functions rings the bell */
void
EhciRingDoorbell(
    _In_ EhciController_t *Controller)
{
	// If the bell is already ringing, force a re-bell
	if (!Controller->QueueControl.BellIsRinging) {
		Controller->QueueControl.BellIsRinging = 1;
		Controller->OpRegisters->UsbCommand |= EHCI_COMMAND_IOC_ASYNC_DOORBELL;
	}
	else {
		Controller->QueueControl.BellReScan = 1;
    }
}

/* EhciNextGenericLink
 * Get's a pointer to the next virtual link, only Qh's have this implemented 
 * right now and will need modifications */
EhciGenericLink_t*
EhciNextGenericLink(
    EhciController_t *Controller,
    EhciGenericLink_t *Link, 
    int Type)
{
	switch (Type) {
        case EHCI_LINK_QH:
            return (EhciGenericLink_t*)&Controller->QueueControl.QHPool[Link->Qh->LinkIndex];
        //case EHCI_LINK_FSTN:
        //    return (EhciGenericLink_t*)&Link->FSTN->PathPointer;
        //case EHCI_LINK_iTD:
        //    return (EhciGenericLink_t*)&Link->iTd->Link;
        //case EHCI_LINK_siTD:
        //    return (EhciGenericLink_t*)&Link->siTd->Link;
        default: {
            ERROR("Unsupported link of type %i requested", Type);
            return NULL;
        }
	}
}

/* EhciLinkPeriodicQh
 * This function links an interrupt Qh into the schedule at Qh->sFrame 
 * and every other Qh->Interval */
void
EhciLinkPeriodicQh(
    _In_ EhciController_t *Controller, 
    _In_ EhciQueueHead_t *Qh)
{
    // Variables
	int Interval    = (int)Qh->Interval;
	int i;

	// Sanity the interval, it must be _atleast_ 1
    if (Interval == 0) {
		Interval = 1;
    }

	// Iterate the entire framelist and install the periodic qh
	for (i = (int)Qh->sFrame; i < (int)Controller->QueueControl.FrameLength; i += Interval) {
		// Retrieve a virtual pointer and a physical
        EhciGenericLink_t *VirtualLink =
            (EhciGenericLink_t*)&Controller->QueueControl.VirtualList[i];
		uintptr_t *PhysicalLink = &Controller->QueueControl.FrameList[i];
		EhciGenericLink_t This = *VirtualLink;
		uintptr_t Type = 0;

		// Iterate past isochronous tds
		while (This.Address) {
			Type = EHCI_LINK_TYPE(*PhysicalLink);
			if (Type == EHCI_LINK_QH) {
				break;
            }

            // Update iterators
			VirtualLink = EhciNextGenericLink(Controller, VirtualLink, Type);
			PhysicalLink = &This.Address;
			This = *VirtualLink;
		}

		// sorting each branch by period (slow-->fast)
		// enables sharing interior tree nodes
		while (This.Address && Qh != This.Qh) {
			if (Qh->Interval > This.Qh->Interval) {
				break;
            }

			// Update iterators
			VirtualLink = (EhciGenericLink_t*)&Controller->QueueControl.QHPool[This.Qh->LinkIndex];
			PhysicalLink = &This.Qh->LinkPointer;
			This = *VirtualLink;
		}

		// link in this qh, unless some earlier pass did that
		if (Qh != This.Qh) {
			Qh->LinkIndex = This.Qh->Index;
			if (This.Qh) {
				Qh->LinkPointer = *PhysicalLink;
            }

            // Flush memory writes
			MemoryBarrier();

			// Perform linking
			VirtualLink->Qh = Qh;
			*PhysicalLink = (EHCI_POOL_QHINDEX(Controller, Qh->Index) | EHCI_LINK_QH);
		}
	}
}

/* EhciUnlinkPeriodic
 * Generic unlink from periodic list needs a bit more information as it
 * is used for all formats */
void
EhciUnlinkPeriodic(
    _In_ EhciController_t *Controller, 
    _In_ uintptr_t Address, 
    _In_ size_t Period, 
    _In_ size_t sFrame)
{
	// Variables
	size_t i;

	// Sanity the period, it must be _atleast_ 1
    if (Period == 0) {
		Period = 1;
    }

	// We should mark Qh->Flags |= EHCI_QH_INVALIDATE_NEXT 
	// and wait for next frame
	for (i = sFrame; i < Controller->QueueControl.FrameLength; i += Period) {
		// Retrieve a virtual pointer and a physical
        EhciGenericLink_t *VirtualLink =
            (EhciGenericLink_t*)&Controller->QueueControl.VirtualList[i];
        uintptr_t *PhysicalLink = &Controller->QueueControl.FrameList[i];
        EhciGenericLink_t This = *VirtualLink;
        uintptr_t Type = 0;

		// Find previous handle that points to our qh
		while (This.Address && This.Address != Address) {
			Type = EHCI_LINK_TYPE(*PhysicalLink);
			VirtualLink = EhciNextGenericLink(Controller, VirtualLink, Type);
			PhysicalLink = &This.Address;
			This = *VirtualLink;
		}

		// Sanitize end of list, it didn't exist
		if (!This.Address) {
			return;
        }

		// Perform the unlinking
		Type = EHCI_LINK_TYPE(*PhysicalLink);
		*VirtualLink = *EhciNextGenericLink(Controller, &This, Type);

		if (*(&This.Address) != EHCI_LINK_END) {
			*PhysicalLink = *(&This.Address);
        }
	}
}

/* EhciQhAllocate
 * This allocates a QH for a Control, Bulk and Interrupt 
 * transaction and should not be used for isoc */
EhciQueueHead_t*
EhciQhAllocate(
    _In_ EhciController_t *Controller)
{
	// Variables
    EhciQueueHead_t *Qh = NULL;
    int i;

	// Acquire controller lock
    SpinlockAcquire(&Controller->Base.Lock);
    
    // Iterate the pool and find a free entry
    for (i = EHCI_POOL_QH_START; i < EHCI_POOL_NUM_QH; i++) {
        if (Controller->QueueControl.QHPool[i].HcdFlags & EHCI_QH_ALLOCATED) {
            continue;
        }

        // Set initial state
        memset(&Controller->QueueControl.QHPool[i], 0, sizeof(EhciQueueHead_t));
        Controller->QueueControl.QHPool[i].Index = i;
        Controller->QueueControl.QHPool[i].Overlay.Status = EHCI_TD_HALTED;
        Controller->QueueControl.QHPool[i].HcdFlags = EHCI_QH_ALLOCATED;
        Controller->QueueControl.QHPool[i].LinkIndex = EHCI_NO_INDEX;
        Controller->QueueControl.QHPool[i].ChildIndex = EHCI_NO_INDEX;
        Qh = &Controller->QueueControl.QHPool[i];
        break;
    }

	// Release controller lock
	SpinlockRelease(&Controller->Base.Lock);
	return Qh;
}

/* EhciQhInitialize
 * This initiates any periodic scheduling information 
 * that might be needed */
OsStatus_t
EhciQhInitialize(
    _In_ EhciController_t *Controller, 
    _In_ EhciQueueHead_t *Qh,
    _In_ UsbSpeed_t Speed,
    _In_ int Direction,
    _In_ UsbTransferType_t Type,
    _In_ size_t EndpointInterval,
    _In_ size_t EndpointMaxPacketSize,
    _In_ size_t TransferLength)
{
    // Variables
    int TransactionsPerFrame = DIVUP(TransferLength, EndpointMaxPacketSize);

	// Calculate the neccessary bandwidth
	Qh->Bandwidth = (reg32_t)
        NS_TO_US(UsbCalculateBandwidth(Speed, 
            Direction, Type, TransferLength));

    // Calculate the frame period
    // If highspeed/fullspeed or Isoc calculate period as 2^(Interval-1)
	if (Speed == HighSpeed
		|| (Speed == FullSpeed && Type == IsochronousTransfer)) {
		Qh->Interval = (1 << EndpointInterval);
	}
	else {
		Qh->Interval = EndpointInterval;
    }

	// Validate Bandwidth
    return UsbSchedulerValidate(Controller->Scheduler, 
        Qh->Interval, Qh->Bandwidth, TransactionsPerFrame);
}

/* EhciTdAllocate
 * This allocates a QTD (TD) for Control, Bulk and Interrupt */
EhciTransferDescriptor_t*
EhciTdAllocate(
    _In_ EhciController_t *Controller)
{
	// Variables
	EhciTransferDescriptor_t *Td = NULL;
	int i;

	// Acquire controller lock
    SpinlockAcquire(&Controller->Base.Lock);
    
    // Iterate the pool and find a free entry
    for (i = 0; i < EHCI_POOL_TD_ASYNC; i++) {
        if (Controller->QueueControl.TDPool[i].HcdFlags & EHCI_TD_ALLOCATED) {
            continue;
        }

        // Perform allocation
        Controller->QueueControl.TDPool[i].HcdFlags = EHCI_TD_ALLOCATED;
        Td = &Controller->QueueControl.TDPool[i];
        break;
	}

    // Sanitize end of list, no allocations?
    if (Td == NULL) {
        ERROR("EhciTdAllocate::Ran out of TD's");
    }

	// Release controller lock
	SpinlockRelease(&Controller->Base.Lock);
	return Td;
}

/* EhciTdFill
 * This sets up a QTD (TD) buffer structure and makes 
 * sure it's split correctly out on all the pages */
size_t
EhciTdFill(
    _In_ EhciTransferDescriptor_t *Td, 
    _In_ uintptr_t BufferAddress, 
    _In_ size_t Length)
{
	// Variables
	size_t LengthRemaining = Length;
	size_t Count = 0;
	int i;

	// Sanitize parameters
	if (Length == 0 || BufferAddress == 0) {
		return 0;
    }

	// Iterate buffers
	for (i = 0; LengthRemaining > 0 && i < 5; i++) {
		uintptr_t Physical = BufferAddress + (i * 0x1000);
        
        // Update buffer
        Td->Buffers[i] = EHCI_TD_BUFFER(Physical);
		if (sizeof(uintptr_t) > 4) {
			Td->ExtBuffers[i] = EHCI_TD_EXTBUFFER(Physical);
        }
		else {
			Td->ExtBuffers[i] = 0;
        }

		// Update iterators
		Count += MIN(0x1000, LengthRemaining);
		LengthRemaining -= MIN(0x1000, LengthRemaining);
    }

    // Return how many bytes were "buffered"
	return Count;
}

/* EhciTdSetup
 * This allocates & initializes a TD for a setup transaction 
 * this is only used for control transactions */
EhciTransferDescriptor_t*
EhciTdSetup(
    _In_ EhciController_t *Controller, 
	_In_ UsbTransaction_t *Transaction)
{
	// Variables
	EhciTransferDescriptor_t *Td;

	// Allocate the transfer-descriptor
	Td = EhciTdAllocate(Controller);

	// Initialize the transfer-descriptor
	Td->Link = EHCI_LINK_END;
    Td->AlternativeLink = EHCI_LINK_END;
    Td->AlternativeLinkIndex = EHCI_NO_INDEX;
	Td->Status = EHCI_TD_ACTIVE;
	Td->Token = EHCI_TD_SETUP;
	Td->Token |= EHCI_TD_ERRCOUNT;

	// Calculate the length of the setup transfer
    Td->Length = (uint16_t)(EHCI_TD_LENGTH(EhciTdFill(Td, 
        Transaction->BufferAddress, sizeof(UsbPacket_t))));

    // Store copies
    Td->OriginalLength = Td->Length;
    Td->OriginalToken = Td->Token;

	// Return the allocated descriptor
	return Td;
}

/* EhciTdIo
 * This allocates & initializes a TD for an i/o transaction 
 * and is used for control, bulk and interrupt */
EhciTransferDescriptor_t*
EhciTdIo(
    _In_ EhciController_t *Controller,
    _In_ UsbTransfer_t *Transfer,
    _In_ UsbTransaction_t *Transaction,
	_In_ int Toggle)
{
	// Variables
    EhciTransferDescriptor_t *Td = NULL;
    unsigned PId = (Transaction->Type == InTransaction) ? EHCI_TD_IN : EHCI_TD_OUT;

	// Allocate a new td
	Td = EhciTdAllocate(Controller);

	// Initialize the new Td
	Td->Link = EHCI_LINK_END;
    Td->AlternativeLink = EHCI_LINK_END;
    Td->AlternativeLinkIndex = EHCI_NO_INDEX;
	Td->Status = EHCI_TD_ACTIVE;
	Td->Token = (uint8_t)(PId & 0x3);
	Td->Token |= EHCI_TD_ERRCOUNT;
    
    // Short packet not ok? 
	if (Transfer->Flags & USB_TRANSFER_SHORT_NOT_OK && PId == EHCI_TD_IN) {
		Td->AlternativeLink = EHCI_POOL_TDINDEX(Controller, EHCI_POOL_TD_ASYNC);
        Td->AlternativeLinkIndex = EHCI_POOL_TD_ASYNC;
    }

	// Calculate the length of the transfer
    Td->Length = (uint16_t)(EHCI_TD_LENGTH(EhciTdFill(Td, 
        Transaction->BufferAddress, Transaction->Length)));

	// Set toggle?
	if (Toggle) {
		Td->Length |= EHCI_TD_TOGGLE;
    }

	// Calculate next toggle 
    // if transaction spans multiple transfers
    // @todo
	if (Transaction->Length > 0
		&& !(DIVUP(Transaction->Length, Transfer->Endpoint.MaxPacketSize) % 2)) {
        Toggle ^= 0x1;
    }

    // Store copies
    Td->OriginalLength = Td->Length;
    Td->OriginalToken = Td->Token;

	// Setup done, return the new descriptor
	return Td;
}

/* EhciGetStatusCode
 * Retrieves a status-code from a given condition code */
UsbTransferStatus_t
EhciGetStatusCode(
    _In_ int ConditionCode)
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

/* EhciRestartQh
 * Restarts an interrupt QH by resetting it to it's start state */
void
EhciRestartQh(
    EhciController_t *Controller, 
    UsbManagerTransfer_t *Transfer)
{
	// Variables
    EhciQueueHead_t *Qh = NULL;
    EhciTransferDescriptor_t *Td = NULL;
    uintptr_t BufferBase, BufferStep;
    
    // Setup some variables
	Qh = (EhciQueueHead_t*)Transfer->EndpointDescriptor;
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];

    // Do some extra processing for periodics
    if (Transfer->Transfer.Type == InterruptTransfer) {
        BufferBase = Transfer->Transfer.Transactions[0].BufferAddress;
        BufferStep = Transfer->Transfer.Transactions[0].Length;
    }

	// Iterate td's
	while (Td) {
		// Update the toggle
		Td->OriginalLength &= ~(EHCI_TD_TOGGLE);
		if (Td->Length & EHCI_TD_TOGGLE) {
			Td->OriginalLength |= EHCI_TD_TOGGLE;
        }

        // Reset members of td
        Td->Status = EHCI_TD_ACTIVE;
        Td->Length = Td->OriginalLength;
        Td->Token = Td->OriginalToken;
        
        // Adjust buffers if interrupt in
        if (Transfer->Transfer.Type == InterruptTransfer) {
            uintptr_t BufferBaseUpdated = ADDLIMIT(BufferBase, Td->Buffers[0], 
                BufferStep, BufferBase + Transfer->Transfer.PeriodicBufferSize);
            EhciTdFill(Td, BufferBaseUpdated, BufferStep);
        }

		// Switch to next transfer descriptor
        if (Td->LinkIndex != EHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
        }
        else {
            Td = NULL;
            break;
        }
	}

	// Set Qh to point to first
	Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];

    // Zero out overlay (BUT KEEP TOGGLE???)
    // @todo
	memset(&Qh->Overlay, 0, sizeof(EhciQueueHeadOverlay_t));

    // Update links
	Qh->Overlay.NextTD = EHCI_POOL_TDINDEX(Controller, Td->Index);
	Qh->Overlay.NextAlternativeTD = EHCI_LINK_END;
}

/* EhciScanQh
 * Scans a QH for completion or error returns non-zero if it has been touched */
int
EhciScanQh(
    EhciController_t *Controller, 
    UsbManagerTransfer_t *Transfer)
{
    // Variables
    EhciQueueHead_t *Qh = NULL;
    EhciTransferDescriptor_t *Td = NULL;
	int ShortTransfer = 0;
	int ErrorTransfer = 0;
	int Counter = 0;
    int ProcessQh = 0;
    
    // Setup some variables
	Qh = (EhciQueueHead_t*)Transfer->EndpointDescriptor;
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    
    // Iterate td's
	while (Td) {
        // Locals
		int CondCode = EhciConditionCodeToIndex(Transfer->Transfer.Speed == HighSpeed ? Td->Status & 0xFC : Td->Status);
		int BytesLeft = Td->Length & 0x7FFF;
		Counter++;

		// Sanitize td activity status
		if (Td->Status & EHCI_TD_ACTIVE) {
			// If it's not the first TD, skip
			if (Counter > 1
				&& (ShortTransfer || ErrorTransfer)) {
                ProcessQh = (ErrorTransfer == 1) ? 2 : 1;
            }
			else {
                // No, only partially done without any errors
				ProcessQh = 0; 
            }
			break;
		}

		// Set for processing per default if we reach here
		ProcessQh = 1;

		// TD is not active
		// this means it's been processed
		if (BytesLeft > 0) {
			ShortTransfer = 1;
		}

		// Error Transfer?
		if (CondCode != 0) {
            ErrorTransfer = 1;
            Transfer->Status = EhciGetStatusCode(CondCode);
            break;
		}

        // Switch to next transfer descriptor
        if (Td->LinkIndex != EHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
        }
        else {
            Td = NULL;
            break;
        }
	}

	// Sanitize the error transfer
	if (ErrorTransfer) {
		ProcessQh = 2;
    }
	return ProcessQh;
}

/* EhciProcessTransfers
 * For transaction progress this involves done/error transfers */
void
EhciProcessTransfers(
	_In_ EhciController_t *Controller)
{
	// Iterate active transfers
	foreach(Node, Controller->QueueControl.TransactionList) {
		// Instantiate a transaction pointer
        UsbManagerTransfer_t *Transfer = 
            (UsbManagerTransfer_t*)Node->Data;
		int Processed = 0;

		// Scan the transfer as long as it's not a isochronous-transfer
		if (Transfer->Transfer.Type != IsochronousTransfer) {
			Processed = EhciScanQh(Controller, Transfer);
        }

		// If it is to be processed, wake or process
		if (Processed) {
			if (Transfer->Transfer.Type == InterruptTransfer)  {
				EhciRestartQh(Controller, Transfer);

				// Notify process of transfer of the status
                if (Transfer->Transfer.UpdatesOn) {
                    InterruptDriver(Transfer->Requester, 
                        (size_t)Transfer->Transfer.PeriodicData, 
                        (size_t)((Processed == 1) ? TransferFinished : Transfer->Status), 
                        Transfer->CurrentDataIndex, 0);
                }

                // Increase
                Transfer->CurrentDataIndex = ADDLIMIT(0, Transfer->CurrentDataIndex,
                    Transfer->Transfer.Transactions[0].Length, Transfer->Transfer.PeriodicBufferSize);
			}
			else {
                ERROR("Should wake-up transferqh");
            }
		}
	}
}

/* EhciProcessDoorBell
 * This makes sure to schedule and/or unschedule transfers */
void
EhciProcessDoorBell(
	_In_ EhciController_t *Controller)
{
    // Variables
	CollectionItem_t *Node = NULL;

Scan:
    // As soon as we enter the scan area we reset the re-scan
    // to allow other threads to set it again
	Controller->QueueControl.BellReScan = 0;

	// Iterate active transfers
	_foreach(Node, Controller->QueueControl.TransactionList) {
		// Instantiate a transaction pointer
        UsbManagerTransfer_t *Transfer = 
            (UsbManagerTransfer_t*)Node->Data;

		// Act based on transfer type
		if (Transfer->Transfer.Type == ControlTransfer
			|| Transfer->Transfer.Type == BulkTransfer) {
            EhciQueueHead_t *Qh = (EhciQueueHead_t*)Transfer->EndpointDescriptor;
            EhciQueueHead_t *PrevQh = NULL;
            unsigned Index = 0;

			// Check for specific event flags
			if (Qh->HcdFlags & EHCI_QH_UNSCHEDULE) {
				Qh->HcdFlags &= ~(EHCI_QH_UNSCHEDULE);
                // Perform an asynchronous memory unlink
                PrevQh = &Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC];
                while (PrevQh->LinkIndex != Qh->Index
                    && PrevQh->LinkIndex != EHCI_NO_INDEX) {
                    PrevQh = &Controller->QueueControl.QHPool[PrevQh->LinkIndex];
                }

                // Now make sure we skip over our qh
                PrevQh->LinkPointer = Qh->LinkPointer;
                PrevQh->LinkIndex = Qh->LinkIndex;
                MemoryBarrier();

                // Reset the qh
                Index = Qh->Index;
                memset(Qh, 0, sizeof(EhciQueueHead_t));
                Qh->Index = Index;
                
                // Stop async scheduler if there aren't anymore 
                // transfers to process
                Controller->QueueControl.AsyncTransactions--;
                if (!Controller->QueueControl.AsyncTransactions) {
                    EhciDisableAsyncScheduler(Controller);
                }
			}
		}
	}

	// If someone has rung the bell while 
	// the door was opened, we should not close the door yet
	if (Controller->QueueControl.BellReScan != 0) {
		goto Scan;
    }

	// Bell is no longer ringing
	Controller->QueueControl.BellIsRinging = 0;
}

/* Re-enable warnings */
#ifdef _MSC_VER
#pragma warning(default:4127)
#endif
