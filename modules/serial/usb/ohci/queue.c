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
 *	- Power Management
 */
//#define __TRACE

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

// Queue Balancing
static const int _Balance[] = { 
	0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 
};

/* OhciQueueInitialize
 * Initialize the controller's queue resources and resets counters */
OsStatus_t
OhciQueueInitialize(
	_In_ OhciController_t *Controller)
{
	// Variables
	OhciTransferDescriptor_t *NullTd = NULL;
	OhciControl_t *Queue = &Controller->QueueControl;
	uintptr_t PoolPhysical = 0, NullPhysical = 0;
	size_t PoolSize;
	void *Pool = NULL;
	int i;

	// Trace
	TRACE("OhciQueueInitialize()");

	// Null out queue-control
	memset(Queue, 0, sizeof(OhciControl_t));

	// Reset indexes
	Queue->TransactionQueueBulkIndex = -1;
	Queue->TransactionQueueControlIndex = -1;

	// Calculate how many bytes of memory we will need
	PoolSize = (OHCI_POOL_EDS + 32) * sizeof(OhciEndpointDescriptor_t);
	PoolSize += OHCI_POOL_TDS * sizeof(OhciTransferDescriptor_t);

	// Allocate both TD's and ED's pool
	if (MemoryAllocate(PoolSize, MEMORY_CLEAN | MEMORY_COMMIT
		| MEMORY_LOWFIRST | MEMORY_CONTIGIOUS, &Pool, &PoolPhysical) != OsSuccess) {
		ERROR("Failed to allocate memory for resource-pool");
		return OsError;
	}

	// Initialize pointers
	Queue->EDPool = (OhciEndpointDescriptor_t**)Pool;
	Queue->EDPoolPhysical = PoolPhysical;
	Queue->TDPool = (OhciTransferDescriptor_t**)((uint8_t*)Pool +
		(OHCI_POOL_EDS + 32) * sizeof(OhciEndpointDescriptor_t));
	Queue->TDPoolPhysical = PoolPhysical + 
		(OHCI_POOL_EDS + 32) * sizeof(OhciEndpointDescriptor_t);

	// Initialize the null-td
	NullTd = Queue->TDPool[OHCI_POOL_TDNULL];
	NullTd->BufferEnd = 0;
	NullTd->Cbp = 0;
	NullTd->Link = 0x0;
	NullTd->Flags = OHCI_TD_ALLOCATED;
	NullPhysical = OHCI_POOL_TDINDEX(Queue->TDPoolPhysical, OHCI_POOL_TDNULL);

	// Enumerate the ED pool and set their links
	// to the NULL descriptor
	for (i = 0; i < (OHCI_POOL_EDS + 32); i++) {
		// Mark it skippable and set a NULL td
		Queue->EDPool[i]->Flags = OHCI_EP_SKIP;
		Queue->EDPool[i]->Current =
			(Queue->EDPool[i]->TailPointer = NullPhysical) | 0x1;
	}

	// Initialize transaction counters
	// and the transaction list
	Queue->TransactionList = ListCreate(KeyInteger, LIST_SAFE);
	return OsSuccess;
}

/* OhciQueueDestroy
 * Unschedules any scheduled ed's and frees all resources allocated
 * by the initialize function */
OsStatus_t
OhciQueueDestroy(
	_In_ OhciController_t *Controller)
{
	return OsError;
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
		OhciEndpointDescriptor_t *Ed = 
			Controller->QueueControl.EDPool[OHCI_POOL_EDS + i];

		// Enumerate links
		while (Ed) {
			// TRACE
			TRACE("0x%x -> ", (EpPtr->Flags & OHCI_EP_SKIP));
			Ed = (OhciEndpointDescriptor_t*)Ed->LinkVirtual;
		}
	}
}

/* OhciEdAllocate
 * Allocates a new ED for usage with the transaction. If this returns
 * NULL we are out of ED's and we should wait till next transfer. */
OhciEndpointDescriptor_t*
OhciEdAllocate(
	_In_ OhciController_t *Controller, 
	_In_ UsbTransferType_t Type)
{
	// Variables
	OhciEndpointDescriptor_t *Ed = NULL;
	int i;

	// Unused for now
	_CRT_UNUSED(Type);

	// Lock access to the queue
	SpinlockAcquire(&Controller->Base.Lock);

	// Now, we usually allocated new endpoints for interrupts
	// and isoc, but it doesn't make sense for us as we keep one
	// large pool of ED's, just allocate from that in any case
	for (i = 0; i < OHCI_POOL_EDS; i++) {
		// Skip in case already allocated
		if (Controller->QueueControl.EDPool[i]->HcdFlags & OHCI_ED_ALLOCATED) {
			continue;
		}

		// We found a free ed - mark it allocated and end
		// but reset the ED first
		memset(Controller->QueueControl.EDPool[i], 0, sizeof(OhciEndpointDescriptor_t));
		Controller->QueueControl.EDPool[i]->HcdFlags = OHCI_ED_ALLOCATED;
		Controller->QueueControl.EDPool[i]->HcdFlags |= OHCI_ED_SET_INDEX(i);
		
		// Store pointer
		Ed = Controller->QueueControl.EDPool[i];
		break;
	}
	

	// Release the lock, let others pass
	SpinlockRelease(&Controller->Base.Lock);
	return Ed;
}

/* OhciTdAllocate
 * Allocates a new TD for usage with the transaction. If this returns
 * NULL we are out of TD's and we should wait till next transfer. */
OhciTransferDescriptor_t*
OhciTdAllocate(
	_In_ OhciController_t *Controller,
	_In_ UsbTransferType_t Type)
{
	// Variables
	OhciTransferDescriptor_t *Td = NULL;
	int i;

	// Unused for now
	_CRT_UNUSED(Type);

	// Lock access to the queue
	SpinlockAcquire(&Controller->Base.Lock);

	// Now, we usually allocated new descriptors for interrupts
	// and isoc, but it doesn't make sense for us as we keep one
	// large pool of ED's, just allocate from that in any case
	for (i = 0; i < OHCI_POOL_TDS; i++) {
		// Skip ahead if allocated, skip twice if isoc
		if (Controller->QueueControl.TDPool[i]->Flags & OHCI_TD_ALLOCATED) {
			if (Controller->QueueControl.TDPool[i]->Flags & OHCI_TD_ISOCHRONOUS) {
				i++;
			}
			continue;
		}

		// If we asked for isoc, make sure secondary is available
		if (Type == IsochronousTransfer) {
			if (Controller->QueueControl.TDPool[i + 1]->Flags & OHCI_TD_ALLOCATED) {
				continue;
			}
			else {
				Controller->QueueControl.TDPool[i]->Flags = OHCI_TD_ISOCHRONOUS;
			}
		}

		// Found one, reset
		Controller->QueueControl.TDPool[i]->Flags |= OHCI_TD_ALLOCATED;
		Controller->QueueControl.TDPool[i]->Index = (int16_t)i;
		Controller->QueueControl.TDPool[i]->LinkIndex = (int16_t)-1;
		Td = Controller->QueueControl.TDPool[i];
		break;
	}

	// Release the lock, let others pass
	SpinlockRelease(&Controller->Base.Lock);
	return Td;
}

/* OhciEdInitialize
 * Initializes and sets up the endpoint descriptor with 
 * the given values */
void
OhciEdInitialize(
	_In_ OhciController_t *Controller,
	_Out_ OhciEndpointDescriptor_t *Ed, 
	_In_ int HeadIndex, 
	_In_ UsbTransferType_t Type,
	_In_ size_t Address, 
	_In_ size_t Endpoint, 
	_In_ size_t PacketSize,
	_In_ UsbSpeed_t Speed)
{
	// Variables
	OhciTransferDescriptor_t *Td = NULL;
	int16_t LastIndex = -1;

	// Update index's
	if (HeadIndex == -1) {
		Ed->Current = OHCI_LINK_END;
		Ed->TailPointer = 0;
	}
	else {
		Td = Controller->QueueControl.TDPool[HeadIndex];

		// Set physical of head
		Ed->Current = OHCI_POOL_TDINDEX(Controller->QueueControl.TDPoolPhysical, HeadIndex) | OHCI_LINK_END;

		// Iterate untill tail
		while (Td->LinkIndex != -1) {
			LastIndex = Td->LinkIndex;
			Td = Controller->QueueControl.TDPool[Td->LinkIndex];
		}

		// Update tail
		Ed->TailPointer = OHCI_POOL_TDINDEX(Controller->QueueControl.TDPoolPhysical, LastIndex);
	}

	// Update head-index
	Ed->HeadIndex = (int16_t)HeadIndex;

	// Initialize flags
	Ed->Flags = OHCI_EP_SKIP;
	Ed->Flags |= (Address & OHCI_EP_ADDRESS_MASK);
	Ed->Flags |= OHCI_EP_ENDPOINT(Endpoint);
	Ed->Flags |= OHCI_EP_INOUT_TD; // Retrieve from TD
	Ed->Flags |= OHCP_EP_LOWSPEED((Speed == LowSpeed) ? 1 : 0);
	Ed->Flags |= OHCI_EP_MAXLEN(PacketSize);
	Ed->Flags |= OHCI_EP_TYPE(Type);

	// If it's isochronous add a special flag to indicate
	// the type of td's used
	if (Type == IsochronousTransfer) {
		Ed->Flags |= OHCI_EP_ISOCHRONOUS;
	}
}

/* OhciTdSetup 
 * Creates a new setup token td and initializes all the members.
 * The Td is immediately ready for execution. */
OhciTransferDescriptor_t*
OhciTdSetup(
	_In_ OhciController_t *Controller, 
	_In_ UsbTransaction_t *Transaction, 
	_In_ UsbTransferType_t Type)
{
	// Variables
	OhciTransferDescriptor_t *Td = NULL;

	// Allocate a new Td
	Td = OhciTdAllocate(Controller, Type);
	if (Td == NULL) {
		return NULL;
	}

	// Set no link
	Td->Link = OHCI_LINK_END;
	Td->LinkIndex = -1;

	// Initialize the Td flags
	Td->Flags = OHCI_TD_ALLOCATED;
	Td->Flags |= OHCI_TD_PID_SETUP;
	Td->Flags |= OHCI_TD_NO_IOC;
	Td->Flags |= OHCI_TD_TOGGLE_LOCAL;
	Td->Flags |= OHCI_TD_ACTIVE;

	// Install the buffer
	Td->Cbp = Transaction->BufferAddress;
	Td->BufferEnd = Td->Cbp + sizeof(UsbPacket_t) - 1;

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
	Td = OhciTdAllocate(Controller, Type);
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
		Td->Flags = 0;
		Td->Flags |= OHCI_TD_FRAMECOUNT(FrameCount - 1);
		Td->Flags |= OHCI_TD_NO_IOC;

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
		Td->Link = OHCI_LINK_END;

		// Setup done
		return Td;
	}

	// Set this is as end of chain
	Td->Link = OHCI_LINK_END;
	Td->LinkIndex = -1;

	// Initialize flags as a IO Td
	Td->Flags = OHCI_TD_ALLOCATED;
	Td->Flags |= PId;
	Td->Flags |= OHCI_TD_NO_IOC;
	Td->Flags |= OHCI_TD_TOGGLE_LOCAL;
	Td->Flags |= OHCI_TD_ACTIVE;

	// We have to allow short-packets in some cases
	// where data returned or send might be shorter
	if (Type == ControlTransfer) {
		if (PId == OHCI_TD_PID_IN && Length > 0) {
			Td->Flags |= OHCI_TD_SHORTPACKET;
		}
	}
	else if (PId == OHCI_TD_PID_IN) {
		Td->Flags |= OHCI_TD_SHORTPACKET;
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
	else {
		Td->Cbp = 0;
		Td->BufferEnd = 0;
	}

	// Setup done
	return Td;
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
	OhciControl_t *Queue = &Controller->QueueControl;
	int	Index = -1;
	size_t i;

	// iso periods can be huge; iso tds specify frame numbers
	if (Interval > OHCI_BANDWIDTH_PHASES) {
		Interval = OHCI_BANDWIDTH_PHASES;
	}

	// Find the least loaded queue
	for (i = 0; i < Interval; i++) {
		if (Index < 0 || Queue->Bandwidth[Index] > Queue->Bandwidth[i]) {
			int	j;

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
	_In_ int EndpointDescriptorIndex)
{
	// Variables
	OhciControl_t *Queue = &Controller->QueueControl;
	OhciEndpointDescriptor_t *Ep = Queue->EDPool[EndpointDescriptorIndex];
	uintptr_t EpAddress = 0;

	// Lookup physical
	EpAddress = OHCI_POOL_EDINDEX(Queue->EDPoolPhysical, EndpointDescriptorIndex);

	// Switch based on type of transfer
	if (Type == ControlTransfer) {
		if (Queue->TransactionsWaitingControl > 0) {
			// Insert into front if 0
			if (Queue->TransactionQueueControlIndex == -1) {
				Queue->TransactionQueueControlIndex = EndpointDescriptorIndex;
			}
			else {
				// Iterate to end of descriptor-chain
				OhciEndpointDescriptor_t *EpItr = 
					Queue->EDPool[Queue->TransactionQueueControlIndex];

				// Iterate until end of chain
				while (EpItr->Link) {
					EpItr = (OhciEndpointDescriptor_t*)EpItr->LinkVirtual;
				}

				// Insert it
				EpItr->Link = EpAddress;
				EpItr->LinkVirtual = (reg32_t)Ep;
			}

			// Increase number of transactions waiting
			Queue->TransactionsWaitingControl++;
		}
		else {
			// Add it HcControl/BulkCurrentED
			Controller->Registers->HcControlHeadED =
				Controller->Registers->HcControlCurrentED = EpAddress;
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
			if (Queue->TransactionQueueBulkIndex == -1) {
				Queue->TransactionQueueBulkIndex = EndpointDescriptorIndex;
			}
			else {
				// Iterate to end of descriptor-chain
				OhciEndpointDescriptor_t *EpItr = 
					Queue->EDPool[Queue->TransactionQueueBulkIndex];

				// Iterate until end of chain
				while (EpItr->Link) {
					EpItr = (OhciEndpointDescriptor_t*)EpItr->LinkVirtual;
				}

				// Insert it
				EpItr->Link = EpAddress;
				EpItr->LinkVirtual = (reg32_t)Ep;
			}

			// Increase waiting count
			Queue->TransactionsWaitingBulk++;
		}
		else {
			// Add it HcControl/BulkCurrentED
			Controller->Registers->HcBulkHeadED =
				Controller->Registers->HcBulkCurrentED = EpAddress;
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
	_In_ int EndpointDescriptorIndex)
{
	// Variables
	OhciEndpointDescriptor_t *Ep = 
		Controller->QueueControl.EDPool[EndpointDescriptorIndex];
	uintptr_t EpAddress = 0;
	int Queue = 0;
	int i;

	// Lookup physical
	EpAddress = OHCI_POOL_EDINDEX(Controller->QueueControl.EDPoolPhysical, EndpointDescriptorIndex);

	// Calculate a queue
	Queue = OhciCalculateQueue(Controller, 
		Ep->Interval, Ep->Bandwidth);

	// Sanitize that it's valid queue
	if (Queue < 0) {
		return OsError;
	}

	// Now loop through the bandwidth-phases and link it
	for (i = Queue; i < OHCI_BANDWIDTH_PHASES; i += (int)Ep->Interval) {
		OhciEndpointDescriptor_t **PrevEd = &Controller->QueueControl.EDPool[OHCI_POOL_EDS + i];
		OhciEndpointDescriptor_t *Here = *PrevEd;
		uint32_t *PrevPtr = (uint32_t*)&Controller->Hcca->InterruptTable[i];

		// Sorting each branch by period (slow before fast)
		// lets us share the faster parts of the tree.
		// (plus maybe: put interrupt eds before iso)
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
	Ep->HcdFlags |= OHCI_ED_SET_QUEUE(Queue);

	// Done
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
		Controller->QueueControl.EDPool[EndpointDescriptorIndex];
	int Queue = OHCI_ED_GET_QUEUE(Ep->HcdFlags);
	int i;

	// Iterate the bandwidth phases
	for (i = Queue; i < OHCI_BANDWIDTH_PHASES; i += (int)Ep->Interval) {
		OhciEndpointDescriptor_t *Temp = NULL;
		OhciEndpointDescriptor_t **PrevEd = 
			&Controller->QueueControl.EDPool[OHCI_POOL_EDS + i];
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
			uintptr_t EpPhysical = OHCI_POOL_EDINDEX(Controller->QueueControl.EDPoolPhysical, 
				Controller->QueueControl.TransactionQueueControlIndex);
			
			// Update new start and kick off queue
			Controller->Registers->HcControlHeadED =
				Controller->Registers->HcControlCurrentED = EpPhysical;
			Controller->Registers->HcCommandStatus |= OHCI_COMMAND_CONTROL_ACTIVE;
		}

		// Reset control queue
		Controller->QueueControl.TransactionQueueControlIndex = -1;
		Controller->QueueControl.TransactionsWaitingControl = 0;
	}
	else if (TransferType == BulkTransfer) {
		if (Controller->QueueControl.TransactionsWaitingBulk > 0) {
			// Retrieve the physical address
			uintptr_t EpPhysical = OHCI_POOL_EDINDEX(Controller->QueueControl.EDPoolPhysical, 
				Controller->QueueControl.TransactionQueueBulkIndex);

			// Update new start and kick off queue
			Controller->Registers->HcBulkHeadED =
				Controller->Registers->HcBulkCurrentED = EpPhysical;
			Controller->Registers->HcCommandStatus |= OHCI_COMMAND_BULK_ACTIVE;
		}

		// Reset bulk queue
		Controller->QueueControl.TransactionQueueBulkIndex = -1;
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
		Td = Controller->QueueControl.TDPool[EndpointDescriptor->HeadIndex];
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
					int SwitchToggles = Transfer->TransactionCount % 2;
					int ErrorTransfer = 0;

					// Re-iterate all td's
					Td2 = Controller->QueueControl.TDPool[EndpointDescriptor->HeadIndex];
					while (Td2) {
						// Extract error code
						int ErrorCode = OHCI_TD_GET_CC(Td2->Flags);
						
						// Sanitize the error code
						if ((ErrorCode != 0 && ErrorCode != 15)
							|| ErrorTransfer) {
							ErrorTransfer = 1;
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

							// Restart Td
							Td2->Flags = Td2->OriginalFlags;
							Td2->Cbp = Td2->OriginalCbp;
						}

						// Notify process of transfer of the status
						if (Transfer->Transfer.UpdatesOn) {
							InterruptDriver(Transfer->Requester, 
								(void*)Transfer->Transfer.PeriodicData);
						}

						// Restart endpoint
						if (!ErrorTransfer) {
							EndpointDescriptor->Current = 
								OHCI_POOL_TDINDEX(Controller->QueueControl.TDPoolPhysical, 
									EndpointDescriptor->HeadIndex); 
						}

						// Go to next td or terminate
						if (Td2->LinkIndex != -1) {
							Td2 = Controller->QueueControl.TDPool[Td2->LinkIndex];
						}
						else {
							break;
						}
					}
				}
			}

			// Go to next td or terminate
			if (Td->LinkIndex != -1) {
				Td = Controller->QueueControl.TDPool[Td->LinkIndex];
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
		OhciEndpointDescriptor_t *EndpointDescriptor = 
			(OhciEndpointDescriptor_t*)Transfer->EndpointDescriptor;

		// Extract index
		int Index = OHCI_ED_GET_INDEX(EndpointDescriptor->HcdFlags);

		// Check flags for any requests, either schedule or unschedule
		if (EndpointDescriptor->HcdFlags & OHCI_ED_SCHEDULE) {
			if (Transfer->Transfer.Type == ControlTransfer
				|| Transfer->Transfer.Type == BulkTransfer) {
				OhciLinkGeneric(Controller, Transfer->Transfer.Type, Index);
			}
			else {
				// Link it in, and activate list
				OhciLinkPeriodic(Controller, Index);
				Controller->Registers->HcControl |= OHCI_CONTROL_PERIODIC_ACTIVE | OHCI_CONTROL_ISOC_ACTIVE;
			}

			// Remove the schedule flag
			EndpointDescriptor->HcdFlags &= ~OHCI_ED_SCHEDULE;
		}
		else if (EndpointDescriptor->HcdFlags & OHCI_ED_UNSCHEDULE) {
			// Only interrupt and isoc requests unscheduling
			// And remove the unschedule flag
			OhciUnlinkPeriodic(Controller, Index);
			EndpointDescriptor->HcdFlags &= ~OHCI_ED_UNSCHEDULE;
		}
	}
}
