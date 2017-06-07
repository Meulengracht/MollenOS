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
#include <os/condition.h>
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

/* OhciTransactionInitialize
 * Initializes a transaction by allocating a new endpoint-descriptor
 * and preparing it for usage */
OhciEndpointDescriptor_t
OhciTransactionInitialize(
	_In_ OhciController_t *Controller, 
	_In_ UsbTransfer_t *Transfer)
{
	// Variables
	OhciEndpointDescriptor_t *Ed = NULL;

	// Allocate a new descriptor
	Ed = OhciEdAllocate(Controller, Transfer->Type);
	if (Ed == NULL) {
		return NULL;
	}

	// Mark it inactive untill ready
	Ed->Flags |= OHCI_EP_SKIP;

	// Calculate bandwidth and interval
	Ed->Bandwidth =
		(UsbCalculateBandwidth(Transfer->Speed, 
		Transfer->Endpoint.Direction, Transfer->Type, 
		Transfer->Endpoint.MaxPacketSize) / 1000);
	Ed->Interval = (uint16_t)Request->Endpoint.Interval;
}

/* This one queues the Transaction up for processing */
void
OhciTransactionSend(
	void *Controller,
	UsbHcRequest_t *Request)
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

/* UsbQueueTransferImpl 
 * Queues a new Control or Bulk transfer for the given driver
 * and pipe. They must exist. The function blocks untill execution */
UsbTransferStatus_t
UsbQueueTransferImpl(
	_In_ UUId_t Device,
	_In_ UUId_t Pipe,
	_In_ UsbTransfer_t *Transfer)
{
	// Variables
	OhciEndpointDescriptor_t *EndpointDescriptor = NULL;
	OhciTransferDescriptor_t *FirstTd = NULL, *ItrTd = NULL;
	OhciController_t *Controller = NULL;
	UsbTransferStatus_t Status;
	size_t Address, Endpoint;
	int i;

	// Get Controller
	Controller = (OhciController_t*)UsbManagerGetController(Device);

	// Initialize
	EndpointDescriptor = OhciTransactionInitialize(Controller, Transfer);

	// Now iterate and add the td's
	for (i = 0; i < 3; i++) {
		// Bytes
		size_t BytesToTransfer = Transfer->Transactions[i].Length;
		size_t ByteOffset = 0;

		while (BytesToTransfer) {
			// Variables
			OhciTransferDescriptor_t *Td = NULL;
			size_t BytesStep = 0;
			int Toggle;

			// Get toggle status
			Toggle = UsbManagerGetToggle(Device, Pipe);

			// Allocate a new Td
			if (Transfer->Transactions[i].Type == SetupTransaction) {
				Td = OhciTdSetup(Controller, &Transfer->Transactions[i], 
					Transfer->Type);

				// Consume entire setup-package
				BytesStep = BytesToTransfer;
			}
			else {
				// Depending on how much we are able to take in
				// 1 page per non-isoc, Isochronous can handle 2 pages
				BytesStep = MIN(BytesToTransfer, 0x1000);

				Td = OhciTdIo(Controller, Transfer->Type, 
					(Transfer->Transactions[i].Type == InTransaction ? OHCI_TD_PID_IN : OHCI_TD_PID_OUT), 
					Toggle, Transfer->Transactions[i].BufferAddress + ByteOffset, BytesStep);
			}

			// Store first
			if (FirstTd == NULL) {
				FirstTd = Td;
				ItrTd = Td;
			}
			else {
				// Update physical link
				ItrTd->Link = OHCI_POOL_TDINDEX(Controller->QueueControl.TDPoolPhysical, Td->Index);

				// Not first, update links
				ItrTd->LinkIndex = Td->Index;
				ItrTd = Td;
			}

			// Update toggle by flipping
			UsbManagerSetToggle(Device, Pipe, Toggle ^ 1);

			// Reduce
			BytesToTransfer -= BytesStep;
			ByteOffset += BytesStep;
		}
	}
	
	// Extract address and endpoint
	Address = HIWORD(Pipe);
	Endpoint = LOWORD(Pipe);

	// Finalize the endpoint-descriptor
	OhciEdInitialize(Controller, EndpointDescriptor,
		(int)FirstTd->Index, Transfer->Type, Address, Endpoint, 
		Transfer->Endpoint.MaxPacketSize, Transfer->Speed);

	// Send the transaction and wait for completion

	// Done
	return Status;
}

/* UsbQueuePeriodicImpl 
 * Queues a new Interrupt or Isochronous transfer. This transfer is 
 * persistant untill device is disconnected or Dequeue is called. */
OsStatus_t
UsbQueuePeriodicImpl()
{
	
}

/* UsbDequeueTransferImpl */
OsStatus_t
UsbDequeueTransferImpl()
{
	
}
