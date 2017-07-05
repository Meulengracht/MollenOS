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
#include <os/utils.h>
#include "uhci.h"

/* Includes
 * - Library */
#include <string.h>

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

/* UhciInitQueues
 * Initialises Queue Heads & Interrupt Queeue */
OsStatus_t
UhciInitQueues(
    _In_ UhciController_t *Controller)
{
	/* Setup Vars */
	uint32_t *FrameListPtr = (uint32_t*)Controller->FrameList;
	Addr_t Pool = 0, PoolPhysical = 0;
	uint32_t i;

    /* Allocate in lower memory as controllers have
	 * a problem with higher memory */
	Controller->FrameList = 
		kmalloc_apm(PAGE_SIZE, &Controller->FrameListPhys, 0x00FFFFFF);

	/* Setup Null Td */
	Pool = (Addr_t)kmalloc(sizeof(UhciTransferDescriptor_t) + UHCI_STRUCT_ALIGN);
	Controller->NullTd = (UhciTransferDescriptor_t*)UhciAlign(Pool, UHCI_STRUCT_ALIGN_BITS, UHCI_STRUCT_ALIGN);
	Controller->NullTdPhysical = 
		AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Controller->NullTd);

	/* Memset it */
	memset((void*)Controller->NullTd, 0, sizeof(UhciTransferDescriptor_t));

	/* Set link invalid */
	Controller->NullTd->Header = (uint32_t)(UHCI_TD_PID_IN | UHCI_TD_DEVICE_ADDR(0x7F) | UHCI_TD_MAX_LEN(0x7FF));
	Controller->NullTd->Link = UHCI_TD_LINK_END;

	/* Setup Qh Pool */
	Pool = (Addr_t)kmalloc((sizeof(UhciQueueHead_t) * UHCI_POOL_NUM_QH) + UHCI_STRUCT_ALIGN);
	Pool = UhciAlign(Pool, UHCI_STRUCT_ALIGN_BITS, UHCI_STRUCT_ALIGN);
	PoolPhysical = (Addr_t)AddressSpaceGetMap(AddressSpaceGetCurrent(), Pool);

	/* Null out pool */
	memset((void*)Pool, 0, sizeof(UhciQueueHead_t) * UHCI_POOL_NUM_QH);

	/* Set them up */
	for (i = 0; i < UHCI_POOL_NUM_QH; i++)
	{
		/* Set QH */
		Controller->QhPool[i] = (UhciQueueHead_t*)Pool;
		Controller->QhPoolPhys[i] = PoolPhysical;

		/* Set its index */
		Controller->QhPool[i]->Flags = UHCI_QH_INDEX(i);

		/* Increase */
		Pool += sizeof(UhciQueueHead_t);
		PoolPhysical += sizeof(UhciQueueHead_t);
	}

	/* Setup interrupt queues */
	for (i = UHCI_POOL_ISOCHRONOUS + 1; i < UHCI_POOL_ASYNC; i++)
	{
		/* Set QH Link */
		Controller->QhPool[i]->Link = (Controller->QhPoolPhys[UHCI_POOL_ASYNC] | UHCI_TD_LINK_QH);
		Controller->QhPool[i]->LinkVirtual = (uint32_t)Controller->QhPool[UHCI_POOL_ASYNC];

		/* Disable TD List */
		Controller->QhPool[i]->Child = UHCI_TD_LINK_END;
		Controller->QhPool[i]->ChildVirtual = 0;

		/* Set in use */
		Controller->QhPool[i]->Flags |= (UHCI_QH_SET_QUEUE(i) | UHCI_QH_ACTIVE);
	}

	/* Setup null QH */
	Controller->QhPool[UHCI_POOL_NULL]->Link =
		(Controller->QhPoolPhys[UHCI_POOL_NULL] | UHCI_TD_LINK_QH);
	Controller->QhPool[UHCI_POOL_NULL]->LinkVirtual =
		(uint32_t)Controller->QhPool[UHCI_POOL_NULL];
	Controller->QhPool[UHCI_POOL_NULL]->Child = Controller->NullTdPhysical;
	Controller->QhPool[UHCI_POOL_NULL]->ChildVirtual = (uint32_t)Controller->NullTd;
	Controller->QhPool[UHCI_POOL_NULL]->Flags |= UHCI_QH_ACTIVE;

	/* Setup async Qh */
	Controller->QhPool[UHCI_POOL_ASYNC]->Link = UHCI_TD_LINK_END;
	Controller->QhPool[UHCI_POOL_ASYNC]->LinkVirtual = 0;
	Controller->QhPool[UHCI_POOL_ASYNC]->Child = Controller->NullTdPhysical;
	Controller->QhPool[UHCI_POOL_ASYNC]->ChildVirtual = (uint32_t)Controller->NullTd;
	Controller->QhPool[UHCI_POOL_ASYNC]->Flags |= UHCI_QH_ACTIVE;

	/* Set all queues to end in the async QH 
	 * This way we handle iso & ints before bulk/control */
	for (i = UHCI_POOL_ISOCHRONOUS + 1; i < UHCI_POOL_ASYNC; i++) {
		Controller->QhPool[i]->Link = 
			Controller->QhPoolPhys[UHCI_POOL_ASYNC] | UHCI_TD_LINK_QH;
		Controller->QhPool[i]->LinkVirtual = 
			(uint32_t)Controller->QhPool[UHCI_POOL_ASYNC];
	}

	/* 1024 Entries
	* Set all entries to the 8 interrupt queues, and we
	* want them interleaved such that some queues get visited more than others */
	for (i = 0; i < UHCI_NUM_FRAMES; i++)
		FrameListPtr[i] = UhciDetermineInterruptQh(Controller, i);

	/* Init transaction list */
	Controller->TransactionList = ListCreate(KeyInteger, LIST_SAFE);
}

/* Aligns address (with roundup if alignment is set) */
Addr_t UhciAlign(Addr_t BaseAddr, Addr_t AlignmentBits, Addr_t Alignment)
{
	/* Save, so we can modify */
	Addr_t AlignedAddr = BaseAddr;

	/* Only align if unaligned */
	if (AlignedAddr & AlignmentBits)
	{
		AlignedAddr &= ~AlignmentBits;
		AlignedAddr += Alignment;
	}

	/* Done */
	return AlignedAddr;
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
		Index = UHCI_POOL_ASYNC;
	}

	// Retrieve physical address of the calculated qh
	return (UHCI_POOL_QHINDEX(
		Controller->QueueControl.QHPoolPhysical, Index) | UHCI_LINK_QH);
}

/* Read current frame number */
void UhciGetCurrentFrame(UhciController_t *Controller)
{
	/* Vars */
	uint16_t FrameNo = 0, Diff = 0;

	/* Read */
	FrameNo = UhciRead16(Controller, UHCI_REGISTER_FRNUM);

	/* Calc diff */
	Diff = (FrameNo - Controller->Frame) & (UHCI_FRAME_MASK - 1);

	/* Add */
	Controller->Frame += Diff;
}

/* Convert condition code to nr */
int UhciConditionCodeToIndex(int ConditionCode)
{
	/* Vars */
	int bCount = 0;
	int Cc = ConditionCode;

	/* Keep bit-shifting */
	for (; Cc != 0;) {
		bCount++;
		Cc >>= 1;
	}

	/* Done */
	return bCount;
}

/* Determine transfer status from a td */
UsbTransferStatus_t UhciGetTransferStatus(UhciTransferDescriptor_t *Td)
{
	/* Get condition index from flags */
	UsbTransferStatus_t RetStatus = TransferFinished;
	int CondCode = UhciConditionCodeToIndex(UHCI_TD_STATUS(Td->Flags));

	/* Switch */
	if (CondCode == 6)
		RetStatus = TransferStalled;
	else if (CondCode == 1)
		RetStatus = TransferInvalidToggles;
	else if (CondCode == 2)
		RetStatus = TransferNotResponding;
	else if (CondCode == 3)
		RetStatus = TransferNAK;
	else if (CondCode == 4)
		RetStatus = TransferBabble;
	else if (CondCode == 5)
		RetStatus = TransferInvalidData;

	/* Done! */
	return RetStatus;
}

/* QH Functions */
UhciQueueHead_t *UhciAllocateQh(UhciController_t *Controller, UsbTransferType_t Type, UsbSpeed_t Speed)
{
	/* Vars */
	UhciQueueHead_t *Qh = NULL;
	int i;

	/* Pick a QH */
	SpinlockAcquire(&Controller->Lock);

	/* Grap it, locked operation */
	if (Type == ControlTransfer
		|| Type == BulkTransfer)
	{
		/* Grap Index */
		for (i = UHCI_POOL_START; i < UHCI_POOL_NUM_QH; i++)
		{
			/* Sanity */
			if (Controller->QhPool[i]->Flags & UHCI_QH_ACTIVE)
				continue;

			/* First thing to do is setup flags */
			Controller->QhPool[i]->Flags = 
				UHCI_QH_ACTIVE | UHCI_QH_INDEX(i)
				| UHCI_QH_TYPE((uint32_t)Type) | UHCI_QH_BANDWIDTH_ALLOC;

			/* Set qh */
			if (Speed == LowSpeed
				&& Type == ControlTransfer)
				Controller->QhPool[i]->Flags |= UHCI_QH_SET_QUEUE(UHCI_POOL_LCTRL);
			else if (Speed == FullSpeed
				&& Type == ControlTransfer)
				Controller->QhPool[i]->Flags |= UHCI_QH_SET_QUEUE(UHCI_POOL_FCTRL) | UHCI_QH_FSBR;
			else if (Type == BulkTransfer)
				Controller->QhPool[i]->Flags |= UHCI_QH_SET_QUEUE(UHCI_POOL_FBULK) | UHCI_QH_FSBR;

			/* Set index */
			Qh = Controller->QhPool[i];
			break;
		}

		/* Sanity */
		if (i == UHCI_POOL_NUM_QH)
			kernel_panic("USB_UHCI::WTF RAN OUT OF QH's\n");
	}
	else
	{
		/* Iso & Int */

		/* Allocate */
		Addr_t aSpace = (Addr_t)kmalloc(sizeof(UhciQueueHead_t) + UHCI_STRUCT_ALIGN);
		Qh = (UhciQueueHead_t*)UhciAlign(aSpace, UHCI_STRUCT_ALIGN_BITS, UHCI_STRUCT_ALIGN);

		/* Zero it out */
		memset((void*)Qh, 0, sizeof(UhciQueueHead_t));

		/* Setup flags */
		Qh->Flags =
			UHCI_QH_ACTIVE | UHCI_QH_INDEX(0xFF) | UHCI_QH_TYPE((uint32_t)Type);
	}

	/* Release lock */
	SpinlockRelease(&Controller->Lock);

	/* Done */
	return Qh;
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

/* TD Functions */
Addr_t OhciAllocateTd(UhciEndpoint_t *Ep, UsbTransferType_t Type)
{
	/* Vars */
	size_t i;
	Addr_t cIndex = 0;
	UhciTransferDescriptor_t *Td;

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


/* Endpoint Functions */
void UhciEndpointSetup(void *Controller, UsbHcEndpoint_t *Endpoint)
{
	/* Cast */
	UhciController_t *uCtrl = (UhciController_t*)Controller;
	Addr_t BufAddr = 0, BufAddrMax = 0;
	Addr_t Pool, PoolPhys;
	size_t i;

	/* Allocate a structure */
	UhciEndpoint_t *uEp = (UhciEndpoint_t*)kmalloc(sizeof(UhciEndpoint_t));

	/* Construct the lock */
	SpinlockReset(&uEp->Lock);

	/* Set initial info */
	uEp->MaxPacketSize = Endpoint->MaxPacketSize;

	/* Woah */
	_CRT_UNUSED(uCtrl);

	/* Now, we want to allocate some TD's
	* but it largely depends on what kind of endpoint this is */
	if (Endpoint->Type == EndpointControl)
		uEp->TdsAllocated = UHCI_ENDPOINT_MIN_ALLOCATED;
	else if (Endpoint->Type == EndpointBulk)
	{
		/* Depends on the maximum transfer */
		uEp->TdsAllocated = DEVICEMANAGER_MAX_IO_SIZE / Endpoint->MaxPacketSize;

		/* Take in account control packets and other stuff */
		uEp->TdsAllocated += UHCI_ENDPOINT_MIN_ALLOCATED;
	}
	else
	{
		/* We handle interrupt & iso dynamically
		* we don't predetermine their sizes */
		uEp->TdsAllocated = 0;
		Endpoint->AttachedData = uEp;
		return;
	}

	/* Now, we do the actual allocation */
	uEp->TDPool = (UhciTransferDescriptor_t**)kmalloc(sizeof(UhciTransferDescriptor_t*) * uEp->TdsAllocated);
	uEp->TDPoolBuffers = (Addr_t**)kmalloc(sizeof(Addr_t*) * uEp->TdsAllocated);
	uEp->TDPoolPhysical = (Addr_t*)kmalloc(sizeof(Addr_t) * uEp->TdsAllocated);

	/* Allocate a TD block */
	Pool = (Addr_t)kmalloc((sizeof(UhciTransferDescriptor_t) * uEp->TdsAllocated) + UHCI_STRUCT_ALIGN);
	Pool = UhciAlign(Pool, UHCI_STRUCT_ALIGN_BITS, UHCI_STRUCT_ALIGN);
	PoolPhys = AddressSpaceGetMap(AddressSpaceGetCurrent(), Pool);

	/* Allocate buffers */
	BufAddr = (Addr_t)kmalloc_a(PAGE_SIZE);
	BufAddrMax = BufAddr + PAGE_SIZE - 1;

	/* Memset it */
	memset((void*)Pool, 0, sizeof(UhciTransferDescriptor_t) * uEp->TdsAllocated);

	/* Iterate it */
	for (i = 0; i < uEp->TdsAllocated; i++)
	{
		/* Set */
		uEp->TDPool[i] = (UhciTransferDescriptor_t*)Pool;
		uEp->TDPoolPhysical[i] = PoolPhys;

		/* Allocate another page? */
		if (BufAddr > BufAddrMax)
		{
			BufAddr = (Addr_t)kmalloc_a(PAGE_SIZE);
			BufAddrMax = BufAddr + PAGE_SIZE - 1;
		}

		/* Setup Buffer */
		uEp->TDPoolBuffers[i] = (Addr_t*)BufAddr;
		uEp->TDPool[i]->Buffer = AddressSpaceGetMap(AddressSpaceGetCurrent(), BufAddr);
		uEp->TDPool[i]->Link = UHCI_TD_LINK_END;

		/* Increase */
		Pool += sizeof(UhciTransferDescriptor_t);
		PoolPhys += sizeof(UhciTransferDescriptor_t);
		BufAddr += Endpoint->MaxPacketSize;
	}

	/* Done! Save */
	Endpoint->AttachedData = uEp;
}

void UhciEndpointDestroy(void *Controller, UsbHcEndpoint_t *Endpoint)
{
	/* Cast */
	UhciController_t *uCtrl = (UhciController_t*)Controller;
	UhciEndpoint_t *uEp = (UhciEndpoint_t*)Endpoint->AttachedData;

	/* Sanity */
	if (uEp == NULL)
		return;

	/* Woah */
	_CRT_UNUSED(uCtrl);

	/* Sanity */
	if (uEp->TdsAllocated != 0)
	{
		/* Vars */
		UhciTransferDescriptor_t *uTd = uEp->TDPool[0];
		size_t i;

		/* Let's free all those resources */
		for (i = 0; i < uEp->TdsAllocated; i++)
		{
			/* free buffer */
			kfree(uEp->TDPoolBuffers[i]);
		}

		/* Free blocks */
		kfree(uTd);
		kfree(uEp->TDPoolBuffers);
		kfree(uEp->TDPoolPhysical);
		kfree(uEp->TDPool);
	}

	/* Free the descriptor */
	kfree(uEp);
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
