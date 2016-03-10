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

/* Initialize the Periodic Scheduler */
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
void EhciEndpointSetup(void *cData, UsbHcEndpoint_t *Endpoint)
{
	/* Cast */
	EhciController_t *Controller = (EhciController_t*)cData;
	Addr_t BufAddr = 0, BufAddrMax = 0;
	Addr_t Pool, PoolPhys;
	size_t i;

	/* Allocate a structure */
	EhciEndpoint_t *oEp = (EhciEndpoint_t*)kmalloc(sizeof(EhciEndpoint_t));

	/* Construct the lock */
	SpinlockReset(&oEp->Lock);

	/* Woah */
	_CRT_UNUSED(Controller);

	/* Now, we want to allocate some TD's
	* but it largely depends on what kind of endpoint this is */
	if (Endpoint->Type == EndpointControl)
		oEp->TdsAllocated = EHCI_ENDPOINT_MIN_ALLOCATED;
	else if (Endpoint->Type == EndpointBulk)
	{
		/* Depends on the maximum transfer */
		oEp->TdsAllocated = DEVICEMANAGER_MAX_IO_SIZE / Endpoint->MaxPacketSize;

		/* Take in account control packets and other stuff */
		oEp->TdsAllocated += EHCI_ENDPOINT_MIN_ALLOCATED;
	}
	else
	{
		/* We handle interrupt & iso dynamically
		* we don't predetermine their sizes */
		oEp->TdsAllocated = 0;
		Endpoint->AttachedData = oEp;
		return;
	}

	/* Now, we do the actual allocation */
	oEp->TDPool = (EhciTransferDescriptor_t**)kmalloc(sizeof(EhciTransferDescriptor_t*) * oEp->TdsAllocated);
	oEp->TDPoolBuffers = (Addr_t**)kmalloc(sizeof(Addr_t*) * oEp->TdsAllocated);
	oEp->TDPoolPhysical = (Addr_t*)kmalloc(sizeof(Addr_t) * oEp->TdsAllocated);

	/* Allocate a TD block */
	Pool = (Addr_t)kmalloc((sizeof(EhciTransferDescriptor_t) * oEp->TdsAllocated) + EHCI_STRUCT_ALIGN);
	Pool = ALIGN(Pool, EHCI_STRUCT_ALIGN, 1);
	PoolPhys = AddressSpaceGetMap(AddressSpaceGetCurrent(), Pool);

	/* Allocate buffers */
	BufAddr = (Addr_t)kmalloc_a(PAGE_SIZE);
	BufAddrMax = BufAddr + PAGE_SIZE - 1;

	/* Memset it */
	memset((void*)Pool, 0, sizeof(EhciTransferDescriptor_t) * oEp->TdsAllocated);

	/* Iterate it */
	for (i = 0; i < oEp->TdsAllocated; i++)
	{
		/* Set */
		oEp->TDPool[i] = (EhciTransferDescriptor_t*)Pool;
		oEp->TDPoolPhysical[i] = PoolPhys;

		/* Allocate another page? */
		if (BufAddr > BufAddrMax)
		{
			BufAddr = (Addr_t)kmalloc_a(PAGE_SIZE);
			BufAddrMax = BufAddr + PAGE_SIZE - 1;
		}

		/* Setup Buffer */
		oEp->TDPoolBuffers[i] = (Addr_t*)BufAddr;

		/* Increase */
		Pool += sizeof(EhciTransferDescriptor_t);
		PoolPhys += sizeof(EhciTransferDescriptor_t);
		BufAddr += Endpoint->MaxPacketSize;
	}

	/* Done! Save */
	Endpoint->AttachedData = oEp;
}

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
		/* Vars */
		EhciTransferDescriptor_t *oTd = oEp->TDPool[0];
		size_t i;

		/* Let's free all those resources */
		for (i = 0; i < oEp->TdsAllocated; i++)
		{
			/* free buffer */
			kfree(oEp->TDPoolBuffers[i]);
		}

		/* Free blocks */
		kfree(oTd);
		kfree(oEp->TDPoolBuffers);
		kfree(oEp->TDPoolPhysical);
		kfree(oEp->TDPool);
	}

	/* Free the descriptor */
	kfree(oEp);
}

/* Queue Functions */
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
