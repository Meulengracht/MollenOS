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
 * Finish the FSBR implementation, right now there is no guarantee of order ls/fs/bul
 * The isochronous unlink/link needs improvements, it does not support multiple isocs in same frame 
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

/* UhciQueueInitialize
 * Initialize the controller's queue resources and resets counters */
OsStatus_t
UhciQueueInitialize(
    _In_ UhciController_t *Controller)
{
	// Variables
	UhciTransferDescriptor_t *NullTd = NULL;
	UhciControl_t *Queue = &Controller->QueueControl;
	uintptr_t PoolPhysical = 0, NullTdPhysical = 0, NullQhPhysical = 0;
	reg32_t *FrameList = NULL;
	size_t PoolSize;
	void *Pool = NULL;
	int i;

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
	FrameList = (reg32_t*)Pool;
	Queue->FrameList = (uintptr_t*)Pool;
	Queue->FrameListPhysical = PoolPhysical;

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

	// Set all queues to end in the async QH 
	// This way we handle iso & ints before bulk/control
	for (i = UHCI_QH_ISOCHRONOUS + 1; i < UHCI_QH_ASYNC; i++) {
		Queue->QHPool[i].Link = 
			UHCI_POOL_QHINDEX(Controller, UHCI_QH_ASYNC) | UHCI_LINK_QH;
		Queue->QHPool[i].LinkIndex = UHCI_QH_ASYNC;
	}

	// 1024 Entries
	// Set all entries to the 8 interrupt queues, and we
	// want them interleaved such that some queues get visited more than others
	for (i = 0; i < UHCI_NUM_FRAMES; i++) {
		FrameList[i] = UhciDetermineInterruptQh(Controller, (size_t)i);
	}

	// Initialize the transaction list
	Queue->TransactionList = ListCreate(KeyInteger, LIST_SAFE);

	// Initialize the usb scheduler
	Controller->Scheduler = UsbSchedulerInitialize(UHCI_NUM_FRAMES, 900, 1);

	// Done
	return OsSuccess;
}

/* UhciQueueDestroy
 * Cleans up any resources allocated by QueueInitialize */
OsStatus_t
UhciQueueDestroy(
	_In_ UhciController_t *Controller)
{
	return OsError;
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
	Delta = (FrameNo - Controller->QueueControl.Frame) & (UHCI_FRAME_MASK - 1);
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
	UsbTransferStatus_t ReturnStatus = TransferFinished;
	int ConditionIndex = 0;

	// Get condition index from flags
	ConditionIndex = UhciConditionCodeToIndex(UHCI_TD_STATUS(Td->Flags));

	// We love if-else-if-else-if
	if (ConditionIndex == 6)
		ReturnStatus = TransferStalled;
	else if (ConditionIndex == 1)
		ReturnStatus = TransferInvalidToggles;
	else if (ConditionIndex == 2)
		ReturnStatus = TransferNotResponding;
	else if (ConditionIndex == 3)
		ReturnStatus = TransferNAK;
	else if (ConditionIndex == 4)
		ReturnStatus = TransferBabble;
	else if (ConditionIndex == 5)
		ReturnStatus = TransferInvalidData;

	// Done
	return ReturnStatus;
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
		Controller->QueueControl.QHPool[i].Flags = UHCI_QH_ACTIVE | UHCI_QH_INDEX(i)
				| UHCI_QH_TYPE((uint32_t)Type) | UHCI_QH_BANDWIDTH_ALLOC;
		
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
	// Variables
	UhciTransferDescriptor_t *FirstTd = NULL;

	// Initialize link
	Qh->Link = UHCI_LINK_END;
	Qh->LinkIndex = UHCI_NO_INDEX;

	// Initialize children
	if (HeadIndex != -1) {
		FirstTd = &Controller->QueueControl.TDPool[HeadIndex];
		Qh->Child = (reg32_t)FirstTd;
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
	UhciTransferDescriptor_t *Td = NULL;
	UhciQueueHead_t *Qh = NULL;
	int Toggle = 0;

	// Get qh from transfer
	Qh = (UhciQueueHead_t*)Transfer->EndpointDescriptor;

	// Now we iterate untill it went wrong, storing
	// the toggle-state, and when we reach the wrong
	// we flip the toggle and set it to EP
	Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
	while (Td) {
		if (Td->Header & UHCI_TD_DATA_TOGGLE) {
			Toggle = 1;
		}
		else {
			Toggle = 0;
		}

		// If the td is unprocessed we breakout
		if (Td->Flags & UHCI_TD_ACTIVE) {
			break;
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
	UsbManagerSetToggle(Transfer->Device, Transfer->Pipe, Toggle);
}

/* UhciProcessRequest
 * Processes a QH */
void
UhciProcessRequest(
	_In_ UhciController_t *Controller,
	_In_ UsbManagerTransfer_t *Transfer,
	_In_ ListNode_t *Node,
	_In_ int FixupToggles,
	_In_ int ErrorTransfer)
{
	// Handle the transfer
	if (Transfer->Transfer.Type == ControlTransfer
		|| Transfer->Transfer.Type == BulkTransfer) {

		// If neccessary fixup toggles
		if (Transfer->Transfer.Type == BulkTransfer && FixupToggles) {
			UhciFixupToggles(Controller, Transfer);
		}

		// Notice finalizer-thread to finish us
		// @todo
	}
	else if (Transfer->Transfer.Type == InterruptTransfer) {
		/* Re-Iterate */
		UhciQueueHead_t *Qh = (UhciQueueHead_t*)Request->Data;
		UsbHcTransaction_t *lIterator = Request->Transactions;
		UhciTransferDescriptor_t *Td = (UhciTransferDescriptor_t*)lIterator->TransferDescriptor;
		int SwitchToggles = Request->TransactionCount % 2;

		/* Sanity - Don't reload on error */
		if (ErrorTransfer) {
			/* Callback - Inform the error */
			if (Request->Callback != NULL)
				Request->Callback->Callback(Request->Callback->Args, TransferStalled);

			/* Done, don't reload */
			return;
		}

		/* Fixup Toggles? */
		if (FixupToggles)
			UhciFixupToggles(Request);

		/* Copy data if not dummy */
		while (lIterator)
		{
			/* Get */
			UhciTransferDescriptor_t *ppTd = 
				(UhciTransferDescriptor_t*)lIterator->TransferDescriptor;

			/* Copy Data from transfer buffer to IoBuffer 
			 * Only on in-transfers though */
			if (lIterator->Length != 0
				&& (ppTd->Header & UHCI_TD_PID_IN))
				memcpy(lIterator->Buffer, lIterator->TransferBuffer, lIterator->Length);

			/* Switch toggle if not dividable by 2 */
			if (SwitchToggles
				|| FixupToggles)
			{
				/* Cast TD */
				UhciTransferDescriptor_t *__Td =
					(UhciTransferDescriptor_t*)lIterator->TransferDescriptorCopy;

				/* Clear toggle */
				__Td->Header &= ~UHCI_TD_DATA_TOGGLE;

				/* If set */
				if (Request->Endpoint->Toggle)
					__Td->Header |= UHCI_TD_DATA_TOGGLE;

				/* Toggle */
				Request->Endpoint->Toggle = (Request->Endpoint->Toggle == 0) ? 1 : 0;
			}

			/* Restart Td */
			memcpy(lIterator->TransferDescriptor,
				lIterator->TransferDescriptorCopy, sizeof(UhciTransferDescriptor_t));

			/* Restore IOC */
			if (lIterator->Link == NULL)
				((UhciTransferDescriptor_t*)
				lIterator->TransferDescriptor)->Flags |= UHCI_TD_IOC;

			/* Eh, next link */
			lIterator = lIterator->Link;
		}

		/* Callback */
		if (Request->Callback != NULL)
			Request->Callback->Callback(Request->Callback->Args, TransferFinished);

		/* Renew data if out transfer */
		lIterator = Request->Transactions;
		while (lIterator)
		{
			/* Get */
			UhciTransferDescriptor_t *ppTd =
				(UhciTransferDescriptor_t*)lIterator->TransferDescriptor;

			/* Copy Data from IoBuffer to transfer buffer */
			if (lIterator->Length != 0
				&& (ppTd->Header & UHCI_TD_PID_OUT))
				memcpy(lIterator->TransferBuffer, lIterator->Buffer, lIterator->Length);

			/* Eh, next link */
			lIterator = lIterator->Link;
		}

		/* Reinitilize Qh */
		Qh->Child = Td->PhysicalAddr;
	}
	else
	{
		/* Re-Iterate */
		UhciQueueHead_t *Qh = (UhciQueueHead_t*)Request->Data;
		UsbHcTransaction_t *lIterator = Request->Transactions;

		/* What to do on error transfer */
		if (ErrorTransfer) 
		{
			/* Unschedule */
			UhciUnlinkIsochronousRequest(Controller, Request);

			/* Callback - Inform the error */
			if (Request->Callback != NULL)
				Request->Callback->Callback(Request->Callback->Args, TransferStalled);

			/* Done for now */
			return;
		}

		/* Copy data if not dummy */
		while (lIterator)
		{
			/* Copy Data from transfer buffer to IoBuffer */
			if (lIterator->Length != 0)
				memcpy(lIterator->Buffer, lIterator->TransferBuffer, lIterator->Length);

			/* Restart Td */
			memcpy(lIterator->TransferDescriptor,
				lIterator->TransferDescriptorCopy, sizeof(UhciTransferDescriptor_t));

			/* Restore IOC */
			if (lIterator->Link == NULL)
				((UhciTransferDescriptor_t*)
				lIterator->TransferDescriptor)->Flags |= UHCI_TD_IOC;

			/* Eh, next link */
			lIterator = lIterator->Link;
		}

		/* Callback ?? needed */
		if (Request->Callback != NULL)
			Request->Callback->Callback(Request->Callback->Args, TransferFinished);

		/* Reschedule */
		UhciLinkIsochronousRequest(Controller, Request, 
			Controller->Frame + UHCI_QT_GET_QUEUE(Qh->Flags));
	}
}

/* UhciProcessTransfers 
 * Goes through all active transfers and handles them if they require
 * any handling. */
void
UhciProcessTransfers(
	_In_ UhciController_t *Controller)
{
	// Variables
	List_t *Transactions = Controller->QueueControl.TransactionList;
	UhciTransferDescriptor_t *Td = NULL;
	int ProcessQh = 0;

	// Update current frame
	UhciUpdateCurrentFrame(Controller);

	// Iterate all active transactions and see if any
	// one of them needs unlinking or linking
	foreach(Node, Transactions) {
		
		// Instantiate the usb-transfer pointer, and then the EP
		UsbManagerTransfer_t *Transfer = 
			(UsbManagerTransfer_t*)Node->Data;
		UhciQueueHead_t *Qh = 
			(UhciQueueHead_t*)Transfer->EndpointDescriptor;

		// Variables
		int ShortTransfer = 0;
		int ErrorTransfer = 0;
		int FixupToggles = 0;
		int Counter = 0;
		ProcessQh = 0;

		// Iterate through all td's in this transaction
		// and find the guilty
		Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
		while (Td) {
			
			// Keep count
			Counter++;

			/* Sanitize the TD descriptor 
			 * - If the TD is null then the transaction has
			 *   been cleaned up, but not removed in time */
			if (Td == NULL) {
				ProcessQh = 0;
				break;
			}

			/* Get code */
			int CondCode = UhciConditionCodeToIndex(UHCI_TD_STATUS(Td->Flags));
			int BytesTransfered = UHCI_TD_ACT_LEN(Td->Flags);
			int BytesRequest = UHCI_TD_GET_LEN(Td->Header);

			/* Sanity first */
			if (Td->Flags & UHCI_TD_ACTIVE) {

				/* If it's the first TD, skip */
				if (Counter == 1) {
					ProcessQh = 0;
					break;
				}

				/* Mark for processing */
				ProcessQh = 1;

				/* Fixup toggles? */
				if (HcRequest->Type == BulkTransfer
					|| HcRequest->Type == InterruptTransfer) {
					if (ShortTransfer == 1 || ErrorTransfer == 1)
						FixupToggles = 1;
				}

				/* Break */
				break;
			}

			/* Set for processing per default */
			ProcessQh = 1;

			/* TD is not active 
			 * this means it's been processed */
			if (BytesTransfered != 0x7FF
				&& BytesTransfered < BytesRequest) {
				ShortTransfer = 1;
			}

			/* Error Transfer ? */
			if (CondCode != 0 && CondCode != 3) {
				ErrorTransfer = 1;
			}

			// Go to next td or terminate
			if (Td->LinkIndex != UHCI_NO_INDEX) {
				Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
			}
			else {
				break;
			}
		}

		/* Does Qh need processing? */
		if (ProcessQh) {
			UhciProcessRequest(Controller, Node, HcRequest, FixupToggles, ErrorTransfer);
			break;
		}
	}
}
