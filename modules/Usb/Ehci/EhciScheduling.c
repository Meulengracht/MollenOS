/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS USB EHCI Controller Driver
*/

/* Includes */
#include <Module.h>
#include "Ehci.h"

/* Additional Includes */
#include <DeviceManager.h>
#include <UsbCore.h>
#include <Scheduler.h>
#include <Heap.h>
#include <Timers.h>
#include <Log.h>

/* CLib */
#include <assert.h>
#include <string.h>

/* Globals */
const char *EhciErrorMessages[] =
{
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

/* Initialize the scheduling structures
 * that is used for the async scheduler code
 * but also the periodic scheduler code */
void EhciInitQueues(EhciController_t *Controller)
{
	/* Vars */
	Addr_t pSpace = 0, Phys = 0;
	size_t i;

	/* The first thing we want to do is 
	 * to determine the size of the frame list */
	if (Controller->CParameters & EHCI_CPARAM_VARIABLEFRAMELIST)
		Controller->FLength = 256; /* Default to shortest list (not 32 frames though) */
	else
		Controller->FLength = 1024;

	/* Allocate the frame list */
	Controller->FrameList = (uint32_t*)AddressSpaceMap(AddressSpaceGetCurrent(), 
		0, (Controller->FLength * sizeof(uint32_t)), ADDRESS_SPACE_FLAG_LOWMEM);

	/* Allocate the virtual copy list */
	Controller->VirtualList = (uint32_t*)kmalloc(sizeof(uint32_t*) * Controller->FLength);

	/* Instantiate them all to nothing */
	for (i = 0; i < Controller->FLength; i++)
		Controller->VirtualList[i] = Controller->FrameList[i] = EHCI_LINK_END;

	/* Allocate the Qh Pool */
	pSpace = (Addr_t)kmalloc((sizeof(EhciQueueHead_t) * EHCI_POOL_NUM_QH) + EHCI_STRUCT_ALIGN);

	/* Align with roundup */
	pSpace = ALIGN(pSpace, EHCI_STRUCT_ALIGN, 1);

	/* Zero it out */
	memset((void*)pSpace, 0, (sizeof(EhciQueueHead_t) * EHCI_POOL_NUM_QH));

	/* Get physical */
	Phys = AddressSpaceGetMap(AddressSpaceGetCurrent(), pSpace);

	/* Initialise ED Pool */
	for (i = 0; i < EHCI_POOL_NUM_QH; i++)
	{
		/* Set */
		Controller->QhPool[i] = (EhciQueueHead_t*)pSpace;
		Controller->QhPool[i]->PhysicalAddress = Phys;

		/* Increament */
		pSpace += sizeof(EhciQueueHead_t);
		Phys += sizeof(EhciQueueHead_t);
	}

	/* Initialize Dummy */
	Controller->QhPool[EHCI_POOL_QH_NULL]->Overlay.NextTD = EHCI_LINK_END;
	Controller->QhPool[EHCI_POOL_QH_NULL]->Overlay.NextAlternativeTD = EHCI_LINK_END;
	Controller->QhPool[EHCI_POOL_QH_NULL]->LinkPointer = EHCI_LINK_END;
	Controller->QhPool[EHCI_POOL_QH_NULL]->HcdFlags = EHCI_QH_ALLOCATED;

	/* Allocate the Async Dummy */
	pSpace = (Addr_t)kmalloc(sizeof(EhciTransferDescriptor_t) + EHCI_STRUCT_ALIGN);
	pSpace = ALIGN(pSpace, EHCI_STRUCT_ALIGN, 1);
	Controller->TdAsync = (EhciTransferDescriptor_t*)pSpace;

	/* Initialize the Async Dummy */
	Controller->TdAsync->Status = EHCI_TD_HALTED;
	Controller->TdAsync->Link = EHCI_LINK_END;
	Controller->TdAsync->AlternativeLink = EHCI_LINK_END;

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

	/* Allocate Transaction List */
	Controller->TransactionList = list_create(LIST_SAFE);

	/* Write addresses */
	Controller->OpRegisters->PeriodicListAddr = 
		(uint32_t)Controller->FrameList;
	Controller->OpRegisters->AsyncListAddress = 
		(uint32_t)Controller->QhPool[EHCI_POOL_QH_ASYNC]->PhysicalAddress | EHCI_LINK_QH;
}

/* Endpoint Functions */

/* Preallocates resources for an endpoint 
 * to speed up buffer and td-allocations 
 * for control and bulk endpoints */
void EhciEndpointSetup(void *cData, UsbHcEndpoint_t *Endpoint)
{
	/* Cast */
	EhciController_t *Controller = (EhciController_t*)cData;
	Addr_t Pool, PoolPhys;
	size_t i;

	/* Allocate a structure */
	EhciEndpoint_t *oEp = (EhciEndpoint_t*)kmalloc(sizeof(EhciEndpoint_t));
	
	/* Reset ep */
	memset(oEp, 0, sizeof(EhciEndpoint_t));

	/* Construct the lock */
	SpinlockReset(&oEp->Lock);

	/* Woah */
	_CRT_UNUSED(Controller);

	/* Sanity */
	if (Endpoint->Type == EndpointInterrupt
		|| Endpoint->Type == EndpointIsochronous)
	{
		/* We handle interrupt & iso dynamically
		* we don't predetermine their sizes */
		Endpoint->AttachedData = oEp;

		/* Done */
		return;
	}

	/* Set default - We should never need more 
	 * than 15 td's ( 15 x 20k is a lot .. )*/
	oEp->TdsAllocated = EHCI_POOL_TD_SIZE;

	/* How many buffers should we allocate? */
	i = DIVUP(DEVICEMANAGER_MAX_IO_SIZE, PAGE_SIZE);

	/* Allow a 'fail-safe' amount of buffer space */
	i += EHCI_POOL_BUFFER_MIN;
	oEp->BuffersAllocated = i;

	/* Allocate the arrays */
	oEp->TDPool = (EhciTransferDescriptor_t**)
		kmalloc(sizeof(EhciTransferDescriptor_t*) * oEp->TdsAllocated);
	oEp->BufferPool = (Addr_t**)kmalloc(sizeof(Addr_t*) * oEp->TdsAllocated);
	oEp->BufferPoolStatus = (int*)kmalloc(sizeof(int) * oEp->TdsAllocated);

	/* Allocate a block of Td's */
	Pool = (Addr_t)kmalloc((sizeof(EhciTransferDescriptor_t) * oEp->TdsAllocated) + EHCI_STRUCT_ALIGN);
	Pool = ALIGN(Pool, EHCI_STRUCT_ALIGN, 1);
	PoolPhys = AddressSpaceGetMap(AddressSpaceGetCurrent(), Pool);

	/* Memset it */
	memset((void*)Pool, 0, sizeof(EhciTransferDescriptor_t) * oEp->TdsAllocated);

	/* Iterate it */
	for (i = 0; i < oEp->TdsAllocated; i++)
	{
		/* Set */
		oEp->TDPool[i] = (EhciTransferDescriptor_t*)Pool;
		oEp->TDPool[i]->PhysicalAddress = PoolPhys;

		/* Increase */
		Pool += sizeof(EhciTransferDescriptor_t);
		PoolPhys += sizeof(EhciTransferDescriptor_t);
	}

	/* Allocate buffers */
	Pool = (Addr_t)kmalloc_a(PAGE_SIZE * oEp->BuffersAllocated);

	/* Iterate */
	for (i = 0; i < oEp->BuffersAllocated; i++)
	{
		/* Set */
		oEp->BufferPool[i] = (Addr_t*)Pool;
		oEp->BufferPoolStatus[i] = 0;

		/* Increase */
		Pool += PAGE_SIZE;
	}

	/* Done! Save */
	Endpoint->AttachedData = oEp;
}

/* Frees the preallocated resources */
void EhciEndpointDestroy(void *cData, UsbHcEndpoint_t *Endpoint)
{
	/* Cast */
	EhciController_t *Controller = (EhciController_t*)cData;
	EhciEndpoint_t *oEp = (EhciEndpoint_t*)Endpoint->AttachedData;

	/* Sanity */
	if (oEp == NULL)
		return;

	/* Woah */
	_CRT_UNUSED(Controller);

	/* Sanity */
	if (oEp->TdsAllocated != 0)
	{
		/* Get a pointer to first */
		EhciTransferDescriptor_t *oTd = oEp->TDPool[0];

		/* Free td-block & td-array */
		kfree(oTd);
		kfree(oEp->TDPool);
	}

	/* Sanity */
	if (oEp->BuffersAllocated != 0)
	{
		/* Get a pointer to first */
		Addr_t *oBuffer = oEp->BufferPool[0];

		/* Free block and arrays */
		kfree(oBuffer);
		kfree(oEp->BufferPool);
		kfree(oEp->BufferPoolStatus);
	}

	/* Free the descriptor */
	kfree(oEp);
}

/* Helpers */
int EhciConditionCodeToIndex(uint32_t ConditionCode)
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
	SchedulerSleepThread(Data);
	IThreadYield();
}

/* Link Functions */

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
 * and every other Qh->Period */
void EhciLinkPeriodicQh(EhciController_t *Controller, EhciQueueHead_t *Qh)
{
	/* Vars */
	size_t Period = Qh->Period;
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
			if (Qh->Period > This.Qh->Period)
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
	uint32_t Bandwidth;

	/* Calculate Bandwidth */
	Qh->Bandwidth = (uint16_t)
		NS_TO_US(UsbCalculateBandwidth(Request->Speed,
		Request->Endpoint->Direction, Request->Type,
		TransactionsPerFrame * Request->Endpoint->MaxPacketSize));
	Qh->Interval = (uint16_t)Request->Endpoint->Interval;

	if (Qh->Interval > 1
		&& Qh->Interval < 8) {
		Qh->Interval = 1;
	}
	else if (Qh->Interval >(Controller->FLength << 3)) {
		Qh->Interval = (uint16_t)(Controller->FLength << 3);
	}

	/* Calculate period */
	Qh->Period = Qh->Interval >> 3;

	/* Get bandwidth period */
	Bandwidth = MIN(EHCI_BANDWIDTH_PHASES, 1 << (Request->Endpoint->Interval - 1));

	/* Allow the modified Interval to override */
	Qh->sMicroPeriod = (uint8_t)MIN(Qh->Interval, Bandwidth);
	Qh->sPeriod = (Qh->sMicroPeriod >> 3);
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

		/* Memset */
		memset(Buffer, 0, PAGE_SIZE * NumPages);
	}

	/* Release */
	SpinlockRelease(&Ep->Lock);

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
	Addr_t Physical = 0;
	size_t LengthRemaining = Length;
	size_t Count = 0;
	int i;

	/* Sanity */
	if (Length == 0)
		return 0;

	/* Get physical */
	Physical = AddressSpaceGetMap(AddressSpaceGetCurrent(), Buffer);

	/* Iterate */
	for (i = 0; LengthRemaining > 0 && i < 5; i++)
	{
		/* Set buffer */
		Td->Buffers[i] = EHCI_TD_BUFFER(Physical + Count);

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


/* Bandwidth Functions 
 * --------------------
 * Most of this bandwidth 
 * calculation code is 
 * taken from linux's source */

/* This code validates an interrupt-QH 
 * and makes sure there is enough room 
 * for it's required period */
int EhciBandwidthValidateQh(EhciController_t *Controller,
	EhciQueueHead_t *Qh, size_t Frame, size_t MicroFrame)
{
	/* If we require more than 7 microframes
	* we need FSTN support */
	if (MicroFrame >= 8)
		return -1;

	/* Calculate the maximum microseconds
	* that may already be allocated */
	int MaxBandwidth = 100 - Qh->Bandwidth;

	/* Iterate and check */
	for (MicroFrame += (Frame << 3);
		MicroFrame < EHCI_BANDWIDTH_PHASES;
		MicroFrame += MaxBandwidth) {
		if (Controller->Bandwidth[MicroFrame] > MaxBandwidth)
			return 0;
	}

	/* Not valid */
	return -1;
}

/* This function either allocates or deallocates 
 * bandwidth in the controller for specified frames
 * and microframes */
void EhciControlBandwidth(EhciController_t *Controller,
	EhciQueueHead_t *Qh, int Allocate)
{
	/* Vars */
	size_t sMicroFrame;
	size_t i;
	int Bandwidth = Qh->Bandwidth;

	/* Get micro-frame */
	sMicroFrame = 0;// qh->ps.bw_phase << 3;

	/* Deallocate? */
	if (Allocate == 0) {
		Bandwidth = -Bandwidth;
	}

	/* Iterate and allocate/deallocate */
	for (i = sMicroFrame /*+ qh->ps.phase_uf */; 
		i < EHCI_BANDWIDTH_PHASES; i += Qh->sMicroPeriod)
		Controller->Bandwidth[i] += Bandwidth;
}

/* Transaction Functions */

/* This one prepaires a QH */
void EhciTransactionInit(void *cData, UsbHcRequest_t *Request)
{
	/* Vars */
	EhciController_t *Controller = (EhciController_t*)cData;
	EhciQueueHead_t *Qh = NULL;

	/* We handle Isochronous transfers a bit different */
	if (Request->Type != IsochronousTransfer)
	{
		/* Allocate a QH */
		Qh = EhciAllocateQh(Controller, Request->Type);

		/* Calculate bus time */
		if (Request->Type == InterruptTransfer)
			EhciInititalizeQh(Controller, Request, Qh);
		
		/* Initialize the Qh already */
		Qh->Flags = EHCI_QH_DEVADDR(Request->Device->Address);
		Qh->Flags |= EHCI_QH_EPADDR(Request->Endpoint->Address);
		Qh->Flags |= EHCI_QH_DTC;

		/* The thing with maxlength is 
		 * that it needs to be MIN(TransferLength, MPS) */
		Qh->Flags |= EHCI_QH_MAXLENGTH(Request->Endpoint->MaxPacketSize);

		/* Now, set additionals depending on speed */
		if (Request->Speed == LowSpeed
			|| Request->Speed == FullSpeed)
		{
			/* Control? */
			if (Request->Type == ControlTransfer)
				Qh->Flags |= EHCI_QH_CONTROLEP;

			/* On low-speed, set this bit */
			if (Request->Speed == LowSpeed)
				Qh->Flags |= EHCI_QH_LOWSPEED;

			/* Set nak-throttle to 0 */
			Qh->Flags |= EHCI_QH_RL(0);

			/* We need to fill the TT's hub-addr
			 * and port-addr */

			/* Set multiplier */
			Qh->State = EHCI_QH_MULTIPLIER(1);
		}
		else
		{
			/* High speed device, no transaction translator */
			Qh->Flags |= EHCI_QH_HIGHSPEED;

			/* Set nak-throttle to 4 if control or bulk */
			if (Request->Type == ControlTransfer
				|| Request->Type == BulkTransfer)
				Qh->Flags |= EHCI_QH_RL(4);
			else
				Qh->Flags |= EHCI_QH_RL(0);

			/* Set multiplier */
			if (Request->Type == InterruptTransfer)
				Qh->State = EHCI_QH_MULTIPLIER(Request->Endpoint->Bandwidth);
			else
				Qh->State = EHCI_QH_MULTIPLIER(1);
		}

		/* Store */
		Request->Data = Qh;
	}
	else
	{
		/* Isochronous Transfer */
	}
	
	/* Set as not Completed for start */
	Request->Status = TransferNotProcessed;
}

/* This function prepares an Td with 
 * the token setup, only used for control
 * endpoints. */
UsbHcTransaction_t *EhciTransactionSetup(void *cData, UsbHcRequest_t *Request, UsbPacket_t *Packet)
{
	/* Vars */
	EhciController_t *Controller = (EhciController_t*)cData;
	UsbHcTransaction_t *Transaction;

	/* Unused */
	_CRT_UNUSED(Controller);

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	memset(Transaction, 0, sizeof(UsbHcTransaction_t));

	/* Create the Td */
	Transaction->TransferDescriptor = (void*)EhciTdSetup(Request->Endpoint->AttachedData,
		Packet, &Transaction->TransferBuffer);

	/* Done */
	return Transaction;
}

/* This function prepares an Td with the 
 * in-token and is used for control, bulk
 * interrupt and isochronous transfers */
UsbHcTransaction_t *EhciTransactionIn(void *cData, UsbHcRequest_t *Request, void *Buffer, size_t Length)
{
	/* Vars */
	EhciController_t *Controller = (EhciController_t*)cData;
	UsbHcTransaction_t *Transaction;

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	memset(Transaction, 0, sizeof(UsbHcTransaction_t));
	
	/* Set Vars */
	Transaction->Buffer = Buffer;
	Transaction->Length = Length;

	/* Setup Td */
	Transaction->TransferDescriptor = (void*)EhciTdIo(Controller, Request->Endpoint->AttachedData,
		Request, EHCI_TD_IN, Length, &Transaction->TransferBuffer);

	/* If previous Transaction */
	if (Request->Transactions != NULL)
	{
		EhciTransferDescriptor_t *PrevTd;
		UsbHcTransaction_t *cTrans = Request->Transactions;

		/* Skip to end */
		while (cTrans->Link)
			cTrans = cTrans->Link;

		/* Set next link to this newly allocated */
		PrevTd = (EhciTransferDescriptor_t*)cTrans->TransferDescriptor;
		PrevTd->Link = ((EhciTransferDescriptor_t*)Transaction->TransferDescriptor)->PhysicalAddress;
	}

	/* We might need a copy */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer)
	{
		/* Allocate TD */
		Transaction->TransferDescriptorCopy =
			(void*)EhciAllocateTd(Request->Endpoint->AttachedData);
	}

	/* Done */
	return Transaction;
}

/* This function prepares an Td with the
* out-token and is used for control, bulk
* interrupt and isochronous transfers */
UsbHcTransaction_t *EhciTransactionOut(void *cData, UsbHcRequest_t *Request, void *Buffer, size_t Length)
{
	/* Vars */
	EhciController_t *Controller = (EhciController_t*)cData;
	UsbHcTransaction_t *Transaction;

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	memset(Transaction, 0, sizeof(UsbHcTransaction_t));

	/* Set Vars */
	Transaction->Buffer = Buffer;
	Transaction->Length = Length;

	/* Setup Td */
	Transaction->TransferDescriptor = (void*)EhciTdIo(Controller, Request->Endpoint->AttachedData,
		Request, EHCI_TD_OUT, Length, &Transaction->TransferBuffer);

	/* Copy Data */
	if (Buffer != NULL && Length != 0)
		memcpy(Transaction->TransferBuffer, Buffer, Length);

	/* If previous Transaction */
	if (Request->Transactions != NULL)
	{
		EhciTransferDescriptor_t *PrevTd;
		UsbHcTransaction_t *cTrans = Request->Transactions;

		/* Skip to end */
		while (cTrans->Link)
			cTrans = cTrans->Link;

		/* Set next link to this newly allocated */
		PrevTd = (EhciTransferDescriptor_t*)cTrans->TransferDescriptor;
		PrevTd->Link = ((EhciTransferDescriptor_t*)Transaction->TransferDescriptor)->PhysicalAddress;
	}

	/* We might need a copy */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer)
	{
		/* Allocate TD */
		Transaction->TransferDescriptorCopy =
			(void*)EhciAllocateTd(Request->Endpoint->AttachedData);
	}

	/* Done */
	return Transaction;
}

/* This function takes care of actually 
 * getting a transaction scheduled 
 * and ready for execution */
void EhciTransactionSend(void *cData, UsbHcRequest_t *Request)
{
	/* Wuhu */
	UsbHcTransaction_t *Transaction = Request->Transactions;
	EhciController_t *Controller = (EhciController_t*)cData;
	UsbTransferStatus_t Completed = TransferFinished;

	EhciQueueHead_t *Qh = NULL;
	EhciTransferDescriptor_t *Td = NULL;

	uint32_t CondCode;

	/************************
	****** SETUP PHASE ******
	*************************/
	
	/* We need to handle this differnetly */
	if (Request->Type != IsochronousTransfer)
	{
		/* Cast */
		Qh = (EhciQueueHead_t*)Request->Data;

		/* Set as not Completed for start */
		Request->Status = TransferNotProcessed;

		/* Iterate and set last to INT */
		Transaction = Request->Transactions;
		while (Transaction->Link)
		{
#ifdef EHCI_DIAGNOSTICS
			Td = (EhciTransferDescriptor_t*)Transaction->TransferDescriptor;

			LogInformation("EHCI", "Td (Addr 0x%x) Token 0x%x, Status 0x%x, Length 0x%x, Buffer 0x%x, Link 0x%x",
				Td->PhysicalAddress, (uint32_t)Td->Token, (
				uint32_t)Td->Status, (uint32_t)Td->Length, Td->Buffers[0],
				Td->Link);
#endif
			/* Next */
			Transaction = Transaction->Link;

#ifdef EHCI_DIAGNOSTICS
			if (Transaction->Link == NULL)
			{
				Td = (EhciTransferDescriptor_t*)Transaction->TransferDescriptor;

				LogInformation("EHCI", "Td (Addr 0x%x) Token 0x%x, Status 0x%x, Length 0x%x, Buffer 0x%x, Link 0x%x",
					Td->PhysicalAddress, (uint32_t)Td->Token, (
					uint32_t)Td->Status, (uint32_t)Td->Length, Td->Buffers[0],
					Td->Link);
			}
#endif
		}

		/* Retrieve Td */
#ifndef EHCI_DIAGNOSTICS
		Td = (EhciTransferDescriptor_t*)Transaction->TransferDescriptor;
		Td->Token |= EHCI_TD_IOC;
#endif

		/* Set Qh to point to first */
		Td = (EhciTransferDescriptor_t*)Request->Transactions->TransferDescriptor;

		/* Zero out overlay */
		memset(&Qh->Overlay, 0, sizeof(EhciQueueHeadOverlay_t));

		/* Set pointers accordingly */
		Qh->Overlay.NextTD = Td->PhysicalAddress;
Qh->Overlay.NextAlternativeTD = EHCI_LINK_END;

#ifdef EHCI_DIAGNOSTICS
LogInformation("EHCI", "Qh Address 0x%x, Flags 0x%x, State 0x%x, Current 0x%x, Next 0x%x",
	Qh->PhysicalAddress, Qh->Flags, Qh->State, Qh->CurrentTD, Qh->Overlay.NextTD);
#endif
	}
	else
	{
		/* Setup Isoc */

	}

	/* Add this Transaction to list */
	list_append((list_t*)Controller->TransactionList, list_create_node(0, Request));

	/*************************
	**** LINKING PHASE ******
	*************************/

	/* Acquire Lock */
	SpinlockAcquire(&Controller->Lock);

	/* Async Scheduling? */
	if (Request->Type == ControlTransfer
		|| Request->Type == BulkTransfer)
	{
		/* Get links of current */
		Qh->LinkPointer = Controller->QhPool[EHCI_POOL_QH_ASYNC]->LinkPointer;
		Qh->LinkPointerVirtual = Controller->QhPool[EHCI_POOL_QH_ASYNC]->LinkPointerVirtual;

		/* Memory Barrier */
		MemoryBarrier();

		/* Insert at the start of queue */
		Controller->QhPool[EHCI_POOL_QH_ASYNC]->LinkPointerVirtual = (uint32_t)Qh;
		Controller->QhPool[EHCI_POOL_QH_ASYNC]->LinkPointer = Qh->PhysicalAddress | EHCI_LINK_QH;
	}
	else
	{
		/* Periodic Scheduling */
		if (Request->Type == InterruptTransfer)
		{
			/* Allocate Bandwidth */


			/* Link */
			EhciLinkPeriodicQh(Controller, Qh);
		}
		else
		{
			LogFatal("EHCI", "Scheduling peridoic");
			for (;;);
		}
	}

	/* Release */
	SpinlockRelease(&Controller->Lock);

	/* Sanity */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer)
		return;

#ifdef EHCI_DIAGNOSTICS
	/* Sleep */
	StallMs(5000);

	/* Inspect state of tds' and qh */
	LogInformation("EHCI", "Qh Address 0x%x, Flags 0x%x, State 0x%x, Current 0x%x, Next 0x%x\n",
		Qh->PhysicalAddress, Qh->Flags, Qh->State, Qh->CurrentTD, Qh->Overlay.NextTD);
#else
	/* Enable Async Scheduler */
	Controller->AsyncTransactions++;
	EhciEnableAsyncScheduler(Controller);

	/* Wait for interrupt */
	SchedulerSleepThread((Addr_t*)Request->Data);

	/* Yield */
	IThreadYield();
#endif

	/*************************
	*** VALIDATION PHASE ****
	*************************/

	/* Check Conditions */
	Transaction = Request->Transactions;
	while (Transaction)
	{
		/* Cast and get the transfer code */
		Td = (EhciTransferDescriptor_t*)Transaction->TransferDescriptor;
		CondCode = EhciConditionCodeToIndex(Request->Speed == HighSpeed ? Td->Status & 0xFC : Td->Status);

		/* Calculate length transferred */
		if (Transaction->Buffer != NULL
			&& Transaction->Length != 0) {
			size_t BytesRemaining = Td->Length & 0x7FFF;
			Transaction->ActualLength = Transaction->Length - BytesRemaining;
		}

#ifdef EHCI_DIAGNOSTICS
		LogInformation("EHCI", "Td (Addr 0x%x) Token 0x%x, Status 0x%x, Length 0x%x, Buffer 0x%x, Link 0x%x\n",
			Td->PhysicalAddress, (uint32_t)Td->Token,
			(uint32_t)Td->Status, (uint32_t)Td->Length, Td->Buffers[0],
			Td->Link);
#endif

		if (CondCode == 0 && Completed == TransferFinished)
			Completed = TransferFinished;
		else
		{
			if (CondCode == 4)
				Completed = TransferNotResponding;
			else if (CondCode == 5)
				Completed = TransferBabble;
			else if (CondCode == 6)
				Completed = TransferInvalidData;
			else if (CondCode == 7)
				Completed = TransferStalled;
			else {
				LogDebug("EHCI", "Error: 0x%x (%s)", CondCode, EhciErrorMessages[CondCode]);
				Completed = TransferInvalidData;
			}
			break;
		}

		/* Next */
		Transaction = Transaction->Link;
	}

	/* Update Status */
	Request->Status = Completed;

#ifdef EHCI_DIAGNOSTICS
	for (;;);
#endif
}

/* This one makes sure a transaction is 
 * unscheduled and cleaned up properly */
void EhciTransactionDestroy(void *cData, UsbHcRequest_t *Request)
{
	/* Cast */
	UsbHcTransaction_t *Transaction = Request->Transactions;
	EhciController_t *Controller = (EhciController_t*)cData;
	list_node_t *Node = NULL;

	/* We unlink and clean up based 
	 * on trasnaction type */
	if (Request->Type == ControlTransfer
		|| Request->Type == BulkTransfer)
	{
		/* Cast */
		EhciQueueHead_t *Qh = (EhciQueueHead_t*)Request->Data;
		EhciQueueHead_t *PrevQh = NULL;
		uint32_t Temp = 0;

		/* Get lock */
		SpinlockAcquire(&Controller->Lock);

		/* Step 1, unlink in memory */
		PrevQh = Controller->QhPool[EHCI_POOL_QH_ASYNC];
		while (PrevQh->LinkPointerVirtual != (uint32_t)Qh)
			PrevQh = (EhciQueueHead_t*)PrevQh->LinkPointerVirtual;

		/* Now make sure PrevQh link skips over */
		PrevQh->LinkPointer = Qh->LinkPointer;
		PrevQh->LinkPointerVirtual = Qh->LinkPointerVirtual;

		/* MemB */
		MemoryBarrier();

		/* Release lock */
		SpinlockRelease(&Controller->Lock);

		/* Mark Qh for unscheduling, 
		 * otherwise we won't get waked up */
		Qh->HcdFlags |= EHCI_QH_UNSCHEDULE;

		/* Now we have to force a doorbell */
		EhciRingDoorbell(Controller, (Addr_t*)Request->Data);

		/* Iterate and reset */
		while (Transaction)
		{
			/* Cast */
			EhciTransferDescriptor_t *Td =
				(EhciTransferDescriptor_t*)Transaction->TransferDescriptor;
			
			/* Free associated buffers */
			EhciDeallocateBuffers(Request->Endpoint->AttachedData, Td);

			/* Save the Td Physical before
			* we memset the strucutre */
			Temp = Td->PhysicalAddress;

			/* Memset */
			memset((void*)Td, 0, sizeof(EhciTransferDescriptor_t));

			/* Restore */
			Td->PhysicalAddress = Temp;

			/* Next */
			Transaction = Transaction->Link;
		}

		/* Save the Qh Physical before 
		 * we memset the strucutre */
		Temp = Qh->PhysicalAddress;

		/* Reset the ED */
		memset(Qh, 0, sizeof(EhciQueueHead_t));

		/* Restore */
		Qh->PhysicalAddress = Temp;

		/* Stop async scheduler 
		 * if there aren't anymore 
		 * transfers to process */
		Controller->AsyncTransactions--;

		/* Sanity */
		if (!Controller->AsyncTransactions)
			EhciDisableAsyncScheduler(Controller);
	}
	else
	{
		/* Interrupt & Isoc */
		if (Request->Type == InterruptTransfer)
		{
			/* Cast */
			EhciQueueHead_t *Qh = (EhciQueueHead_t*)Request->Data;

			/* Get lock */
			SpinlockAcquire(&Controller->Lock);

			/* Unlink */
			EhciUnlinkPeriodic(Controller, (Addr_t)Qh, Qh->Period, Qh->sFrame);

			/* Release Bandwidth */


			/* Release lock */
			SpinlockRelease(&Controller->Lock);

			/* Mark Qh for unscheduling,
			* otherwise we won't get waked up */
			Qh->HcdFlags |= EHCI_QH_UNSCHEDULE;

			/* Now we have to force a doorbell */
			EhciRingDoorbell(Controller, (Addr_t*)Request->Data);

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
		}
		else
		{
			/* Isochronous */
		}
	}

	/* Remove transaction from list */
	_foreach(Node, ((list_t*)Controller->TransactionList)) {
		if (Node->data == Request)
			break;
	}

	/* Sanity */
	if (Node != NULL) {
		list_remove_by_node((list_t*)Controller->TransactionList, Node);
		kfree(Node);
	}
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
				ProcessQh = 1;
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

	/* Done */
	return ProcessQh;
}

/* Process transfers 
 * for transaction progress
 * this involves done/error 
 * transfers */
void EhciProcessTransfers(EhciController_t *Controller)
{
	/* Transaction is completed / Failed */
	list_t *Transactions = (list_t*)Controller->TransactionList;

	/* Get transactions in progress and find the offender */
	foreach(Node, Transactions)
	{
		/* Cast UsbRequest */
		UsbHcRequest_t *HcRequest = (UsbHcRequest_t*)Node->data;
		int Processed = 0;

		/* Scan */
		if (HcRequest->Type != IsochronousTransfer)
			Processed = EhciScanQh(Controller, HcRequest);
		else
			;

		/* If it was processed, wake */
		if (Processed)
			SchedulerWakeupOneThread((Addr_t*)HcRequest->Data);
	}
}

/* Processes transfers 
 * This makes sure to schedule 
 * and/or unschedule transfers */
void EhciProcessDoorBell(EhciController_t *Controller)
{
	/* Vars */
	list_node_t *Node = NULL;

Scan:
	/* Reset the rescan */
	Controller->BellReScan = 0;

	/* Iterate transactions */
	_foreach(Node, ((list_t*)Controller->TransactionList))
	{
		/* Cast */
		UsbHcRequest_t *Request = (UsbHcRequest_t*)Node->data;

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

	/* If someone has 
	 * rung the bell while 
	 * the door was opened, we 
	 * should not close the door yet */
	if (Controller->BellReScan != 0)
		goto Scan;

	/* Bell is no longer ringing */
	Controller->BellIsRinging = 0;
}