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
#include <Pci.h>

/* CLib */
#include <string.h>

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

	/* Allocate the Async Dummy */
	pSpace = (Addr_t)kmalloc(sizeof(EhciTransferDescriptor_t) + EHCI_STRUCT_ALIGN);
	pSpace = ALIGN(pSpace, EHCI_STRUCT_ALIGN, 1);
	Controller->TdAsync = (EhciTransferDescriptor_t*)pSpace;

	/* Initialize the Async Dummy */
	Controller->TdAsync->Status = EHCI_TD_HALTED;
	Controller->TdAsync->Link = EHCI_LINK_END;
	Controller->TdAsync->AlternativeLink = EHCI_LINK_END;

	/* Initialize Async */
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->Flags = EHCI_QH_RECLAMATIONHEAD;
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->Overlay.Status = EHCI_TD_HALTED;
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->Overlay.NextTD = EHCI_LINK_END;
	Controller->QhPool[EHCI_POOL_QH_ASYNC]->Overlay.NextAlternativeTD =
		AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Controller->TdAsync);

	/* Allocate Transaction List */
	Controller->TransactionList = list_create(LIST_SAFE);

	/* Write addresses */
	Controller->OpRegisters->PeriodicListAddr = (uint32_t)Controller->FrameList;
	Controller->OpRegisters->AsyncListAddress = (uint32_t)Controller->QhPool[EHCI_POOL_QH_ASYNC]->PhysicalAddress;
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
	 * than 15 td's */
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
Addr_t *EhciAllocateBuffers(EhciEndpoint_t *Ep, size_t Length)
{
	/* Vars */
	Addr_t *Buffer = NULL;
	size_t i, j, NumPages = 0;

	/* Sanity */
	if (Length == 0)
		return NULL;

	/* How many pages to allocate? */
	NumPages = DIVUP(Length, PAGE_SIZE);

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

	/* Done! */
	return Buffer;
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
	void *Buffer;

	/* Allocate a Td */
	Td = EhciAllocateTd(Ep);

	/* Grab a Buffer */
	Buffer = EhciAllocateBuffers(Ep, sizeof(UsbPacket_t));

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
	void *Buffer;

	/* Allocate a Td */
	Td = EhciAllocateTd(Ep);

	/* Sanity */
	Buffer = EhciAllocateBuffers(Ep, Length);

	/* Invalidate Links */
	Td->Link = EHCI_LINK_END;

	/* Used on a short packet-ins */
	if (PId == EHCI_TD_IN)
		Td->AlternativeLink = Controller->TdAsync->PhysicalAddress;
	else
		Td->AlternativeLink = EHCI_LINK_END;

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

	/* Store buffer */
	*TDBuffer = Buffer;

	/* Done */
	return Td;
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
		{
			/* Get frame-count */
			uint32_t TransactionsPerFrame = Request->Endpoint->Bandwidth;
			uint32_t Bandwidth;

			/* Calculate Bandwidth */
			Qh->BusTime = (uint16_t)
				NS_TO_US(UsbCalculateBandwidth(Request->Speed, 
					Request->Endpoint->Direction, Request->Type, 
					TransactionsPerFrame * Request->Endpoint->MaxPacketSize));
			Qh->Interval = (uint16_t)Request->Endpoint->Interval;

			if (Qh->Interval > 1
				&& Qh->Interval < 8) {
				Qh->Interval = 1;
			}
			else if (Qh->Interval > (Controller->FLength << 3)) {
				Qh->Interval = (uint16_t)(Controller->FLength << 3);
			}

			/* Calculate period */
			Qh->Period = Qh->Interval >> 3;

			/* Get bandwidth period */
			Bandwidth = MIN(64, 1 << (Request->Endpoint->Interval - 1));

			/* Allow the modified Interval to override */
			//bw_uperiod = MIN(Qh->Interval, Bandwidth)
			//bw_period = (bw_uperiod >> 3)
		}
		
		/* Initialize the Qh already */
		Qh->Flags = EHCI_QH_DEVADDR(Request->Device->Address);
		Qh->Flags |= EHCI_QH_EPADDR(Request->Endpoint->Address);
		Qh->Flags |= EHCI_QH_DTC;
		Qh->Flags |= EHCI_QH_MAXLENGTH(Request->Endpoint->MaxPacketSize);

		/* Control? */
		if (Request->Type == ControlTransfer)
			Qh->Flags |= EHCI_QH_CONTROLEP;

		/* Now, set additionals depending on speed */
		if (Request->Speed == LowSpeed
			|| Request->Speed == FullSpeed)
		{
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
UsbHcTransaction_t *EhciTransactionSetup(void *cData, UsbHcRequest_t *Request)
{
	/* Vars */
	EhciController_t *Controller = (EhciController_t*)cData;
	UsbHcTransaction_t *Transaction;

	/* Unused */
	_CRT_UNUSED(Controller);

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	Transaction->IoBuffer = 0;
	Transaction->IoLength = 0;
	Transaction->Link = NULL;

	/* Create the Td */
	Transaction->TransferDescriptor = (void*)EhciTdSetup(Request->Endpoint->AttachedData,
		&Request->Packet, &Transaction->TransferBuffer);

	/* Done */
	return Transaction;
}

/* This function prepares an Td with the 
 * in-token and is used for control, bulk
 * interrupt and isochronous transfers */
UsbHcTransaction_t *EhciTransactionIn(void *cData, UsbHcRequest_t *Request)
{
	/* Vars */
	EhciController_t *Controller = (EhciController_t*)cData;
	UsbHcTransaction_t *Transaction;

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	Transaction->TransferDescriptorCopy = NULL;
	Transaction->IoBuffer = Request->IoBuffer;
	Transaction->IoLength = Request->IoLength;
	Transaction->Link = NULL;

	/* Setup Td */
	Transaction->TransferDescriptor = (void*)EhciTdIo(Controller, Request->Endpoint->AttachedData,
		Request, EHCI_TD_IN, Request->IoLength, &Transaction->TransferBuffer);

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
UsbHcTransaction_t *EhciTransactionOut(void *cData, UsbHcRequest_t *Request)
{
	/* Vars */
	EhciController_t *Controller = (EhciController_t*)cData;
	UsbHcTransaction_t *Transaction;

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	Transaction->TransferDescriptorCopy = NULL;
	Transaction->IoBuffer = 0;
	Transaction->IoLength = 0;
	Transaction->Link = NULL;

	/* Setup Td */
	Transaction->TransferDescriptor = (void*)EhciTdIo(Controller, Request->Endpoint->AttachedData,
		Request, EHCI_TD_OUT, Request->IoLength, &Transaction->TransferBuffer);

	/* Copy Data */
	if (Request->IoBuffer != NULL && Request->IoLength != 0)
		memcpy(Transaction->TransferBuffer, Request->IoBuffer, Request->IoLength);

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
