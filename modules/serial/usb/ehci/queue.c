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
 * - Isochronous Transport
 * - Transaction Translator Support
 */

/* Includes
 * - System */
#include <os/mollenos.h>
#include "ehci.h"

/* Includes
 * - Library */
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

/* Disable the warning about conditional
 * expressions being constant, they are intentional */
#ifdef _MSC_VER
#pragma warning(disable:4127)
#endif

/* EhciQueueInitialize
 * Initialize the controller's queue resources and resets counters */
OsStatus_t
EhciQueueInitialize(
	_In_ EhciController_t *Controller)
{
	// Variables
	EhciControl_t *Queue = NULL;
	uintptr_t RequiredSpace = 0, PoolPhysical = 0;
	void *Pool = NULL;
	int i;

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
	Queue->VirtualList = (reg32_t*)malloc(Queue->FrameLength * sizeof(reg32_t));

	// Add up all the size we are going to need for pools and
	// the actual frame list
	RequiredSpace += Queue->FrameLength * sizeof(reg32_t);        // Framelist
	RequiredSpace += sizeof(EhciQueueHead_t) * EHCI_POOL_NUM_QH;  // Qh-pool
	RequiredSpace += sizeof(EhciTransferDescriptor_t) * EHCI_POOL_NUM_TD; // Td-pool

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

	// Initialize frame lists
	for (i = 0; i < Queue->FrameLength; i++) {
		Queue->VirtualList[i] = Queue->FrameList[i] = EHCI_LINK_END;
	}

	// Initialize the QH pool
	for (i = 0; i < EHCI_POOL_NUM_QH; i++) {
		Queue->QHPool[i].Index = i;
		Queue->QHPool[i].LinkIndex = EHCI_NO_INDEX;
	}

	// Initialize the TD pool
	for (i = 0; i < EHCI_POOL_NUM_TD; i++) {
		Queue->TDPool[i].Index = i;
		Queue->TDPool[i].LinkIndex = EHCI_NO_INDEX;
		Queue->TDPool[i].AlternativeLinkIndex = EHCI_NO_INDEX;
	}

	// Initialize the dummy (null) queue-head that we use for end-link
	Queue->QHPool[EHCI_POOL_QH_NULL].Overlay.NextTD = EHCI_LINK_END;
	Queue->QHPool[EHCI_POOL_QH_NULL].Overlay.NextAlternativeTD = EHCI_LINK_END;
	Queue->QHPool[EHCI_POOL_QH_NULL].LinkPointer = EHCI_LINK_END;
	Queue->QHPool[EHCI_POOL_QH_NULL].HcdFlags = EHCI_QH_ALLOCATED;

	// Initialize the dummy (async) transfer-descriptor that we use for queuing
	Queue->TDPool[EHCI_POOL_TD_ASYNC].Status = EHCI_TD_HALTED;
	Queue->TDPool[EHCI_POOL_TD_ASYNC].Link = EHCI_LINK_END;
	Queue->TDPool[EHCI_POOL_TD_ASYNC].AlternativeLink = EHCI_LINK_END;

	/* Initialize Async */
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->LinkPointer = 
		Controller->QhPool[EHCI_POOL_QH_ASYNC]->PhysicalAddress | EHCI_LINK_QH;
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->LinkPointerVirtual = 
		(uint32_t)Controller->QhPool[EHCI_POOL_QH_ASYNC];

	Controller->QhPool[EHCI_POOL_QH_ASYNC]->Flags = EHCI_QH_RECLAMATIONHEAD;
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->Overlay.Status = EHCI_TD_HALTED;
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->Overlay.NextTD = EHCI_LINK_END;
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->Overlay.NextAlternativeTD =
		AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Controller->TdAsync);
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->HcdFlags = EHCI_QH_ALLOCATED;

	// Allocate the transaction list
	Queue->TransactionList = ListCreate(KeyInteger, LIST_SAFE);

	// Initialize a bandwidth scheduler
	Controller->Scheduler = UsbSchedulerInitialize(
		Queue->FrameLength, EHCI_MAX_BANDWIDTH, 8);

	// Update the hardware registers to point to the newly allocated
	// addresses
	Controller->OpRegisters->PeriodicListAddress = 
		(reg32_t)Queue->FrameListPhysical;
	Controller->OpRegisters->AsyncListAddress = 
		(reg32_t)Queue->QHPool[EHCI_POOL_QH_ASYNC].PhysicalAddress | EHCI_LINK_QH;
}

/* EhciQueueDestroy
 * Unschedules any scheduled ed's and frees all resources allocated
 * by the initialize function */
OsStatus_t
EhciQueueDestroy(
	_In_ EhciController_t *Controller)
{

}

/* Helpers */
int
EhciConditionCodeToIndex(
	_In_ uint32_t ConditionCode)
{
	/* Vars */
	int bCount = 0;
	uint32_t Cc = ConditionCode;

	/* Keep bit-shifting */
	for (; Cc != 0;) {
		bCount++;
		Cc >>= 1;
	}

	/* Done */
	return bCount;
}

/* Enables the async scheduler if it 
 * is not enabled already */
void EhciEnableAsyncScheduler(EhciController_t *Controller)
{
	/* Vars */
	uint32_t Temp = 0;

	/* Sanity */
	if (Controller->OpRegisters->UsbStatus & EHCI_STATUS_ASYNC_ACTIVE)
		return;

	/* Enable */
	Temp = Controller->OpRegisters->UsbCommand;
	Temp |= EHCI_COMMAND_ASYNC_ENABLE;
	Controller->OpRegisters->UsbCommand = Temp;
}

/* Disables the async sheduler if it 
 * is not disabled already */
void EhciDisableAsyncScheduler(EhciController_t *Controller)
{
	/* Vars */
	uint32_t Temp = 0;

	/* If it's not running do nothing */
	if (!(Controller->OpRegisters->UsbStatus & EHCI_STATUS_ASYNC_ACTIVE))
		return;

	/* Disable */
	Temp = Controller->OpRegisters->UsbCommand;
	Temp &= ~(EHCI_COMMAND_ASYNC_ENABLE);
	Controller->OpRegisters->UsbCommand = Temp;
}

/* This functions rings the bell
 * and waits for the door to open */
void EhciRingDoorbell(EhciController_t *Controller, Addr_t *Data)
{
	/* Now we have to force a doorbell */
	if (!Controller->BellIsRinging) {
		Controller->BellIsRinging = 1;
		Controller->OpRegisters->UsbCommand |= EHCI_COMMAND_IOC_ASYNC_DOORBELL;
	}
	else
		Controller->BellReScan = 1;

	/* Flush */
	MemoryBarrier();

	/* Wait */
	SchedulerSleepThread(Data, 5000);
	IThreadYield();
}

/* Get's a pointer to the next virtaul 
 * link, only Qh's have this implemented 
 * right now and will need modifications */
EhciGenericLink_t *EhciNextGenericLink(EhciGenericLink_t *Link, uint32_t Type)
{
	switch (Type) {
	case EHCI_LINK_QH:
		return (EhciGenericLink_t*)&Link->Qh->LinkPointerVirtual;
	case EHCI_LINK_FSTN:
		return (EhciGenericLink_t*)&Link->FSTN->PathPointer;
	case EHCI_LINK_iTD:
		return (EhciGenericLink_t*)&Link->iTd->Link;
	default:
		return (EhciGenericLink_t*)&Link->siTd->Link;
	}
}

/* This function links an interrupt Qh 
 * into the schedule at Qh->sFrame 
 * and every other Qh->Interval */
void EhciLinkPeriodicQh(EhciController_t *Controller, EhciQueueHead_t *Qh)
{
	/* Vars */
	size_t Period = Qh->Interval;
	size_t i;

	/* Sanity 
	 * a minimum of every frame */
	if (Period == 0)
		Period = 1;

	/* Iterate */
	for (i = Qh->sFrame; i < Controller->FLength; i += Period) 
	{
		/* Virtual Pointer */
		EhciGenericLink_t *pLink =
			(EhciGenericLink_t*)&Controller->VirtualList[i];

		/* Hardware Pointer */
		uint32_t *pHw = &Controller->FrameList[i];

		/* Lokals */
		EhciGenericLink_t This = *pLink;
		uint32_t Type = 0;

		/* Iterate past isochronous tds */
		while (This.Address) {
			Type = EHCI_LINK_TYPE(*pHw);
			if (Type == EHCI_LINK_QH)
				break;

			/* Next */
			pLink = EhciNextGenericLink(pLink, Type);
			pHw = &This.Address;
			This = *pLink;
		}

		/* sorting each branch by period (slow-->fast)
		 * enables sharing interior tree nodes */
		while (This.Address && Qh != This.Qh) {
			/* Sanity */
			if (Qh->Interval > This.Qh->Interval)
				break;

			/* Move to next */
			pLink = (EhciGenericLink_t*)&This.Qh->LinkPointerVirtual;
			pHw = &This.Qh->LinkPointer;
			This = *pLink;
		}

		/* link in this qh, unless some earlier pass did that */
		if (Qh != This.Qh) 
		{
			/* Steal link */
			Qh->LinkPointerVirtual = (uint32_t)This.Qh;
			if (This.Qh)
				Qh->LinkPointer = *pHw;

			/* Memory Barrier */
			MemoryBarrier();

			/* Link it in */
			pLink->Qh = Qh;
			*pHw = (Qh->PhysicalAddress | EHCI_LINK_QH);
		}
	}
}

/* Generic unlink from periodic lsit 
 * needs a bit more information as it
 * is used for all formats */
void EhciUnlinkPeriodic(EhciController_t *Controller, Addr_t Address, size_t Period, size_t sFrame)
{
	/* Vars */
	size_t i;

	/* Sanity
	* a minimum of every frame */
	if (Period == 0)
		Period = 1;

	/* We should mark Qh->Flags |= EHCI_QH_INVALIDATE_NEXT 
	 * and wait for next frame */
	/* Iterate */
	for (i = sFrame; i < Controller->FLength; i += Period)
	{
		/* Virtual Pointer */
		EhciGenericLink_t *pLink =
			(EhciGenericLink_t*)&Controller->VirtualList[i];

		/* Hardware Pointer */
		uint32_t *pHw = &Controller->FrameList[i];

		/* Lokals */
		EhciGenericLink_t This = *pLink;
		uint32_t Type = 0;

		/* Find previous handle that points to our qh */
		while (This.Address 
			&& This.Address != Address) 
		{
			/* Just keep going forward ! */
			Type = EHCI_LINK_TYPE(*pHw);
			pLink = EhciNextGenericLink(pLink, Type);
			pHw = &This.Address;
			This = *pLink;
		}

		/* Make sure we are not at end */
		if (!This.Address)
			return;

		/* Update links */
		Type = EHCI_LINK_TYPE(*pHw);
		*pLink = *EhciNextGenericLink(&This, Type);

		if (*(&This.Address) != EHCI_LINK_END)
			*pHw = *(&This.Address);
	}
}

/* Queue Functions */

/* This allocates a QH for a
* Control, Bulk and Interrupt 
* transaction and should not be 
* used for isoc */
EhciQueueHead_t *EhciAllocateQh(EhciController_t *Controller, UsbTransferType_t Type)
{
	/* Vars */
	EhciQueueHead_t *Qh = NULL;

	/* Acquire Lock */
	SpinlockAcquire(&Controller->Lock);

	/* Allocate based on transaction type */
	if (Type == ControlTransfer
		|| Type == BulkTransfer)
	{
		/* Take from the pool */
		int i;

		/* Iterate */
		for (i = 0; i < EHCI_POOL_NUM_QH; i++) {
			/* Sanity */
			if (Controller->QhPool[i]->HcdFlags & EHCI_QH_ALLOCATED)
				continue;

			/* Allocate */
			Controller->QhPool[i]->Overlay.Status = EHCI_TD_HALTED;
			Controller->QhPool[i]->HcdFlags = EHCI_QH_ALLOCATED;

			/* Done! */
			Qh = Controller->QhPool[i];
			break;
		}

		/* Sanity */
		if (i == EHCI_POOL_NUM_QH)
			kernel_panic("USB_EHCI::: RAN OUT OF QH'S!\n");
	}
	else if (Type == InterruptTransfer)
	{
		/* Interrupt -> Allocate */
		Addr_t pSpace = (Addr_t)kmalloc(sizeof(EhciQueueHead_t) + EHCI_STRUCT_ALIGN);
		pSpace = ALIGN(pSpace, EHCI_STRUCT_ALIGN, 1);

		/* Memset */
		memset((void*)pSpace, 0, sizeof(EhciQueueHead_t));

		/* Cast */
		Qh = (EhciQueueHead_t*)pSpace;

		/* Setup */
		Qh->Overlay.Status = EHCI_TD_HALTED;
		Qh->Overlay.NextTD = EHCI_LINK_END;
		Qh->Overlay.NextAlternativeTD = EHCI_LINK_END;
		Qh->PhysicalAddress = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Qh);

		/* Set allocated */
		Qh->HcdFlags = EHCI_QH_ALLOCATED;
	}
	else
	{
		/* Isoc */
	}

	/* Unlock */
	SpinlockRelease(&Controller->Lock);

	/* Done */
	return Qh;
}

/* This initiates any 
 * periodic scheduling 
 * information that might be 
 * needed */
void EhciInititalizeQh(EhciController_t *Controller, UsbHcRequest_t *Request, EhciQueueHead_t *Qh)
{
	/* Get frame-count */
	uint32_t TransactionsPerFrame = Request->Endpoint->Bandwidth;

	/* Calculate Bandwidth */
	Qh->Bandwidth = (uint16_t)
		NS_TO_US(UsbCalculateBandwidth(Request->Speed,
		Request->Endpoint->Direction, Request->Type,
		TransactionsPerFrame * Request->Endpoint->MaxPacketSize));

	/* Calculate actual period */
	if (Request->Speed == HighSpeed
		|| (Request->Speed == FullSpeed && Request->Type == IsochronousTransfer))
	{
		/* Calculate period as 2^(Interval-1) */
		Qh->Interval = (1 << Request->Endpoint->Interval);
	}
	else
		Qh->Interval = Request->Endpoint->Interval;

	/* Validate Bandwidth */
	if (UsbSchedulerValidate(Controller->Scheduler, Qh->Interval, Qh->Bandwidth, TransactionsPerFrame))
		LogDebug("EHCI", "Couldn't allocate space in scheduler for params %u:%u", 
			Qh->Interval, Qh->Bandwidth);
}

/* Transfer Descriptor Functions */

/* This allocates a QTD (TD) for 
 * Control, Bulk and Interrupt */
EhciTransferDescriptor_t *EhciAllocateTd(EhciEndpoint_t *Ep)
{
	/* Vars */
	EhciTransferDescriptor_t *Td = NULL;
	size_t i;

	/* Acquire Lock */
	SpinlockAcquire(&Ep->Lock);

	/* If EP is empty, custom allocation */
	if (Ep->TdsAllocated != 0)
	{
		/* Grap it, locked operation */
		for (i = 0; i < Ep->TdsAllocated; i++)
		{
			/* Sanity */
			if (Ep->TDPool[i]->HcdFlags & EHCI_TD_ALLOCATED)
				continue;

			/* Yay!! */
			Ep->TDPool[i]->HcdFlags = EHCI_TD_ALLOCATED;
			Td = Ep->TDPool[i];
			break;
		}

		/* Sanity */
		if (i == Ep->TdsAllocated)
			kernel_panic("USB_EHCI::WTF ran out of TD's!!!!\n");
	}
	else
	{
		/* Interrupt */
		Td = (EhciTransferDescriptor_t*)ALIGN(
			(Addr_t)kmalloc(sizeof(EhciTransferDescriptor_t) + EHCI_STRUCT_ALIGN), EHCI_STRUCT_ALIGN, 1);

		/* Initialize */
		memset(Td, 0, sizeof(EhciTransferDescriptor_t));
	}

	/* Release */
	SpinlockRelease(&Ep->Lock);

	/* Done! */
	return Td;
}

/* This allocates buffer-space for 
 * all types of transfers */
Addr_t *EhciAllocateBuffers(EhciEndpoint_t *Ep, size_t Length, uint16_t *BufInfo)
{
	/* Vars */
	Addr_t *Buffer = NULL;
	size_t i, j, NumPages = 0;
	uint16_t bInfo = 0;

	/* Sanity */
	if (Length == 0)
		return NULL;

	/* How many pages to allocate? */
	NumPages = DIVUP(Length, PAGE_SIZE);
	bInfo = ((NumPages & 0xF) << 8);

	/* Acquire Lock */
	SpinlockAcquire(&Ep->Lock);

	/* Sanity */
	if (Ep->BuffersAllocated != 0)
	{
		/* Grap it, locked operation */
		for (i = 0; i < Ep->BuffersAllocated; i++)
		{
			/* Sanity */
			if (Ep->BufferPoolStatus[i] == EHCI_POOL_BUFFER_ALLOCATED)
				continue;

			/* Yay!! */
			Buffer = Ep->BufferPool[i];

			/* Store */
			bInfo |= (i & 0xFF);

			/* Now, how many should we allocate? */
			for (j = 0; j < NumPages; j++) {
				Ep->BufferPoolStatus[i + j] = EHCI_POOL_BUFFER_ALLOCATED;
			}

			break;
		}

		/* Sanity */
		if (i == Ep->TdsAllocated)
			kernel_panic("USB_EHCI::WTF ran out of TD BUFFERS!!!!\n");
	}
	else
	{
		/* Interrupt & Isoc */
		Buffer = kmalloc_a(PAGE_SIZE * NumPages);
	}

	/* Release */
	SpinlockRelease(&Ep->Lock);

	/* Memset */
	memset(Buffer, 0, PAGE_SIZE * NumPages);

	/* Update */
	*BufInfo = bInfo;

	/* Done! */
	return Buffer;
}

/* This deallocates the buffer-space
 * which was requested for a td */
void EhciDeallocateBuffers(EhciEndpoint_t *Ep, EhciTransferDescriptor_t *Td)
{
	/* Vars */
	uint32_t BufferIndex = 0;
	uint32_t BufferCount = 0;
	uint32_t i;

	/* Get lock */
	SpinlockAcquire(&Ep->Lock);

	/* Get counts */
	BufferIndex = EHCI_TD_GETIBUF(Td->HcdFlags);
	BufferCount = EHCI_TD_GETJBUF(Td->HcdFlags);

	/* Sanity */
	if (BufferCount != 0) {
		/* Iterate and free */
		for (i = 0; i < BufferCount; i++) {
			Ep->BufferPoolStatus[BufferIndex + i] = 0;
		}
	}

	/* Release lock */
	SpinlockRelease(&Ep->Lock);
}

/* This sets up a QTD (TD) buffer
 * structure and makes sure it's split correctly 
 * out on all the pages */
size_t EhciTdFill(EhciTransferDescriptor_t *Td, Addr_t Buffer, size_t Length)
{
	/* Vars */
	size_t LengthRemaining = Length;
	size_t Count = 0;
	int i;

	/* Sanity */
	if (Length == 0)
		return 0;

	/* Iterate */
	for (i = 0; LengthRemaining > 0 && i < 5; i++)
	{
		/* Get physical */
		Addr_t Physical = AddressSpaceGetMap(AddressSpaceGetCurrent(), Buffer + (i * PAGE_SIZE));

		/* Set buffer */
		Td->Buffers[i] = EHCI_TD_BUFFER(Physical);

		/* Set extended? */
		if (sizeof(Addr_t) > 4)
			Td->ExtBuffers[i] = EHCI_TD_EXTBUFFER((uint64_t)Physical);
		else
			Td->ExtBuffers[i] = 0;

		/* Increase */
		Count += MIN(0x1000, LengthRemaining);

		/* Decrease remaining length */
		LengthRemaining -= MIN(0x1000, LengthRemaining);
    }

	/* Done */
	return Count;
}

/* This allocates & initializes 
 * a TD for a setup transaction 
 * this is only used for control
 * transactions */
EhciTransferDescriptor_t *EhciTdSetup(EhciEndpoint_t *Ep, UsbPacket_t *pPacket, void **TDBuffer)
{
	/* Vars */
	EhciTransferDescriptor_t *Td;
	uint16_t BufInfo = 0;
	void *Buffer;

	/* Allocate a Td */
	Td = EhciAllocateTd(Ep);

	/* Grab a Buffer */
	Buffer = EhciAllocateBuffers(Ep, sizeof(UsbPacket_t), &BufInfo);

	/* Invalidate Links */
	Td->Link = EHCI_LINK_END;
	Td->AlternativeLink = EHCI_LINK_END;

	/* Set Status */
	Td->Status = EHCI_TD_ACTIVE;

	/* Setup Token */
	Td->Token = EHCI_TD_SETUP;
	Td->Token |= EHCI_TD_ERRCOUNT;

	/* Setup Length */
	Td->Length = EHCI_TD_LENGTH((uint16_t)EhciTdFill(Td, (Addr_t)Buffer, sizeof(UsbPacket_t)));

	/* Copy data to TD buffer */
	*TDBuffer = Buffer;
	memcpy(Buffer, (void*)pPacket, sizeof(UsbPacket_t));

	/* Store information 
	 * about the buffer allocation */
	Td->HcdFlags |= EHCI_TD_IBUF(BufInfo);
	Td->HcdFlags |= EHCI_TD_JBUF((BufInfo >> 8));

	/* Done */
	return Td;
}

/* This allocates & initializes
* a TD for an i/o transaction 
* and is used for control, bulk 
* and interrupt */
EhciTransferDescriptor_t *EhciTdIo(EhciController_t *Controller, 
	EhciEndpoint_t *Ep, UsbHcRequest_t *Request, uint32_t PId,
	size_t Length, void **TDBuffer)
{
	/* Vars */
	EhciTransferDescriptor_t *Td = NULL;
	uint16_t BufInfo = 0;
	void *Buffer;

	/* Allocate a Td */
	Td = EhciAllocateTd(Ep);

	/* Sanity */
	Buffer = EhciAllocateBuffers(Ep, Length, &BufInfo);

	/* Invalidate Links */
	Td->Link = EHCI_LINK_END;
	Td->AlternativeLink = EHCI_LINK_END;

	/* Short packet not ok? */
	//if (Request->Flags & USB_SHORT_NOT_OK && PId == EHCI_TD_IN)
	//	Td->AlternativeLink = Controller->TdAsync->PhysicalAddress;
	_CRT_UNUSED(Controller);

	/* Set Status */
	Td->Status = EHCI_TD_ACTIVE;

	/* Setup Token */
	Td->Token = (uint8_t)(PId & 0x3);
	Td->Token |= EHCI_TD_ERRCOUNT;

	/* Setup Length */
	Td->Length = EHCI_TD_LENGTH((uint16_t)EhciTdFill(Td, (Addr_t)Buffer, Length));

	/* Toggle? */
	if (Request->Endpoint->Toggle)
		Td->Length |= EHCI_TD_TOGGLE;

	/* Calculate next toggle 
	 * if transaction spans multiple transfers */
	if (Length > 0
		&& !(DIVUP(Length, Request->Endpoint->MaxPacketSize) % 2))
		Request->Endpoint->Toggle ^= 0x1;

	/* Store buffer */
	*TDBuffer = Buffer;

	/* Store information
	* about the buffer allocation */
	Td->HcdFlags |= EHCI_TD_IBUF(BufInfo);
	Td->HcdFlags |= EHCI_TD_JBUF((BufInfo >> 8));

	/* Done */
	return Td;
}

/* Restarts an interrupt QH 
 * by resetting it to it's start state */
void EhciRestartQh(EhciController_t *Controller, UsbHcRequest_t *Request)
{
	/* Get transaction list */
	UsbHcTransaction_t *tList = (UsbHcTransaction_t*)Request->Transactions;
	EhciQueueHead_t *Qh = (EhciQueueHead_t*)Request->Data;
	EhciTransferDescriptor_t *Td = NULL, *TdCopy = NULL;

	/* For now */
	_CRT_UNUSED(Controller);

	/* Iterate and reset 
	 * Switch toggles if necessary ? */
	while (tList)
	{
		/* Cast TD(s) */
		TdCopy = (EhciTransferDescriptor_t*)tList->TransferDescriptorCopy;
		Td = (EhciTransferDescriptor_t*)tList->TransferDescriptor;

		/* Let's see */
		if (tList->Length != 0
			&& Td->Token & EHCI_TD_IN)
			memcpy(tList->Buffer, tList->TransferBuffer, tList->Length);

		/* Update Toggles */
		TdCopy->Length &= ~(EHCI_TD_TOGGLE);
		if (Td->Length & EHCI_TD_TOGGLE)
			TdCopy->Length |= EHCI_TD_TOGGLE;

		/* Reset */
		memcpy(Td, TdCopy, sizeof(EhciTransferDescriptor_t));

		/* Get next link */
		tList = tList->Link;
	}

	/* Set Qh to point to first */
	Td = (EhciTransferDescriptor_t*)Request->Transactions->TransferDescriptor;

	/* Zero out overlay (BUT KEEP TOGGLE???) */
	memset(&Qh->Overlay, 0, sizeof(EhciQueueHeadOverlay_t));

	/* Set pointers accordingly */
	Qh->Overlay.NextTD = Td->PhysicalAddress;
	Qh->Overlay.NextAlternativeTD = EHCI_LINK_END;
}

/* Scans a QH for 
 * completion or error 
 * returns non-zero if it 
 * has been touched */
int EhciScanQh(EhciController_t *Controller, UsbHcRequest_t *Request)
{
	/* Get transaction list */
	UsbHcTransaction_t *tList = (UsbHcTransaction_t*)Request->Transactions;

	/* State variables */
	int ShortTransfer = 0;
	int ErrorTransfer = 0;
	int Counter = 0;
	int ProcessQh = 0;

	/* For now... */
	_CRT_UNUSED(Controller);

	/* Loop through transactions */
	while (tList)
	{
		/* Increament */
		Counter++;

		/* Cast Td */
		EhciTransferDescriptor_t *Td =
			(EhciTransferDescriptor_t*)tList->TransferDescriptor;

		/* Get code */
		int CondCode = EhciConditionCodeToIndex(Request->Speed == HighSpeed ? Td->Status & 0xFC : Td->Status);
		int BytesLeft = Td->Length & 0x7FFF;

		/* Sanity first */
		if (Td->Status & EHCI_TD_ACTIVE) {

			/* If it's not the first TD, skip */
			if (Counter > 1
				&& (ShortTransfer || ErrorTransfer))
				ProcessQh = (ErrorTransfer == 1) ? 2 : 1;
			else
				ProcessQh = 0; /* No, only partially done without any errs */

			/* Break */
			break;
		}

		/* Set for processing per default */
		ProcessQh = 1;

		/* TD is not active
		* this means it's been processed */
		if (BytesLeft > 0) {
			ShortTransfer = 1;
		}

		/* Error Transfer ? */
		if (CondCode != 0) {
			ErrorTransfer = 1;
		}

		/* Get next transaction */
		tList = tList->Link;
	}

	/* Sanity */
	if (ErrorTransfer)
		ProcessQh = 2;

	/* Done */
	return ProcessQh;
}

/* EhciProcessTransfers
 * For transaction progress this involves done/error transfers */
void
EhciProcessTransfers(
	_In_ EhciController_t *Controller)
{
	/* Transaction is completed / Failed */
	List_t *Transactions = (List_t*)Controller->TransactionList;

	/* Get transactions in progress and find the offender */
	foreach(Node, Transactions)
	{
		/* Cast UsbRequest */
		UsbHcRequest_t *HcRequest = (UsbHcRequest_t*)Node->Data;
		int Processed = 0;

		/* Scan */
		if (HcRequest->Type != IsochronousTransfer)
			Processed = EhciScanQh(Controller, HcRequest);
		else
			;

		/* If it is to be processed, wake or process */
		if (Processed) {
			if (HcRequest->Type == InterruptTransfer) 
			{
				/* Restart the Qh */
				EhciRestartQh(Controller, HcRequest);

				/* Access Callback */
				if (HcRequest->Callback != NULL)
					HcRequest->Callback->Callback(HcRequest->Callback->Args,
					Processed == 2 ? TransferStalled : TransferFinished);

				/* Renew data in out transfers */
				UsbHcTransaction_t *tList = (UsbHcTransaction_t*)HcRequest->Transactions;

				/* Iterate and reset */
				while (tList)
				{
					/* Cast TD(s) */
					EhciTransferDescriptor_t *Td = 
						(EhciTransferDescriptor_t*)tList->TransferDescriptor;

					/* Let's see */
					if (tList->Length != 0
						&& Td->Token & EHCI_TD_OUT)
						memcpy(tList->TransferBuffer, tList->Buffer, tList->Length);

					/* Get next link */
					tList = tList->Link;
				}
			}
			else
				SchedulerWakeupOneThread((Addr_t*)HcRequest->Data);
		}
	}
}

/* EhciProcessDoorBell
 * This makes sure to schedule and/or unschedule transfers */
void
EhciProcessDoorBell(
	_In_ EhciController_t *Controller)
{
	/* Vars */
	ListNode_t *Node = NULL;

Scan:
	/* Reset the rescan */
	Controller->BellReScan = 0;

	/* Iterate transactions */
	_foreach(Node, ((List_t*)Controller->TransactionList))
	{
		/* Cast */
		UsbHcRequest_t *Request = (UsbHcRequest_t*)Node->Data;

		/* Get transaction type */
		if (Request->Type == ControlTransfer
			|| Request->Type == BulkTransfer)
		{
			/* Cast Qh */
			EhciQueueHead_t *Qh = (EhciQueueHead_t*)Request->Data;

			/* Has it asked to be unscheduled? */
			if (Qh->HcdFlags & EHCI_QH_UNSCHEDULE) {
				Qh->HcdFlags &= ~(EHCI_QH_UNSCHEDULE);
				SchedulerWakeupOneThread((Addr_t*)Qh);
			}
		}
	}

	/* If someone has rung the bell while 
	 * the door was opened, we should not close the door yet */
	if (Controller->BellReScan != 0)
		goto Scan;

	/* Bell is no longer ringing */
	Controller->BellIsRinging = 0;
}

/* Re-enable warnings */
#ifdef _MSC_VER
#pragma warning(default:4127)
#endif
