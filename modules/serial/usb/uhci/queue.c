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

// Transaction Error Messages
const char *UhciErrorMessages[] =
{
	"No Error",
	"Bitstuff Error",
	"CRC/Timeout Error",
	"NAK Recieved",
	"Babble Detected",
	"Data Buffer Error",
	"Stalled",
	"Active"
};

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
		Queue->QHPool[i].LinkVirtual = (uint32_t)&Queue->QHPool[UHCI_QH_ASYNC];

		// Initialize qh
		Queue->QHPool[i].Child = UHCI_LINK_END;
		Queue->QHPool[i].ChildVirtual = 0;
		Queue->QHPool[i].Flags |= (UHCI_QH_SET_QUEUE(i) | UHCI_QH_ACTIVE);
	}

	// Initialize the null QH
	NullQhPhysical = UHCI_POOL_QHINDEX(Controller, UHCI_QH_NULL);
	Queue->QHPool[UHCI_QH_NULL].Link = (NullQhPhysical | UHCI_LINK_QH);
	Queue->QHPool[UHCI_QH_NULL].LinkVirtual = (uint32_t)&Queue->QHPool[UHCI_QH_NULL];
	Queue->QHPool[UHCI_QH_NULL].Child = NullTdPhysical;
	Queue->QHPool[UHCI_QH_NULL].ChildVirtual = (uint32_t)NullTd;
	Queue->QHPool[UHCI_QH_NULL].Flags |= UHCI_QH_ACTIVE;

	// Initialize the async QH
	Queue->QHPool[UHCI_QH_ASYNC].Link = UHCI_LINK_END;
	Queue->QHPool[UHCI_QH_ASYNC].LinkVirtual = 0;
	Queue->QHPool[UHCI_QH_ASYNC].Child = NullTdPhysical;
	Queue->QHPool[UHCI_QH_ASYNC].ChildVirtual = (uint32_t)NullTd;
	Queue->QHPool[UHCI_QH_ASYNC].Flags |= UHCI_QH_ACTIVE;

	// Set all queues to end in the async QH 
	// This way we handle iso & ints before bulk/control
	for (i = UHCI_QH_ISOCHRONOUS + 1; i < UHCI_QH_ASYNC; i++) {
		Queue->QHPool[i].Link = 
			UHCI_POOL_QHINDEX(Controller, UHCI_QH_ASYNC) | UHCI_LINK_QH;
		Queue->QHPool[i].LinkVirtual = (uint32_t)&Queue->QHPool[UHCI_QH_ASYNC];
	}

	// 1024 Entries
	// Set all entries to the 8 interrupt queues, and we
	// want them interleaved such that some queues get visited more than others
	for (i = 0; i < UHCI_NUM_FRAMES; i++) {
		FrameList[i] = UhciDetermineInterruptQh(Controller, (size_t)i);
	}

	// Initialize the transaction list
	Queue->TransactionList = ListCreate(KeyInteger, LIST_SAFE);
}

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

/* UhciAllocateQh 
 * Allocates and prepares a new Qh for a usb-transfer. */
UhciQueueHead_t*
UhciAllocateQh(
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

/* UhciAllocateTd
 * Allocates a new TD for usage with the transaction. If this returns
 * NULL we are out of TD's and we should wait till next transfer. */
UhciTransferDescriptor_t*
UhciAllocateTd(UhciEndpoint_t *Ep, UsbTransferType_t Type)
{
	/* Vars */
	UhciTransferDescriptor_t *Td;
	size_t i;

	/* Pick a QH */
	SpinlockAcquire(&Ep->Lock);

	/* Sanity */
	if (Type == ControlTransfer
		|| Type == BulkTransfer)
	{
		/* Grap it, locked operation */
		for (i = 0; i < Ep->TdsAllocated; i++)
		{
			/* Sanity */
			if (Ep->TDPool[i]->HcdFlags & UHCI_TD_HCD_ALLOCATED)
				continue;

			/* Found one, set flags  */
			Ep->TDPool[i]->HcdFlags = UHCI_TD_HCD_ALLOCATED;

			/* Return index */
			cIndex = (Addr_t)i;
			break;
		}

		/* Sanity */
		if (i == Ep->TdsAllocated)
			kernel_panic("USB_UHCI::WTF ran out of TD's!!!!\n");
	}
	else
	{
		/* Isochronous & Interrupt */

		/* Allocate a new */
		Td = (UhciTransferDescriptor_t*)UhciAlign((
			(Addr_t)kmalloc(sizeof(UhciTransferDescriptor_t) + UHCI_STRUCT_ALIGN)), 
				UHCI_STRUCT_ALIGN_BITS, UHCI_STRUCT_ALIGN);

		/* Null it */
		memset((void*)Td, 0, sizeof(UhciTransferDescriptor_t));

		/* Found one, set flags  */
		Td->HcdFlags = UHCI_TD_HCD_ALLOCATED;

		/* Store physical */
		Td->PhysicalAddr = AddressSpaceGetMap(AddressSpaceGetCurrent(), (Addr_t)Td);

		/* Set as index */
		cIndex = (Addr_t)Td;
	}

	/* Release Lock */
	SpinlockRelease(&Ep->Lock);

	/* Done! */
	return cIndex;
}

/* Initializes the Qh */
void UhciQhInit(UhciQueueHead_t *Qh, UsbHcTransaction_t *FirstTd)
{
	/* Set link pointer */
	Qh->Link = UHCI_TD_LINK_END;
	Qh->LinkVirtual = 0;

	/* Set Td list */
	Qh->Child = AddressSpaceGetMap(AddressSpaceGetCurrent(), 
		(Addr_t)FirstTd->TransferDescriptor);
	Qh->ChildVirtual = (uint32_t)FirstTd->TransferDescriptor;
}

/* Setup TD */
UhciTransferDescriptor_t *UhciTdSetup(UhciEndpoint_t *Ep, UsbTransferType_t Type,
	UsbPacket_t *pPacket, size_t DeviceAddr,
	size_t EpAddr, UsbSpeed_t Speed, void **TDBuffer)
{
	/* Vars */
	UhciTransferDescriptor_t *Td;
	Addr_t TDIndex;
	void *Buffer;

	/* Allocate a Td */
	TDIndex = OhciAllocateTd(Ep, Type);

	/* Grab a Td and a Buffer */
	Td = Ep->TDPool[TDIndex];
	Buffer = Ep->TDPoolBuffers[TDIndex];

	/* Set Link */
	Td->Link = UHCI_TD_LINK_END;

	/* Setup TD Control Status */
	Td->Flags = UHCI_TD_ACTIVE;
	Td->Flags |= UHCI_TD_SET_ERR_CNT(3);

	if (Speed == LowSpeed)
		Td->Flags |= UHCI_TD_LOWSPEED;

	/* Setup TD Header Packet */
	Td->Header = UHCI_TD_PID_SETUP;
	Td->Header |= UHCI_TD_DEVICE_ADDR(DeviceAddr);
	Td->Header |= UHCI_TD_EP_ADDR(EpAddr);
	Td->Header |= UHCI_TD_MAX_LEN((sizeof(UsbPacket_t) - 1));

	/* Setup SETUP packet */
	*TDBuffer = Buffer;
	memcpy(Buffer, (void*)pPacket, sizeof(UsbPacket_t));

	/* Set buffer */
	Td->Buffer = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Buffer);

	/* Done */
	return Td;
}

/* In/Out TD */
UhciTransferDescriptor_t *UhciTdIo(UhciEndpoint_t *Ep, UsbTransferType_t Type,
	size_t Toggle, size_t DeviceAddr, size_t EpAddr,
	uint32_t PId, size_t Length, UsbSpeed_t Speed, void **TDBuffer)
{
	/* Vars */
	UhciTransferDescriptor_t *Td;
	void *Buffer;
	Addr_t TDIndex;

	/* Allocate a Td */
	TDIndex = OhciAllocateTd(Ep, Type);

	/* Grab a buffer */
	if (Type == ControlTransfer || Type == BulkTransfer) {
		Td = Ep->TDPool[TDIndex];
		Buffer = Ep->TDPoolBuffers[TDIndex];
	}
	else {
		Td = (UhciTransferDescriptor_t*)TDIndex;
		Buffer = (void*)kmalloc_a(PAGE_SIZE);
	}

	/* Set Link */
	Td->Link = UHCI_TD_LINK_END;

	/* Setup TD Control Status */
	Td->Flags = UHCI_TD_ACTIVE;
	Td->Flags |= UHCI_TD_SET_ERR_CNT(3);

	/* Low speed transfer? */
	if (Speed == LowSpeed)
		Td->Flags |= UHCI_TD_LOWSPEED;

	/* Isochronous TD? */
	if (Type == IsochronousTransfer)
		Td->Flags |= UHCI_TD_ISOCHRONOUS;

	/* We set SPD on in transfers, but NOT on zero length */
	if (Type == ControlTransfer) {
		if (PId == UHCI_TD_PID_IN && Length > 0)
			Td->Flags |= UHCI_TD_SHORT_PACKET;
	}
	else if (PId == UHCI_TD_PID_IN)
		Td->Flags |= UHCI_TD_SHORT_PACKET;

	/* Setup TD Header Packet */
	Td->Header = PId;
	Td->Header |= UHCI_TD_DEVICE_ADDR(DeviceAddr);
	Td->Header |= UHCI_TD_EP_ADDR(EpAddr);

	/* Toggle? */
	if (Toggle)
		Td->Header |= UHCI_TD_DATA_TOGGLE;

	/* Set Size */
	if (Length > 0) {
		if (Length < Ep->MaxPacketSize
			&& Type == InterruptTransfer)
			Td->Header |= UHCI_TD_MAX_LEN((Ep->MaxPacketSize - 1));
		else
			Td->Header |= UHCI_TD_MAX_LEN((Length - 1));
	}
	else
		Td->Header |= UHCI_TD_MAX_LEN(0x7FF);

	/* Set buffer */
	*TDBuffer = Buffer;
	Td->Buffer = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Buffer);

	/* Done */
	return Td;
}

/* Bandwidth Functions */

/* Find highest current load for a given Phase/Period 
 * Used for calculating optimal bandwidth for a scheduler-queue */
int UhciCalculateBandwidth(UhciController_t *Controller, int Phase, int Period)
{
	/* Get current load */
	int HighestBw = Controller->Bandwidth[Phase];

	/* Iterate and check the bandwidth */
	for (Phase += Period; Phase < UHCI_BANDWIDTH_PHASES; Phase += Period)
		HighestBw = MAX(HighestBw, Controller->Bandwidth[Phase]);

	/* Done! */
	return HighestBw;
}

/* Max 90% of the bandwidth in a queue can go to iso/int 
 * thus we must make sure we don't go over that, this 
 * calculates if there is enough "room" for our Qh */
int UhciValidateBandwidth(UhciController_t *Controller, UhciQueueHead_t *Qh)
{
	/* Vars */
	int MinimalBw = 0;

	/* Find the optimal phase (unless it is already set) and get
	 * its load value. */
	if (Qh->Phase >= 0)
		MinimalBw = UhciCalculateBandwidth(Controller, Qh->Phase, Qh->Period);
	else 
	{
		/* Vars */
		int Phase, Bandwidth;
		int MaxPhase = MIN(UHCI_BANDWIDTH_PHASES, Qh->Period);

		/* Set initial */
		Qh->Phase = 0;
		MinimalBw = UhciCalculateBandwidth(Controller, Qh->Phase, Qh->Period);

		/* Iterate untill we locate the optimal phase */
		for (Phase = 1; Phase < MaxPhase; ++Phase)
		{
			/* Get bandwidth for this phase & period */
			Bandwidth = UhciCalculateBandwidth(Controller, Phase, Qh->Period);
			
			/* Sanity */
			if (Bandwidth < MinimalBw) {
				MinimalBw = Bandwidth;
				Qh->Phase = (uint16_t)Phase;
			}
		}
	}

	/* Maximum allowable periodic bandwidth is 90%, or 900 us per frame */
	if (MinimalBw + Qh->Bandwidth > 900)
		return -1;
	
	/* Done, Ok! */
	return 0;
}

/* Reserve Bandwidth */
void UhciReserveBandwidth(UhciController_t *Controller, UhciQueueHead_t *Qh)
{
	/* Vars */
	int Bandwidth = Qh->Bandwidth;
	int i;

	/* Iterate phase & period */
	for (i = Qh->Phase; i < UHCI_BANDWIDTH_PHASES; i += Qh->Period) {
		Controller->Bandwidth[i] += Bandwidth;
		Controller->TotalBandwidth += Bandwidth;
	}

	/* Set allocated */
	Qh->Flags |= UHCI_QH_BANDWIDTH_ALLOC;
}

/* Release Bandwidth */
void UhciReleaseBandwidth(UhciController_t *Controller, UhciQueueHead_t *Qh)
{
	/* Vars */
	int Bandwidth = Qh->Bandwidth;
	int i;

	/* Iterate and free */
	for (i = Qh->Phase; i < UHCI_BANDWIDTH_PHASES; i += Qh->Period) {
		Controller->Bandwidth[i] -= Bandwidth;
		Controller->TotalBandwidth -= Bandwidth;
	}
	
	/* Set not-allocated */
	Qh->Flags &= ~(UHCI_QH_BANDWIDTH_ALLOC);
}

/* Transaction Functions */
void UhciLinkIsochronousRequest(UhciController_t *Controller,
	UsbHcRequest_t *Request, uint32_t Frame)
{
	/* Vars */
	UsbHcTransaction_t *Transaction = Request->Transactions;
	uint32_t *Frames = (uint32_t*)Controller->FrameList;
	UhciTransferDescriptor_t *Td = NULL;
	UhciQueueHead_t *Qh = NULL; 

	/* Cast */
	Qh = (UhciQueueHead_t*)Request->Data;

	/* Iterate */
	while (Transaction)
	{
		/* Get TD */
		Td = (UhciTransferDescriptor_t*)Transaction->TransferDescriptor;

		/* Calculate Frame */
		Td->Frame = Frame;
		Frame += UHCI_QT_GET_QUEUE(Qh->Flags);

		/* Get previous */
		Td->Link = Frames[Td->Frame];

		/* MemB */
		MemoryBarrier();

		/* Link it */
		Frames[Td->Frame] = Td->PhysicalAddr;

		/* Next! */
		Transaction = Transaction->Link;
	}
}

void UhciUnlinkIsochronousRequest(UhciController_t *Controller, UsbHcRequest_t *Request)
{
	/* Vars */
	UsbHcTransaction_t *Transaction = Request->Transactions;
	uint32_t *Frames = (uint32_t*)Controller->FrameList;
	UhciTransferDescriptor_t *Td = NULL;
	UhciQueueHead_t *Qh = NULL;

	/* Cast */
	Qh = (UhciQueueHead_t*)Request->Data;

	/* Iterate */
	while (Transaction)
	{
		/* Get TD */
		Td = (UhciTransferDescriptor_t*)Transaction->TransferDescriptor;

		/* Unlink */
		if (Frames[Td->Frame] == Td->PhysicalAddr)
			Frames[Td->Frame] = Td->Link;

		/* Next! */
		Transaction = Transaction->Link;
	}
}

/* This one prepaires an ED */
void UhciTransactionInit(void *Controller, UsbHcRequest_t *Request)
{	
	/* Vars */
	UhciController_t *Ctrl = (UhciController_t*)Controller;
	UhciQueueHead_t *Qh = NULL;

	/* Get a QH */
	Qh = UhciAllocateQh(Ctrl, Request->Type, Request->Speed);

	/* Save it */
	Request->Data = Qh;

	/* Set as not Completed for start */
	Request->Status = TransferNotProcessed;

	/* Do we need to reserve bandwidth? */
	if (Qh != NULL && 
		!(Qh->Flags & UHCI_QH_BANDWIDTH_ALLOC))
	{
		/* Vars */
		int Exponent, Queue, Run = 1;

		/* Find the correct index we link into */
		for (Exponent = 7; Exponent >= 0; --Exponent) {
			if ((1 << Exponent) <= (int)Request->Endpoint->Interval)
				break;
		}

		/* Sanity */
		if (Exponent < 0) {
			LogFatal("UHCI", "Invalid interval %u", Request->Endpoint->Interval);
			Exponent = 0;
		}

		/* Make sure we have enough bandwidth for the transfer */
		if (Exponent > 0) {
			while (Run != 0 && --Exponent >= 0)
			{
				/* Now we can select a queue */
				Queue = 9 - Exponent;

				/* Set initial period */
				Qh->Period = 1 << Exponent;

				/* Set Qh Queue */
				Qh->Flags = UHCI_QH_CLR_QUEUE(Qh->Flags);
				Qh->Flags |= UHCI_QH_SET_QUEUE(Queue);

				/* For now, interrupt phase is fixed by the layout
				* of the QH lists. */
				Qh->Phase = (Qh->Period / 2) & (UHCI_BANDWIDTH_PHASES - 1);

				/* Check */
				Run = UhciValidateBandwidth(Ctrl, Qh);
			}
		}
		else
		{
			/* Now we can select a queue */
			Queue = 9 - Exponent;

			/* Set initial period */
			Qh->Period = 1 << Exponent;

			/* Set Qh Queue */
			Qh->Flags = UHCI_QH_CLR_QUEUE(Qh->Flags);
			Qh->Flags |= UHCI_QH_SET_QUEUE(Queue);

			/* For now, interrupt phase is fixed by the layout
			* of the QH lists. */
			Qh->Phase = (Qh->Period / 2) & (UHCI_BANDWIDTH_PHASES - 1);

			/* Check */
			Run = UhciValidateBandwidth(Ctrl, Qh);
		}

		/* Sanity */
		if (Run != 0) {
			LogFatal("UHCI", "Had no room for the transfer in queueus");
			return;
		}

		/* Reserve the bandwidth */
		UhciReserveBandwidth(Ctrl, Qh);
	}
}

/* This one prepaires an setup Td */
UsbHcTransaction_t *UhciTransactionSetup(void *Controller, UsbHcRequest_t *Request, UsbPacket_t *Packet)
{
	/* Vars */
	UhciController_t *Ctrl = (UhciController_t*)Controller;
	UsbHcTransaction_t *Transaction;

	/* Unused */
	_CRT_UNUSED(Ctrl);

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	memset(Transaction, 0, sizeof(UsbHcTransaction_t));

	/* Create the Td */
	Transaction->TransferDescriptor = (void*)UhciTdSetup(Request->Endpoint->AttachedData,
		Request->Type, Packet, 
		Request->Device->Address, Request->Endpoint->Address, 
		Request->Speed, &Transaction->TransferBuffer);

	/* Done! */
	return Transaction;
}

/* This one prepaires an in Td */
UsbHcTransaction_t *UhciTransactionIn(void *Controller, UsbHcRequest_t *Request, void *Buffer, size_t Length)
{
	/* Variables */
	UhciController_t *Ctrl = (UhciController_t*)Controller;
	UsbHcTransaction_t *Transaction;

	/* Unused */
	_CRT_UNUSED(Ctrl);

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	memset(Transaction, 0, sizeof(UsbHcTransaction_t));

	/* Set Vars */
	Transaction->Buffer = Buffer;
	Transaction->Length = Length;

	/* Setup Td */
	Transaction->TransferDescriptor = (void*)UhciTdIo(Request->Endpoint->AttachedData,
		Request->Type, Request->Endpoint->Toggle,
		Request->Device->Address, Request->Endpoint->Address, UHCI_TD_PID_IN, Length,
		Request->Speed, &Transaction->TransferBuffer);

	/* If previous Transaction */
	if (Request->Transactions != NULL)
	{
		UhciTransferDescriptor_t *PrevTd;
		UsbHcTransaction_t *cTrans = Request->Transactions;

		while (cTrans->Link)
			cTrans = cTrans->Link;

		PrevTd = (UhciTransferDescriptor_t*)cTrans->TransferDescriptor;
		PrevTd->Link =
			(AddressSpaceGetMap(AddressSpaceGetCurrent(), 
				(VirtAddr_t)Transaction->TransferDescriptor) | UHCI_TD_LINK_DEPTH);
	}

	/* We might need a copy */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer)
	{
		/* Allocate TD */
		Transaction->TransferDescriptorCopy =
			(void*)UhciAlign(((Addr_t)kmalloc(sizeof(UhciTransferDescriptor_t) + UHCI_STRUCT_ALIGN)), UHCI_STRUCT_ALIGN_BITS, UHCI_STRUCT_ALIGN);

		/* Copy data */
		memcpy(Transaction->TransferDescriptorCopy, 
			Transaction->TransferDescriptor, sizeof(UhciTransferDescriptor_t));
	}

	/* Done */
	return Transaction;
}

/* This one prepaires an out Td */
UsbHcTransaction_t *UhciTransactionOut(void *Controller, UsbHcRequest_t *Request, void *Buffer, size_t Length)
{
	/* Vars */
	UhciController_t *Ctrl = (UhciController_t*)Controller;
	UsbHcTransaction_t *Transaction;

	/* Unused */
	_CRT_UNUSED(Ctrl);

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	memset(Transaction, 0, sizeof(UsbHcTransaction_t));

	/* Set Vars */
	Transaction->Buffer = Buffer;
	Transaction->Length = Length;

	/* Setup Td */
	Transaction->TransferDescriptor = (void*)UhciTdIo(Request->Endpoint->AttachedData,
		Request->Type, Request->Endpoint->Toggle,
		Request->Device->Address, Request->Endpoint->Address, UHCI_TD_PID_OUT, Length,
		Request->Speed, &Transaction->TransferBuffer);

	/* Copy Data */
	if (Buffer != NULL && Length != 0)
		memcpy(Transaction->TransferBuffer, Buffer, Length);

	/* If previous Transaction */
	if (Request->Transactions != NULL)
	{
		UhciTransferDescriptor_t *PrevTd;
		UsbHcTransaction_t *cTrans = Request->Transactions;

		while (cTrans->Link)
			cTrans = cTrans->Link;

		PrevTd = (UhciTransferDescriptor_t*)cTrans->TransferDescriptor;
		PrevTd->Link =
			(AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Transaction->TransferDescriptor) | UHCI_TD_LINK_DEPTH);
	}

	/* We might need a copy */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer)
	{
		/* Allocate TD */
		Transaction->TransferDescriptorCopy =
			(void*)UhciAlign(((Addr_t)kmalloc(sizeof(UhciTransferDescriptor_t) + UHCI_STRUCT_ALIGN)), UHCI_STRUCT_ALIGN_BITS, UHCI_STRUCT_ALIGN);

		/* Copy data */
		memcpy(Transaction->TransferDescriptorCopy,
			Transaction->TransferDescriptor, sizeof(UhciTransferDescriptor_t));
	}

	/* Done */
	return Transaction;
}

/* This one queues the Transaction up for processing */
void UhciTransactionSend(void *Controller, UsbHcRequest_t *Request)
{
	/* Wuhu */
	UsbHcTransaction_t *Transaction = Request->Transactions;
	UhciController_t *Ctrl = (UhciController_t*)Controller;
	UsbTransferStatus_t Completed = TransferFinished;
	UhciQueueHead_t *Qh = NULL;
	UhciTransferDescriptor_t *Td = NULL;
	Addr_t QhAddress = 0;
	DataKey_t Key;
	int CondCode, Queue;

	/*************************
	 ****** SETUP PHASE ******
	 *************************/

	/* Cast */
	Qh = (UhciQueueHead_t*)Request->Data;

	/* Get physical */
	QhAddress = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Qh);

	/* Set as not Completed for start */
	Request->Status = TransferNotProcessed;

#ifdef UHCI_DIAGNOSTICS
	Transaction = Request->Transactions;
	while (Transaction)
	{
		Td = (UhciTransferDescriptor_t*)Transaction->TransferDescriptor;
		LogDebug("UHCI", "Td (Addr 0x%x) Flags 0x%x, Buffer 0x%x, Header 0x%x, Next Td 0x%x",
			AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Td),
			Td->Flags, Td->Buffer, Td->Header, Td->Link);

		/* Next */
		Transaction = Transaction->Link;
	}
#else
	/* Iterate */
	Transaction = Request->Transactions;
	while (Transaction->Link)
		Transaction = Transaction->Link;

	/* Set last TD with IOC */
	Td = (UhciTransferDescriptor_t*)Transaction->TransferDescriptor;
	Td->Flags |= UHCI_TD_IOC;
#endif

	/* Initialize QH */
	UhciQhInit(Qh, Request->Transactions);

	/* Add this Transaction to list */
	Key.Value = 0;
	ListAppend((List_t*)Ctrl->TransactionList, ListCreateNode(Key, Key, Request));

	/* Debug */
#ifdef UHCI_DIAGNOSTICS
	LogDebug("UHCI", "QH at 0x%x, FirstTd 0x%x, NextQh 0x%x", QhAddress, Qh->Child, Qh->Link);
#endif

	/*************************
	 **** LINKING PHASE ******
	 *************************/
	UhciGetCurrentFrame(Ctrl);

	/* Get queue */
	Queue = UHCI_QT_GET_QUEUE(Qh->Flags);

	/* Now lets try the Transaction */
	SpinlockAcquire(&Ctrl->Lock);

	/* For Control & Bulk, we link into async list */
	if (Queue >= UHCI_POOL_ASYNC)
	{
		/* Just append to async */
		UhciQueueHead_t *PrevQh = Ctrl->QhPool[UHCI_POOL_ASYNC];
		int PrevQueue = UHCI_QT_GET_QUEUE(PrevQh->Flags);

		/* Iterate to end */
		while (PrevQh->LinkVirtual != 0) 
		{	
			/* Get queue */
			PrevQueue = UHCI_QT_GET_QUEUE(PrevQh->Flags);

			/* Sanity */
			if (PrevQueue <= Queue)
				break;

			/* Next! */
			PrevQh = (UhciQueueHead_t*)PrevQh->LinkVirtual;
		}

		/* Steal it's link */
		Qh->Link = PrevQh->Link;
		Qh->LinkVirtual = PrevQh->LinkVirtual;

		/* Memory Barrier */
		MemoryBarrier();

		/* Set link - Atomic operation
		* We don't need to stop/start controller */
		PrevQh->Link = (QhAddress | UHCI_TD_LINK_QH);
		PrevQh->LinkVirtual = (uint32_t)Qh;

#ifdef UHCI_FSBR
		/* FSBR? */
		if (PrevQueue < UHCI_POOL_FSBR
			&& Queue >= UHCI_POOL_FSBR) {
			/* Link NULL to fsbr */
			Ctrl->QhPool[UHCI_POOL_NULL]->Link = (QhAddress | UHCI_TD_LINK_QH);
			Ctrl->QhPool[UHCI_POOL_NULL]->LinkVirtual = (uint32_t)Qh;
			
			/* Link last QH to NULL */
			PrevQh = Ctrl->QhPool[UHCI_POOL_ASYNC];
			while (PrevQh->LinkVirtual != 0)
				PrevQh = (UhciQueueHead_t*)PrevQh->LinkVirtual;
			PrevQh->Link = (Ctrl->QhPoolPhys[UHCI_POOL_NULL] | UHCI_TD_LINK_QH);
			PrevQh->LinkVirtual = (uint32_t)Ctrl->QhPool[UHCI_POOL_NULL];
		}
#endif
	}
	else if (Queue != UHCI_POOL_ISOCHRONOUS
		&& Queue < UHCI_POOL_ASYNC)
	{
		/* Get queue */
		UhciQueueHead_t *ItrQh = NULL, *PrevQh = NULL;
		int NextQueue;

		/* Iterate list */
		ItrQh = Ctrl->QhPool[Queue];

		/* Iterate to end */
		while (ItrQh)
		{
			/* Get next */
			PrevQh = ItrQh;
			ItrQh = (UhciQueueHead_t*)ItrQh->LinkVirtual;

			/* Sanity -> */
			if (ItrQh == NULL)
				break;

			/* Get queue of next */
			NextQueue = UHCI_QT_GET_QUEUE(ItrQh->Flags);

			/* Sanity */
			if (Queue < NextQueue)
				break;
		}

		/* Insert */
		Qh->Link = PrevQh->Link;
		Qh->LinkVirtual = PrevQh->LinkVirtual;

		/* Memory Barrier */
		MemoryBarrier();

		/* Set link - Atomic operation 
		 * We don't need to stop/start controller */
		PrevQh->Link = QhAddress | UHCI_TD_LINK_QH;
		PrevQh->LinkVirtual = (uint32_t)Qh;
	}
	else
	{
		/* Isochronous Transfer */
		UhciLinkIsochronousRequest(Ctrl, Request, Ctrl->Frame + 10);
	}

	/* Release lock */
	SpinlockRelease(&Ctrl->Lock);

	/* Sanity */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer)
		return;
	
#ifndef UHCI_DIAGNOSTICS
	/* Wait for interrupt */
	SchedulerSleepThread((Addr_t*)Qh, 5000);

	/* Yield */
	IThreadYield();
#else
	/* Wait */
	StallMs(100);

	/* Check Conditions */
	LogDebug("UHCI", "Qh Next 0x%x, Qh Head 0x%x", Qh->Link, Qh->Child);
#endif

	/*************************
	 *** VALIDATION PHASE ****
	 *************************/
	
	Transaction = Request->Transactions;
	while (Transaction)
	{
		/* Cast and get the transfer code */
		Td = (UhciTransferDescriptor_t*)Transaction->TransferDescriptor;
		CondCode = UhciConditionCodeToIndex(UHCI_TD_STATUS(Td->Flags));

		/* Calculate length transferred 
		 * Take into consideration N-1 */
		if (Transaction->Buffer != NULL
			&& Transaction->Length != 0) {
			Transaction->ActualLength = UHCI_TD_ACT_LEN(Td->Flags + 1);
		}

#ifdef UHCI_DIAGNOSTICS
		LogDebug("UHCI", "Td Flags 0x%x, Header 0x%x, Buffer 0x%x, Td Condition Code %u (%s)", 
			Td->Flags, Td->Header, Td->Buffer, CondCode, UhciErrorMessages[CondCode]);
#endif

		if (CondCode == 0 && Completed == TransferFinished)
			Completed = TransferFinished;
		else
		{
			if (CondCode == 6)
				Completed = TransferStalled;
			else if (CondCode == 1)
				Completed = TransferInvalidToggles;
			else if (CondCode == 2)
				Completed = TransferNotResponding;
			else if (CondCode == 3)
				Completed = TransferNAK;
			else {
				LogDebug("UHCI", "Error: 0x%x (%s)", CondCode, UhciErrorMessages[CondCode]);
				Completed = TransferInvalidData;
			}
			break;
		}

		Transaction = Transaction->Link;
	}

	/* Update Status */
	Request->Status = Completed;

#ifdef UHCI_DIAGNOSTICS
	for (;;);
#endif
}

/* Cleans up transfers */
void UhciTransactionDestroy(void *Controller, UsbHcRequest_t *Request)
{
	/* Cast, we need these */
	UsbHcTransaction_t *Transaction = Request->Transactions;
	UhciController_t *Ctrl = (UhciController_t*)Controller;
	UhciQueueHead_t *Qh = (UhciQueueHead_t*)Request->Data;
	ListNode_t *Node = NULL;

	/* Update */
	UhciGetCurrentFrame(Ctrl);

	/* Unallocate Qh */
	if (Request->Type == ControlTransfer
		|| Request->Type == BulkTransfer)
	{
		/* Lock controller */
		SpinlockAcquire(&Ctrl->Lock);

		/* Unlink Qh */
		UhciQueueHead_t *PrevQh = Ctrl->QhPool[UHCI_POOL_ASYNC];

		/* Iterate to end */
		while (PrevQh->LinkVirtual != (uint32_t)Qh)
			PrevQh = (UhciQueueHead_t*)PrevQh->LinkVirtual;

		/* Sanity */
		if (PrevQh->LinkVirtual != (uint32_t)Qh) {
			LogDebug("UHCI", "Couldn't find Qh in frame-list");
		}
		else 
		{
			/* Now skip */
			PrevQh->Link = Qh->Link;
			PrevQh->LinkVirtual = Qh->LinkVirtual;

			/* Memory Barrier */
			MemoryBarrier();

#ifdef UHCI_FSBR
			/* Get */
			int PrevQueue = UHCI_QT_GET_QUEUE(PrevQh->Flags);

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

			/* Iterate and reset */
			while (Transaction) {
				memset(Transaction->TransferDescriptor, 0, sizeof(UhciTransferDescriptor_t));
				Transaction = Transaction->Link;
			}

			/* Invalidate links */
			Qh->Child = 0;
			Qh->ChildVirtual = 0;
			Qh->Link = 0;
			Qh->LinkVirtual = 0;

			/* Mark inactive */
			Qh->Flags &= ~UHCI_QH_ACTIVE;
		}

		/* Done */
		SpinlockRelease(&Ctrl->Lock);
	}
	else if (Request->Type == InterruptTransfer)
	{
		/* Vars */
		UhciQueueHead_t *ItrQh = NULL, *PrevQh = NULL;
		int Queue = UHCI_QT_GET_QUEUE(Qh->Flags);

		/* Iterate and find our Qh */
		ItrQh = Ctrl->QhPool[Queue];
		while (ItrQh != Qh) {
			/* Validate we are not at the end */
			if (ItrQh->LinkVirtual == (uint32_t)Ctrl->QhPool[UHCI_POOL_NULL]
				|| ItrQh->LinkVirtual == 0) {
				ItrQh = NULL;
				break;
			}

			/* Next */
			PrevQh = ItrQh;
			ItrQh = (UhciQueueHead_t*)ItrQh->LinkVirtual;
		}

		/* If Qh is null, didn't exist */
		if (Qh == NULL) {
			LogDebug("UHCI", "Tried to unschedule a queue-qh that didn't exist in queue");
		}
		else
		{
			/* If there is a previous (there should always be) */
			if (PrevQh != NULL) {
				PrevQh->Link = Qh->Link;
				PrevQh->LinkVirtual = Qh->LinkVirtual;

				/* Memory Barrier */
				MemoryBarrier();
			}
		}

		/* Free bandwidth */
		UhciReleaseBandwidth(Ctrl, Qh);

		/* Iterate transactions and free buffers & td's */
		while (Transaction)
		{
			/* free buffer */
			kfree(Transaction->TransferBuffer);

			/* free both TD's */
			kfree((void*)Transaction->TransferDescriptor);
			kfree((void*)Transaction->TransferDescriptorCopy);

			/* Next */
			Transaction = Transaction->Link;
		}

		/* Free it */
		kfree(Request->Data);

		/* Remove from list */
		_foreach(Node, ((List_t*)Ctrl->TransactionList)) {
			if (Node->Data == Request)
				break;
		}

		/* Sanity */
		if (Node != NULL) {
			ListRemoveByNode((List_t*)Ctrl->TransactionList, Node);
			kfree(Node);
		}
	}
	else
	{
		/* Cast Qh */
		UhciQueueHead_t *Qh = (UhciQueueHead_t*)Request->Data;

		/* Unlink */
		UhciUnlinkIsochronousRequest(Ctrl, Request);

		/* Free bandwidth */
		UhciReleaseBandwidth(Ctrl, Qh);

		/* Iterate transactions and free buffers & td's */
		while (Transaction)
		{
			/* free buffer */
			kfree(Transaction->TransferBuffer);

			/* free both TD's */
			kfree((void*)Transaction->TransferDescriptor);
			kfree((void*)Transaction->TransferDescriptorCopy);

			/* Next */
			Transaction = Transaction->Link;
		}

		/* Free it */
		kfree(Request->Data);

		/* Remove from list */
		_foreach(Node, ((List_t*)Ctrl->TransactionList)) {
			if (Node->Data == Request)
				break;
		}

		/* Sanity */
		if (Node != NULL) {
			ListRemoveByNode((List_t*)Ctrl->TransactionList, Node);
			kfree(Node);
		}
	}
}

/* Fixup toggles for a failed transaction */
void UhciFixupToggles(UsbHcRequest_t *Request)
{
	/* Iterate */
	UsbHcTransaction_t *lIterator = Request->Transactions;
	int Toggle = 0;

	/* Now we iterate untill it went wrong, storing
	* the toggle-state, and when we reach the wrong
	* we flip the toggle and set it to EP */
	while (lIterator) {
		/* Get td */
		UhciTransferDescriptor_t *Td =
			(UhciTransferDescriptor_t*)lIterator->TransferDescriptor;

		/* Store toggle */
		if (Td->Header & UHCI_TD_DATA_TOGGLE)
			Toggle = 1;
		else
			Toggle = 0;

		/* At the unproccessed? */
		if (Td->Flags & UHCI_TD_ACTIVE)
			break;
	}

	/* Update EP */
	Request->Endpoint->Toggle = Toggle;
}

/* Processes a QH */
void UhciProcessRequest(UhciController_t *Controller, ListNode_t *Node, 
	UsbHcRequest_t *Request, int FixupToggles, int ErrorTransfer)
{
	/* What kind of transfer was this? */
	if (Request->Type == ControlTransfer
		|| Request->Type == BulkTransfer)
	{
		/* Perhaps we need to fixup toggles? */
		if (Request->Type == BulkTransfer && FixupToggles)
			UhciFixupToggles(Request);

		/* Wake a node */
		SchedulerWakeupOneThread((Addr_t*)Request->Data);

		/* Remove from list */
		ListRemoveByNode((List_t*)Controller->TransactionList, Node);

		/* Cleanup node */
		kfree(Node);
	}
	else if (Request->Type == InterruptTransfer)
	{
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

/* Handles completions */
void UhciProcessTransfers(UhciController_t *Controller)
{
	/* Transaction is completed / Failed */
	List_t *Transactions = (List_t*)Controller->TransactionList;
	int ProcessQh = 0;

	/* Update frame */
	UhciGetCurrentFrame(Controller);

	/* Get transactions in progress and find the offender */
	foreach(Node, Transactions)
	{
		/* Cast UsbRequest */
		UsbHcRequest_t *HcRequest = (UsbHcRequest_t*)Node->Data;

		/* Get transaction list */
		UsbHcTransaction_t *tList = (UsbHcTransaction_t*)HcRequest->Transactions;

		/* State variables */
		int ShortTransfer = 0;
		int ErrorTransfer = 0;
		int FixupToggles = 0;
		int Counter = 0;
		ProcessQh = 0;

		/* Loop through transactions */
		while (tList)
		{
			/* Increament */
			Counter++;

			/* Cast Td */
			UhciTransferDescriptor_t *Td = 
				(UhciTransferDescriptor_t*)tList->TransferDescriptor;

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

			/* Get next transaction */
			tList = tList->Link;
		}

		/* Does Qh need processing? */
		if (ProcessQh) {
			UhciProcessRequest(Controller, Node, HcRequest, FixupToggles, ErrorTransfer);
			break;
		}
	}
}
