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
	SpinlockAcquire(&Controller->Lock);

	// Now, we usually allocated new endpoints for interrupts
	// and isoc, but it doesn't make sense for us as we keep one
	// large pool of ED's, just allocate from that in any case
	for (i = 0; i < OHCI_POOL_NUM_ED; i++) {
		// Skip in case already allocated
		if (Controller.QueueControl->EDPool[i]->HcdFlags & OHCI_ED_ALLOCATED) {
			continue;
		}

		// We found a free ed - mark it allocated and end
		// but reset the ED first
		memset(Controller.QueueControl->EDPool[i], 0, sizeof(OhciEndpointDescriptor_t));
		Controller.QueueControl->EDPool[i]->HcdFlags = OHCI_ED_ALLOCATED;
		
		// Store pointer
		Ed = Controller.QueueControl->EDPool[i];
		break;
	}
	

	// Release the lock, let others pass
	SpinlockRelease(&Controller->Lock);
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
	SpinlockAcquire(&Controller->Lock);

	// Now, we usually allocated new descriptors for interrupts
	// and isoc, but it doesn't make sense for us as we keep one
	// large pool of ED's, just allocate from that in any case
	for (i = 0; i < OHCI_POOL_TDS; i++) {
		// Skip ahead if allocated, skip twice if isoc
		if (Controller.QueueControl->TDPool[i]->Flags & OHCI_TD_ALLOCATED) {
			if (Controller.QueueControl->TDPool[i]->Flags & OHCI_TD_ISOCHRONOUS) {
				i++;
			}
			continue;
		}

		// If we asked for isoc, make sure secondary is available
		if (Type == IsochronousTransfer) {
			if (Controller.QueueControl->TDPool[i + 1]->Flags & OHCI_TD_ALLOCATED) {
				continue;
			}
			else {
				Controller.QueueControl->TDPool[i]->Flags = OHCI_TD_ISOCHRONOUS;
			}
		}

		// Found one, reset
		Controller.QueueControl->TDPool[i]->Flags |= OHCI_TD_ALLOCATED;
		Td = Controller.QueueControl->TDPool[i];
		break;
	}

	// Release the lock, let others pass
	SpinlockRelease(&Controller->Lock);
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
		Td = Controller.QueueControl->TDPool[HeadIndex];

		// Set physical of head
		Ed->HeadPtr = OHCI_POOL_TDINDEX(Controller.QueueControl->TDPoolPhysical, HeadIndex) | OHCI_LINK_END;

		// Iterate untill tail
		while (Td->LinkIndex != -1) {
			LastIndex = Td->LinkIndex;
			Td = Controller.QueueControl->TDPool[Td->LinkIndex];
		}

		// Update tail
		Ed->TailPtr = OHCI_POOL_TDINDEX(Controller.QueueControl->TDPoolPhysical, LastIndex);
	}

	// Initialize flags
	Ed->Flags = OHCI_EP_SKIP;
	Ed->Flags |= (Address & OHCI_EP_ADDRESS_MASK);
	Ed->Flags |= OHCI_EP_ENDPOINT(Endpoint);
	Ed->Flags |= OHCI_EP_INOUT_TD; /* Get PID from Td */
	Ed->Flags |= OHCP_EP_LOWSPEED((Speed == LowSpeed) ? 1 : 0);
	Ed->Flags |= OHCI_EP_MAXLEN(PacketSize);
	Ed->Flags |= OHCI_EP_TYPE(Type);

	// If it's isochronous add a special flag to indicate
	// the type of td's used
	if (Type == IsochronousTransfer) {
		Ed->Flags |= OHCI_EP_ISOCHRONOUS;
	}
}

/* Setup TD */
OhciTransferDescriptor_t *OhciTdSetup(OhciEndpoint_t *Ep, UsbTransferType_t Type,
	UsbPacket_t *pPacket, void **TDBuffer)
{
	/* Vars */
	OhciGTransferDescriptor_t *Td;
	Addr_t TDIndex;
	void *Buffer;

	/* Allocate a Td */
	TDIndex = OhciAllocateTd(Ep, Type);

	/* Grab a Td and a Buffer */
	Td = Ep->TDPool[TDIndex];
	Buffer = Ep->TDPoolBuffers[TDIndex];

	/* Set no link */
	Td->NextTD = OHCI_LINK_END;

	/* Setup the Td for a SETUP Td */
	Td->Flags = OHCI_TD_ALLOCATED;
	Td->Flags |= OHCI_TD_PID_SETUP;
	Td->Flags |= OHCI_TD_NO_IOC;
	Td->Flags |= OHCI_TD_TOGGLE_LOCAL;
	Td->Flags |= OHCI_TD_ACTIVE;

	/* Setup the SETUP Request */
	*TDBuffer = Buffer;
	memcpy(Buffer, (void*)pPacket, sizeof(UsbPacket_t));

	/* Set Td Buffer */
	Td->Cbp = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Buffer);
	Td->BufferEnd = Td->Cbp + sizeof(UsbPacket_t) - 1;

	return Td;
}

OhciTransferDescriptor_t *OhciTdIo(OhciEndpoint_t *OhciEp, UsbTransferType_t Type,
	UsbHcEndpoint_t *Endpoint, uint32_t PId, size_t Length, void **TDBuffer)
{
	/* Vars */
	OhciGTransferDescriptor_t *Td = NULL;
	OhciITransferDescriptor_t *iTd = NULL;
	Addr_t TDIndex;
	void *Buffer;

	/* Allocate a Td */
	TDIndex = OhciAllocateTd(OhciEp, Type);

	/* Sanity */
	if (Type == ControlTransfer || Type == BulkTransfer) {
		Td = OhciEp->TDPool[TDIndex];
		Buffer = OhciEp->TDPoolBuffers[TDIndex];
	}
	else if (Type == InterruptTransfer) {
		Td = (OhciGTransferDescriptor_t*)TDIndex;
		Buffer = (void*)kmalloc_a(PAGE_SIZE);
	}
	else
	{
		/* Calculate frame count - Maximum packet size is 1023 bytes */
		uint32_t FrameCount = DIVUP(Length, 1023);
		uint32_t BufItr = 0;
		uint32_t FrameItr = 0;
		uint32_t Crossed = 0;

		/* If direction is out and mod 1023 is 0
		* add a zero-length frame */

		/* Cast */
		iTd = (OhciITransferDescriptor_t*)TDIndex;

		/* Allocate a buffer */
		Buffer = (void*)kmalloc_a(Length);

		/* IF framecount is > 8, nono */
		if (FrameCount > 8)
			FrameCount = 8;

		/* Setup */
		iTd->Flags = 0;
		iTd->Flags |= OHCI_TD_FRAMECOUNT(FrameCount - 1);
		iTd->Flags |= OHCI_TD_NO_IOC;

		/* Buffer */
		iTd->Bp0 = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Buffer);
		iTd->BufferEnd = iTd->Bp0 + Length - 1;

		/* Setup offsets */
		while (FrameCount)
		{
			/* Set offset 0 */
			iTd->Offsets[FrameItr] = (BufItr & 0xFFF);
			iTd->Offsets[FrameItr] = ((Crossed & 0x1) << 12);

			/* Increase buffer */
			BufItr += 1023;

			/* Sanity */
			if (BufItr >= PAGE_SIZE)
			{
				/* Reduce, set crossed */
				BufItr -= PAGE_SIZE;
				Crossed = 1;
			}

			/* Set iterators */
			FrameItr++;
			FrameCount--;
		}

		/* EOL */
		iTd->NextTD = OHCI_LINK_END;

		/* Done */
		return (OhciGTransferDescriptor_t*)iTd;
	}

	/* EOL */
	Td->NextTD = OHCI_LINK_END;

	/* Setup the Td for a IO Td */
	Td->Flags = OHCI_TD_ALLOCATED;
	Td->Flags |= PId;
	Td->Flags |= OHCI_TD_NO_IOC;
	Td->Flags |= OHCI_TD_TOGGLE_LOCAL;
	Td->Flags |= OHCI_TD_ACTIVE;

	/* Allow short packet? */
	if (Type == ControlTransfer) {
		if (PId == OHCI_TD_PID_IN && Length > 0)
			Td->Flags |= OHCI_TD_SHORTPACKET;
	}
	else if (PId == OHCI_TD_PID_IN)
		Td->Flags |= OHCI_TD_SHORTPACKET;

	/* Toggle? */
	if (Endpoint->Toggle)
		Td->Flags |= OHCI_TD_TOGGLE;

	/* Store buffer */
	*TDBuffer = Buffer;

	/* Bytes to transfer?? */
	if (Length > 0) {
		Td->Cbp = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Buffer);
		Td->BufferEnd = Td->Cbp + Length - 1;
	}
	else {
		Td->Cbp = 0;
		Td->BufferEnd = 0;
	}

	/* Done! */
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
	_In_ int EndpointDescriptorIndex)
{
	// Variables
	OhciControl_t *Queue = &Controller->QueueControl;
	OhciEndpointDescriptor_t *Ep = Queue->EDPool[EndpointDescriptorIndex];
	uinptr_t EpAddress = 0;

	// Lookup physical
	EpAddress = OHCI_POOL_EDINDEX(Queue->EDPoolPhysical, EndpointDescriptorIndex);

	// Switch based on type of transfer
	if (Request->Type == ControlTransfer) {
		if (Queue->TransactionsWaitingControl > 0) {
			// Insert into front if 0
			if (Queue->TransactionQueueControlIndex == -1)
				Queue->TransactionQueueControlIndex = EndpointDescriptorIndex;
			else
			{
				// Iterate to end of descriptor-chain
				OhciEndpointDescriptor_t *EpItr = 
					(OhciEndpointDescriptor_t*)Queue->TransactionQueueControl;

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
		else
		{
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
	else if (Request->Type == BulkTransfer) {
		if (Queue->TransactionsWaitingBulk > 0) {
			// Insert into front if 0
			if (Queue->TransactionQueueBulkIndex == -1) {
				Queue->TransactionQueueBulkIndex = EndpointDescriptorIndex;
			}
			else {
				// Iterate to end of descriptor-chain
				OhciEndpointDescriptor_t *EpItr = 
					(OhciEndpointDescriptor_t*)Queue->TransactionQueueBulk;

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
		Queue->EDPool[EndpointDescriptorIndex];
	Addr_t EpAddress = 0;
	int Queue = 0;
	int i;

	// Lookup physical
	EpAddress = OHCI_POOL_EDINDEX(Queue->EDPoolPhysical, EndpointDescriptorIndex);

	// Calculate a queue
	Queue = OhciCalculateQueue(Controller, Ep->Interval, Ep->Bandwidth);

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
			
			// Get next
			PrevEd = &((OhciEndpointDescriptor_t*)Here->NextEDVirtual);
			PrevPtr = &Here->NextED;
			Here = *PrevEd;
		}

		// Sanitize the found
		if (Ep != Here) {
			Ep->NextEDVirtual = (uint32_t)Here;
			if (Here) {
				Ep->NextED = *PrevPtr;
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

/* Unlinking Functions */
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
			PrevPtr = &Temp->NextED;
			PrevEd = &((OhciEndpointDescriptor_t*)Temp->NextEDVirtual);
		}

		// Make sure we actually found it
		if (*PrevEd) {
			*PrevPtr = Ep->NextED;
			*PrevEd = (OhciEndpointDescriptor_t*)Ep->NextEDVirtual;
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
		if (Controller->TransactionsWaitingControl > 0) {
			// Retrieve the physical address
			uintptr_t EpPhysical = OHCI_POOL_EDINDEX(Queue->EDPoolPhysical, 
				Controller->TransactionQueueControlIndex);
			
			// Update new start and kick off queue
			Controller->Registers->HcControlHeadED =
				Controller->Registers->HcControlCurrentED = EpPhysical;
			Controller->Registers->HcCommandStatus |= OHCI_COMMAND_CONTROL_ACTIVE;
		}

		// Reset control queue
		Controller->TransactionQueueControlIndex = -1;
		Controller->TransactionsWaitingControl = 0;
	}
	else if (TransferType == BulkTransfer) {
		if (Controller->TransactionsWaitingBulk > 0) {
			// Retrieve the physical address
			uintptr_t EpPhysical = OHCI_POOL_EDINDEX(Queue->EDPoolPhysical, 
				Controller->TransactionQueueBulkIndex);

			// Update new start and kick off queue
			Controller->Registers->HcBulkHeadED =
				Controller->Registers->HcBulkCurrentED = EpPhysical;
			Controller->Registers->HcCommandStatus |= OHCI_COMMAND_BULK_ACTIVE;
		}

		// Reset bulk queue
		Controller->TransactionQueueBulkIndex = -1;
		Controller->TransactionsWaitingBulk = 0;
	}
}

/* Process Done Queue */
void OhciProcessDoneQueue(OhciController_t *Controller, Addr_t DoneHeadAddr)
{
	/* Get transaction list */
	List_t *Transactions = (List_t*)Controller->TransactionList;

	/* Get Ed with the same td address as DoneHeadAddr */
	foreach(Node, Transactions)
	{
		/* Cast UsbRequest */
		UsbHcRequest_t *HcRequest = (UsbHcRequest_t*)Node->Data;

		/* Get Ed */
		OhciEndpointDescriptor_t *Ed = (OhciEndpointDescriptor_t*)HcRequest->Data;
		UsbTransferType_t TransferType = (UsbTransferType_t)((Ed->Flags >> 27) & 0xF);

		/* Get transaction list */
		UsbHcTransaction_t *tList = (UsbHcTransaction_t*)HcRequest->Transactions;

		/* Is it this? */
		while (tList)
		{
			/* Get physical of TD */
			Addr_t TdPhysical = AddressSpaceGetMap(AddressSpaceGetCurrent(), 
				(VirtAddr_t)tList->TransferDescriptor);

			/* Is it this one? */
			if (DoneHeadAddr == TdPhysical)
			{
				/* Depending on type */
				if (TransferType == ControlTransfer
					|| TransferType == BulkTransfer)
				{
					/* Reload */
					OhciReloadControlBulk(Controller, TransferType);

					/* Wake a node */
					SchedulerWakeupOneThread((Addr_t*)Ed);

					/* Remove from list */
					ListRemoveByNode(Transactions, Node);

					/* Cleanup node */
					kfree(Node);
				}
				else if (TransferType == InterruptTransfer
					|| TransferType == IsochronousTransfer)
				{
					/* Re-Iterate */
					UsbHcTransaction_t *lIterator = HcRequest->Transactions;
					int SwitchToggles = HcRequest->TransactionCount % 2;
					int ErrorTransfer = 0;

					/* Copy data if not dummy */
					while (lIterator)
					{
						/* Get Td */
						OhciGTransferDescriptor_t *Td =
							(OhciGTransferDescriptor_t*)lIterator->TransferDescriptor;
						
						/* Get condition-code */
						int ConditionCode = OHCI_TD_GET_CC(Td->Flags);

						/* Sanity */
						if ((ConditionCode != 0 
								&& ConditionCode != 15)
							|| ErrorTransfer) {
							ErrorTransfer = 1;
						}
						else
						{
							/* Let's see 
							 * Only copy data */
							if (lIterator->Length != 0
								&& Td->Flags & OHCI_TD_PID_IN)
								memcpy(lIterator->Buffer, lIterator->TransferBuffer, lIterator->Length);

							/* Switch toggle */
							if (TransferType == InterruptTransfer
								&& SwitchToggles)
							{
								OhciGTransferDescriptor_t *__Td =
									(OhciGTransferDescriptor_t*)lIterator->TransferDescriptorCopy;

								/* Clear Toggle */
								__Td->Flags &= ~OHCI_TD_TOGGLE;

								/* Set it? */
								if (HcRequest->Endpoint->Toggle)
									__Td->Flags |= OHCI_TD_TOGGLE;

								/* Switch toggle bit */
								HcRequest->Endpoint->Toggle =
									(HcRequest->Endpoint->Toggle == 1) ? 0 : 1;
							}

							/* Restart Td */
							memcpy(lIterator->TransferDescriptor,
								lIterator->TransferDescriptorCopy,
								TransferType == InterruptTransfer ?
								sizeof(OhciGTransferDescriptor_t) : sizeof(OhciITransferDescriptor_t));
						}

						/* Eh, next link */
						lIterator = lIterator->Link;
					}

					/* Callback */
					if (HcRequest->Callback != NULL)
						HcRequest->Callback->Callback(HcRequest->Callback->Args, 
							ErrorTransfer == 1 ? TransferStalled : TransferFinished);

					/* Restore data for 
					 * out transfers */
					lIterator = HcRequest->Transactions;
					while (lIterator)
					{
						/* Get Td */
						OhciGTransferDescriptor_t *Td =
							(OhciGTransferDescriptor_t*)lIterator->TransferDescriptor;

						/* Let's see
						* Only copy data */
						if (lIterator->Length != 0
							&& Td->Flags & OHCI_TD_PID_OUT)
							memcpy(lIterator->TransferBuffer, lIterator->Buffer, lIterator->Length);

						/* Eh, next link */
						lIterator = lIterator->Link;
					}

					/* Restart Ed */
					if (!ErrorTransfer)
						Ed->HeadPtr = 
							AddressSpaceGetMap(AddressSpaceGetCurrent(), 
								(VirtAddr_t)HcRequest->Transactions->TransferDescriptor);
				}

				/* Done */
				return;
			}

			/* Go to next */
			tList = tList->Link;
		}
	}
}

/* Process Transactions 
 * This code unlinks / links pending endpoint descriptors */
void OhciProcessTransactions(OhciController_t *Controller)
{
	/* Get transaction list */
	List_t *Transactions = (List_t*)Controller->TransactionList;

	/* Iterate list */
	foreach(Node, Transactions)
	{
		/* Cast UsbRequest */
		UsbHcRequest_t *HcRequest = (UsbHcRequest_t*)Node->Data;

		/* Get Ed */
		OhciEndpointDescriptor_t *Ed = (OhciEndpointDescriptor_t*)HcRequest->Data;

		/* Has this Ed requested linkage? */
		if (Ed->HcdFlags & OHCI_ED_SCHEDULE)
		{
			/* What kind of scheduling is requested? */
			if (HcRequest->Type == ControlTransfer
				|| HcRequest->Type == BulkTransfer) {
				/* Link */
				OhciLinkGeneric(Controller, HcRequest);
			}
			else {
				/* Link */
				OhciLinkPeriodic(Controller, HcRequest);

				/* Make sure periodic list is active */
				Controller->Registers->HcControl |= OHCI_CONTROL_PERIODIC_ACTIVE | OHCI_CONTROL_ISOC_ACTIVE;
			}

			/* Remove scheduling flag */
			Ed->HcdFlags &= ~OHCI_ED_SCHEDULE;
		}
		else if (Ed->HcdFlags & OHCI_ED_UNSCHEDULE)
		{
			/* Only interrupt and isoc requests unscheduling */
			OhciUnlinkPeriodic(Controller, HcRequest);

			/* Remove unscheduling flag */
			Ed->HcdFlags &= ~OHCI_ED_UNSCHEDULE;

			/* Wake up process if anyone was waiting for us to unlink */
			SchedulerWakeupOneThread((Addr_t*)HcRequest->Data);
		}
	}
}
