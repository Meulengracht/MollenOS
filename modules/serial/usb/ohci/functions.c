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
#include <os/thread.h>
#include <os/utils.h>
#include "ohci.h"

/* Includes
 * - Library */
#include <ds/list.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>


/* Endpoint Functions */
void OhciEndpointSetup(void *Controller, UsbHcEndpoint_t *Endpoint)
{
	/* Cast */
	OhciController_t *oCtrl = (OhciController_t*)Controller;
	Addr_t BufAddr = 0, BufAddrMax = 0;
	Addr_t Pool, PoolPhys;
	size_t i;

	/* Allocate a structure */
	OhciEndpoint_t *oEp = (OhciEndpoint_t*)kmalloc(sizeof(OhciEndpoint_t));

	/* Construct the lock */
	SpinlockReset(&oEp->Lock);

	/* Woah */
	_CRT_UNUSED(oCtrl);

	/* Now, we want to allocate some TD's 
	 * but it largely depends on what kind of endpoint this is */
	if (Endpoint->Type == EndpointControl)
		oEp->TdsAllocated = OHCI_ENDPOINT_MIN_ALLOCATED;
	else if (Endpoint->Type == EndpointBulk)
	{
		/* Depends on the maximum transfer */
		oEp->TdsAllocated = DEVICEMANAGER_MAX_IO_SIZE / Endpoint->MaxPacketSize;
		
		/* Take in account control packets and other stuff */
		oEp->TdsAllocated += OHCI_ENDPOINT_MIN_ALLOCATED;
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
	oEp->TDPool = (OhciGTransferDescriptor_t**)kmalloc(sizeof(OhciGTransferDescriptor_t*) * oEp->TdsAllocated);
	oEp->TDPoolBuffers = (Addr_t**)kmalloc(sizeof(Addr_t*) * oEp->TdsAllocated);
	oEp->TDPoolPhysical = (Addr_t*)kmalloc(sizeof(Addr_t) * oEp->TdsAllocated);

	/* Allocate a TD block */
	Pool = (Addr_t)kmalloc((sizeof(OhciGTransferDescriptor_t) * oEp->TdsAllocated) + OHCI_STRUCT_ALIGN);
	Pool = OhciAlign(Pool, OHCI_STRUCT_ALIGN_BITS, OHCI_STRUCT_ALIGN);
	PoolPhys = AddressSpaceGetMap(AddressSpaceGetCurrent(), Pool);

	/* Allocate buffers */
	BufAddr = (Addr_t)kmalloc_a(PAGE_SIZE);
	BufAddrMax = BufAddr + PAGE_SIZE - 1;

	/* Memset it */
	memset((void*)Pool, 0, sizeof(OhciGTransferDescriptor_t) * oEp->TdsAllocated);

	/* Iterate it */
	for (i = 0; i < oEp->TdsAllocated; i++)
	{
		/* Set */
		oEp->TDPool[i] = (OhciGTransferDescriptor_t*)Pool;
		oEp->TDPoolPhysical[i] = PoolPhys;

		/* Allocate another page? */
		if (BufAddr > BufAddrMax)
		{
			BufAddr = (Addr_t)kmalloc_a(PAGE_SIZE);
			BufAddrMax = BufAddr + PAGE_SIZE - 1;
		}

		/* Setup Buffer */
		oEp->TDPoolBuffers[i] = (Addr_t*)BufAddr;
		oEp->TDPool[i]->Cbp = AddressSpaceGetMap(AddressSpaceGetCurrent(), BufAddr);
		oEp->TDPool[i]->NextTD = 0x1;

		/* Increase */
		Pool += sizeof(OhciGTransferDescriptor_t);
		PoolPhys += sizeof(OhciGTransferDescriptor_t);
		BufAddr += Endpoint->MaxPacketSize;
	}

	/* Done! Save */
	Endpoint->AttachedData = oEp;
}

void OhciEndpointDestroy(void *Controller, UsbHcEndpoint_t *Endpoint)
{
	/* Cast */
	OhciController_t *oCtrl = (OhciController_t*)Controller;
	OhciEndpoint_t *oEp = (OhciEndpoint_t*)Endpoint->AttachedData;
	
	/* Sanity */
	if (oEp == NULL)
		return;

	/* Woah */
	_CRT_UNUSED(oCtrl);

	/* Sanity */
	if (oEp->TdsAllocated != 0)
	{
		/* Vars */
		OhciGTransferDescriptor_t *oTd = oEp->TDPool[0];
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


/* Transaction Functions */

/* This one prepaires an ED */
void OhciTransactionInit(void *Controller, UsbHcRequest_t *Request)
{
	/* Vars */
	OhciController_t *Ctrl = (OhciController_t*)Controller;
	OhciEndpointDescriptor_t *Ed = NULL;

	/* Allocate an EP */
	Ed = OhciAllocateEp(Ctrl, Request->Type);

	/* Mark it inactive for now */
	Ed->Flags |= OHCI_EP_SKIP;

	/* Calculate bus time */
	Ed->Bandwidth =
		(UsbCalculateBandwidth(Request->Speed, 
		Request->Endpoint->Direction, Request->Type, Request->Endpoint->MaxPacketSize) / 1000);
	Ed->Interval = Request->Endpoint->Interval;

	/* Store */
	Request->Data = Ed;

	/* Set as not Completed for start */
	Request->Status = TransferNotProcessed;
}

/* This one prepaires an setup Td */
UsbHcTransaction_t *OhciTransactionSetup(void *Controller, UsbHcRequest_t *Request, UsbPacket_t *Packet)
{
	/* Vars */
	OhciController_t *Ctrl = (OhciController_t*)Controller;
	UsbHcTransaction_t *Transaction;

	/* Unused */
	_CRT_UNUSED(Ctrl);

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	memset(Transaction, 0, sizeof(UsbHcTransaction_t));

	/* Create the Td */
	Transaction->TransferDescriptor = (void*)OhciTdSetup(Request->Endpoint->AttachedData, 
		Request->Type, Packet, &Transaction->TransferBuffer);

	/* Done */
	return Transaction;
}

/* This one prepaires an in Td */
UsbHcTransaction_t *OhciTransactionIn(void *Controller, UsbHcRequest_t *Request, void *Buffer, size_t Length)
{
	/* Vars */
	OhciController_t *Ctrl = (OhciController_t*)Controller;
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
	Transaction->TransferDescriptor = (void*)OhciTdIo(Request->Endpoint->AttachedData, 
		Request->Type, Request->Endpoint, OHCI_TD_PID_IN, Length,
		&Transaction->TransferBuffer);

	/* If previous Transaction */
	if (Request->Transactions != NULL)
	{
		OhciGTransferDescriptor_t *PrevTd;
		UsbHcTransaction_t *cTrans = Request->Transactions;

		while (cTrans->Link)
			cTrans = cTrans->Link;

		PrevTd = (OhciGTransferDescriptor_t*)cTrans->TransferDescriptor;
		PrevTd->NextTD = AddressSpaceGetMap(AddressSpaceGetCurrent(), 
			(VirtAddr_t)Transaction->TransferDescriptor);
	}

	/* We might need a copy */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer)
	{
		/* Allocate TD */
		Transaction->TransferDescriptorCopy = 
			(void*)OhciAllocateTd(Request->Endpoint->AttachedData, Request->Type);
	}

	/* Done */
	return Transaction;
}

/* This one prepaires an out Td */
UsbHcTransaction_t *OhciTransactionOut(void *Controller, UsbHcRequest_t *Request, void *Buffer, size_t Length)
{
	/* Vars */
	OhciController_t *Ctrl = (OhciController_t*)Controller;
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
	Transaction->TransferDescriptor = (void*)OhciTdIo(Request->Endpoint->AttachedData, 
		Request->Type, Request->Endpoint, OHCI_TD_PID_OUT, Length,
		&Transaction->TransferBuffer);

	/* Copy Data */
	if (Buffer != NULL && Length != 0)
		memcpy(Transaction->TransferBuffer, Buffer, Length);

	/* If previous Transaction */
	if (Request->Transactions != NULL)
	{
		OhciGTransferDescriptor_t *PrevTd;
		UsbHcTransaction_t *cTrans = Request->Transactions;

		while (cTrans->Link)
			cTrans = cTrans->Link;

		PrevTd = (OhciGTransferDescriptor_t*)cTrans->TransferDescriptor;
		PrevTd->NextTD = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Transaction->TransferDescriptor);
	}

	/* We might need a copy */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer)
	{
		/* Allocate TD */
		Transaction->TransferDescriptorCopy =
			(void*)OhciAllocateTd(Request->Endpoint->AttachedData, Request->Type);
	}

	/* Done */
	return Transaction;
}

/* This one queues the Transaction up for processing */
void OhciTransactionSend(void *Controller, UsbHcRequest_t *Request)
{
	/* Wuhu */
	UsbHcTransaction_t *Transaction = Request->Transactions;
	OhciController_t *Ctrl = (OhciController_t*)Controller;
	UsbTransferStatus_t Completed = TransferFinished;
	OhciEndpointDescriptor_t *Ep = NULL;
	OhciGTransferDescriptor_t *Td = NULL;
	DataKey_t Key;
	uint32_t CondCode;
	Addr_t EdAddress;

	/*************************
	****** SETUP PHASE ******
	*************************/

	/* Cast */
	Ep = (OhciEndpointDescriptor_t*)Request->Data;

	/* Get physical */
	EdAddress = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Ep);

	/* Set as not Completed for start */
	Request->Status = TransferNotProcessed;

	/* Add dummy Td to end
	 * But we have to keep the endpoint toggle */
	if (Request->Type != IsochronousTransfer) {
		CondCode = Request->Endpoint->Toggle;
		UsbTransactionOut(UsbGetHcd(Ctrl->HcdId), Request,
			(Request->Type == ControlTransfer) ? 1 : 0, NULL, 0);
		Request->Endpoint->Toggle = CondCode;
		CondCode = 0;
	}

	/* Iterate and set last to INT */
	Transaction = Request->Transactions;
	while (Transaction->Link
		&& Transaction->Link->Link)
	{
#ifdef _OHCI_DIAGNOSTICS_
		Td = (OhciGTransferDescriptor_t*)Transaction->TransferDescriptor;

		LogDebug("OHCI", "Td (Addr 0x%x) Flags 0x%x, Cbp 0x%x, BufferEnd 0x%x, Next Td 0x%x\n", 
			AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Td), Td->Flags, Td->Cbp, Td->BufferEnd, Td->NextTD);
#endif
		/* Next */
		Transaction = Transaction->Link;
	}

	/* Retrieve Td */
#ifndef _OHCI_DIAGNOSTICS_
	Td = (OhciGTransferDescriptor_t*)Transaction->TransferDescriptor;
	Td->Flags &= ~(OHCI_TD_NO_IOC);
#endif

	/* Initialize the allocated ED */
	OhciEpInit(Ep, Request->Transactions, Request->Type,
		Request->Device->Address, Request->Endpoint->Address,
		Request->Endpoint->MaxPacketSize, Request->Speed);

	/* Add this Transaction to list */
	Key.Value = 0;
	ListAppend((List_t*)Ctrl->TransactionList, ListCreateNode(Key, Key, Request));

	/* Remove Skip */
	Ep->Flags &= ~(OHCI_EP_SKIP);
	Ep->HeadPtr &= ~(0x1);

#ifdef _OHCI_DIAGNOSTICS_
	LogDebug("OHCI", "Ed Address 0x%x, Flags 0x%x, Ed Tail 0x%x, Ed Head 0x%x, Next 0x%x\n", 
		EdAddress, Ep->Flags, Ep->TailPtr, Ep->HeadPtr, Ep->NextED);
#endif

	/*************************
	**** LINKING PHASE ******
	*************************/
	
	/* Add them to list */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer) 
	{
		/* Interrupt & Isochronous */

		/* Update saved copies, now all is prepaired */
		Transaction = Request->Transactions;
		while (Transaction)
		{
			/* Do an exact copy */
			memcpy(Transaction->TransferDescriptorCopy, Transaction->TransferDescriptor,
				(Request->Type == InterruptTransfer) ? sizeof(OhciGTransferDescriptor_t) :
				sizeof(OhciITransferDescriptor_t));

			/* Next */
			Transaction = Transaction->Link;
		}
	}

	/* Mark for scheduling */
	Ep->HcdFlags |= OHCI_ED_SCHEDULE;

	/* Enable SOF, ED is not scheduled before */
	Ctrl->Registers->HcInterruptStatus = OHCI_INTR_SOF;
	Ctrl->Registers->HcInterruptEnable = OHCI_INTR_SOF;

	/* Sanity */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer)
		return;

	/* Wait for interrupt */
#ifdef _OHCI_DIAGNOSTICS_
	printf("Current Frame 0x%x (HCCA), HcFrameNumber 0x%x, HcFrameInterval 0x%x, HcFrameRemaining 0x%x\n", Ctrl->HCCA->CurrentFrame,
		Ctrl->Registers->HcFmNumber, Ctrl->Registers->HcFmInterval, Ctrl->Registers->HcFmRemaining);
	printf("Transaction sent.. waiting for reply..\n");
	StallMs(5000);
	printf("Current Frame 0x%x (HCCA), HcFrameNumber 0x%x, HcFrameInterval 0x%x, HcFrameRemaining 0x%x\n", Ctrl->HCCA->CurrentFrame,
		Ctrl->Registers->HcFmNumber, Ctrl->Registers->HcFmInterval, Ctrl->Registers->HcFmRemaining);
	printf("1. Current Control: 0x%x, Current Head: 0x%x\n",
		Ctrl->Registers->HcControlCurrentED, Ctrl->Registers->HcControlHeadED);
	StallMs(5000);
	printf("Current Frame 0x%x (HCCA), HcFrameNumber 0x%x, HcFrameInterval 0x%x, HcFrameRemaining 0x%x\n", Ctrl->HCCA->CurrentFrame,
		Ctrl->Registers->HcFmNumber, Ctrl->Registers->HcFmInterval, Ctrl->Registers->HcFmRemaining);
	printf("2. Current Control: 0x%x, Current Head: 0x%x\n",
		Ctrl->Registers->HcControlCurrentED, Ctrl->Registers->HcControlHeadED);
	printf("Current Control 0x%x, Current Cmd 0x%x\n",
		Ctrl->Registers->HcControl, Ctrl->Registers->HcCommandStatus);
#else
	/* Wait for interrupt */
	SchedulerSleepThread((Addr_t*)Request->Data, 5000);

	/* Yield */
	IThreadYield();
#endif

	/*************************
	*** VALIDATION PHASE ****
	*************************/

	/* Check Conditions (WithOUT dummy) */
#ifdef _OHCI_DIAGNOSTICS_
	LogDebug("OHCI", "Ed Flags 0x%x, Ed Tail 0x%x, Ed Head 0x%x\n", ((OhciEndpointDescriptor_t*)Request->Data)->Flags,
		((OhciEndpointDescriptor_t*)Request->Data)->TailPtr, ((OhciEndpointDescriptor_t*)Request->Data)->HeadPtr);
#endif
	Transaction = Request->Transactions;
	while (Transaction->Link)
	{
		/* Cast and get the transfer code */
		Td = (OhciGTransferDescriptor_t*)Transaction->TransferDescriptor;
		CondCode = OHCI_TD_GET_CC(Td->Flags);

		/* Calculate length transferred 
		 * Take into consideration the N-1 */
		if (Transaction->Buffer != NULL
			&& Transaction->Length != 0) {
			Transaction->ActualLength = (Td->Cbp + 1) & 0xFFF;
		}

#ifdef _OHCI_DIAGNOSTICS_
		LogDebug("OHCI", "Td Flags 0x%x, Cbp 0x%x, BufferEnd 0x%x, Td Condition Code %u (%s)\n", 
			Td->Flags, Td->Cbp, Td->BufferEnd, CondCode, OhciErrorMessages[CondCode]);
#endif

		if (CondCode == 0 && Completed == TransferFinished)
			Completed = TransferFinished;
		else
		{
			if (CondCode == 4)
				Completed = TransferStalled;
			else if (CondCode == 3)
				Completed = TransferInvalidToggles;
			else if (CondCode == 2 || CondCode == 1)
				Completed = TransferBabble;
			else if (CondCode == 5)
				Completed = TransferNotResponding;
			else {
				LogDebug("OHCI", "Error: 0x%x (%s)", CondCode, OhciErrorMessages[CondCode]);
				Completed = TransferInvalidData;
			}
			break;
		}

		Transaction = Transaction->Link;
	}

	/* Update Status */
	Request->Status = Completed;

#ifdef _OHCI_DIAGNOSTICS_
	for (;;);
#endif
}

/* This one cleans up */
void OhciTransactionDestroy(void *Controller, UsbHcRequest_t *Request)
{
	/* Vars */
	UsbHcTransaction_t *Transaction = Request->Transactions;
	OhciController_t *Ctrl = (OhciController_t*)Controller;
	ListNode_t *Node = NULL;

	/* Unallocate Ed */
	if (Request->Type == ControlTransfer
		|| Request->Type == BulkTransfer)
	{
		/* Iterate and reset */
		while (Transaction)
		{
			/* Cast */
			OhciGTransferDescriptor_t *Td =
				(OhciGTransferDescriptor_t*)Transaction->TransferDescriptor;

			/* Memset */
			memset((void*)Td, 0, sizeof(OhciGTransferDescriptor_t));

			/* Next */
			Transaction = Transaction->Link;
		}

		/* Reset the ED */
		memset(Request->Data, 0, sizeof(OhciEndpointDescriptor_t));
	}
	else
	{
		/* Iso / Interrupt */
		
		/* Cast */
		OhciEndpointDescriptor_t *Ed = (OhciEndpointDescriptor_t*)Request->Data;

		/* Mark for unscheduling */
		Ed->HcdFlags |= OHCI_ED_UNSCHEDULE;

		/* Enable SOF, ED is not scheduled before */
		Ctrl->Registers->HcInterruptStatus = OHCI_INTR_SOF;
		Ctrl->Registers->HcInterruptEnable = OHCI_INTR_SOF;

		/* Wait for it to happen */
		SchedulerSleepThread((Addr_t*)Ed, 5000);
		IThreadYield();

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
