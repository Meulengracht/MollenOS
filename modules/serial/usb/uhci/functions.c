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
#include "uhci.h"

/* Includes
 * - Library */
#include <ds/list.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* UhciErrorMessages
 * Textual representations of the possible error codes */
const char *UhciErrorMessages[] = {
	"No Error",
	"Bitstuff Error",
	"CRC/Timeout Error",
	"NAK Recieved",
	"Babble Detected",
	"Data Buffer Error",
	"Stalled",
	"Active"
};

/* UhciTransactionInitialize
 * Initializes a transaction by allocating a new endpoint-descriptor
 * and preparing it for usage */
UhciQueueHead_t*
UhciTransactionInitialize(
	_In_ UhciController_t *Controller, 
	_In_ UsbTransfer_t *Transfer,
	_In_ size_t TransactionCount)
{
	// Variables
	UhciQueueHead_t *Qh = NULL;

	// Allocate a new queue-head
	Qh = UhciQhAllocate(Controller, Transfer->Type, Transfer->Speed);

	// Handle bandwidth allocation if neccessary
	if (Qh != NULL && 
		!(Qh->Flags & UHCI_QH_BANDWIDTH_ALLOC))
	{
		// Variables
		OsStatus_t Run = OsError;
		int Exponent, Queue;

		// Calculate the correct index
		for (Exponent = 7; Exponent >= 0; --Exponent) {
			if ((1 << Exponent) <= (int)Transfer->Endpoint.Interval)
				break;
		}

		// Sanitize that the exponent is valid
		if (Exponent < 0) {
			ERROR("UHCI: Invalid interval %u", 
				Transfer->Endpoint.Interval);
			Exponent = 0;
		}

		// Calculate the bandwidth
		Qh->Bandwidth = UsbCalculateBandwidth(Transfer->Speed, 
			Transfer->Direction, Transfer->Type, Transfer->Length);

		/* Make sure we have enough bandwidth for the transfer */
		if (Exponent > 0) {
			while (Run != OsSuccess && --Exponent >= 0) {
				
				// Select queue
				Queue = 9 - Exponent;

				// Calculate initial period
				Qh->Period = 1 << Exponent;

				// Update the queue of qh
				Qh->Flags = UHCI_QH_CLR_QUEUE(Qh->Flags);
				Qh->Flags |= UHCI_QH_SET_QUEUE(Queue);

				// For now, interrupt phase is fixed by the layout
				// of the QH lists.
				Qh->Phase = (Qh->Period / 2) & (UHCI_BANDWIDTH_PHASES - 1);

				// Validate the bandwidth
				Run = UsbSchedulerValidate(Controller->Scheduler,
					Qh->Period, Qh->Bandwidth, TransactionCount);
			}
		}
		else {

			// Select queue
			Queue = 9 - Exponent;

			// Calculate initial period
			Qh->Period = 1 << Exponent;

			// Update the queue of qh
			Qh->Flags = UHCI_QH_CLR_QUEUE(Qh->Flags);
			Qh->Flags |= UHCI_QH_SET_QUEUE(Queue);

			// For now, interrupt phase is fixed by the layout
			// of the QH lists.
			Qh->Phase = (Qh->Period / 2) & (UHCI_BANDWIDTH_PHASES - 1);

			// Validate the bandwidth
			Run = UsbSchedulerValidate(Controller->Scheduler,
					Qh->Period, Qh->Bandwidth, TransactionCount);
		}

		// Sanitize the validation
		if (Run == OsError) {
			ERROR("UHCI: Had no room for the transfer in queueus");
			return Qh;
		}

		// Reserve and done!
		UsbSchedulerReserveBandwidth(Controller->Scheduler,
					Qh->Period, Qh->Bandwidth, TransactionCount, 
					&Qh->StartFrame, NULL);
	}

	// Done
	return Qh;
}

/* UhciTransactionDispatch
 * Queues the transfer up in the controller hardware, after finalizing the
 * transactions and preparing them. */
OsStatus_t
UhciTransactionDispatch(
	_In_ UhciController_t *Controller,
	_In_ UsbManagerTransfer_t *Transfer)
{
	// Variables
	UhciQueueHead_t *Qh = NULL;
	int QhIndex = -1;
	UhciTransferDescriptor_t *Td = NULL;
	uintptr_t QhAddress;
	DataKey_t Key;
	int CondCode, Queue;

	/*************************
	 ****** SETUP PHASE ******
	 *************************/
	Qh = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
	QhIndex = UHCI_QH_INDEX(Qh->HcdFlags);

	// Lookup physical
	QhAddress = UHCI_POOL_QHINDEX(Controller, QhIndex);

	// Store transaction in queue
	Key.Value = 0;
	ListAppend(Controller->QueueControl.TransactionList, 
		ListCreateNode(Key, Key, Transfer));

	/*************************
	 **** LINKING PHASE ******
	 *************************/
	UhciGetCurrentFrame(Controller);

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
	
#ifndef __DEBUG
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

#ifdef __DEBUG
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

#ifdef __DEBUG
	for (;;);
#endif

	// Variables
	OhciEndpointDescriptor_t *EndpointDescriptor = NULL;
	int EndpointDescriptorIndex = -1;
	uintptr_t EpAddress;
	DataKey_t Key;

	/*************************
	 ****** SETUP PHASE ******
	 *************************/
	EndpointDescriptor = (OhciEndpointDescriptor_t*)Transfer->EndpointDescriptor;
	EndpointDescriptorIndex = OHCI_ED_GET_INDEX(EndpointDescriptor->HcdFlags);

	// Lookup physical
	EpAddress = OHCI_POOL_EDINDEX(Controller->QueueControl.EDPoolPhysical, EndpointDescriptorIndex);

	// Store transaction in queue
	Key.Value = 0;
	ListAppend(Controller->QueueControl.TransactionList, 
		ListCreateNode(Key, Key, Transfer));

	// Clear pauses
	EndpointDescriptor->Flags &= ~(OHCI_EP_SKIP);
	EndpointDescriptor->Current &= ~(OHCI_LINK_END);

	// Trace
	TRACE("Ed Address 0x%x, Flags 0x%x, Tail 0x%x, Current 0x%x, Link 0x%x", 
		EpAddress, EndpointDescriptor->Flags, EndpointDescriptor->TailPointer, 
		EndpointDescriptor->Current, EndpointDescriptor->Link);

	/*************************
	 **** LINKING PHASE ******
	 *************************/
	
	// Set the schedule flag on ED
	EndpointDescriptor->HcdFlags |= OHCI_ED_SCHEDULE;

	// Enable SOF, ED is not scheduled before this interrupt
	Controller->Registers->HcInterruptStatus = OHCI_INTR_SOF;
	Controller->Registers->HcInterruptEnable = OHCI_INTR_SOF;

	// Now the transaction is queued.
#ifdef __DEBUG
	TRACE("Current Frame 0x%x (HCCA), HcFrameNumber 0x%x, HcFrameInterval 0x%x, HcFrameRemaining 0x%x", Ctrl->HCCA->CurrentFrame,
		Controller->Registers->HcFmNumber, Controller->Registers->HcFmInterval, 
		Controller->Registers->HcFmRemaining);
	TRACE("Transaction sent.. waiting for reply..");
	ThreadSleep(5000);
	TRACE("Current Frame 0x%x (HCCA), HcFrameNumber 0x%x, HcFrameInterval 0x%x, HcFrameRemaining 0x%x", Ctrl->HCCA->CurrentFrame,
		Controller->Registers->HcFmNumber, Controller->Registers->HcFmInterval, 
		Controller->Registers->HcFmRemaining);
	TRACE("1. Current Control: 0x%x, Current Head: 0x%x",
		Controller->Registers->HcControlCurrentED, Controller->Registers->HcControlHeadED);
	ThreadSleep(5000);
	TRACE("Current Frame 0x%x (HCCA), HcFrameNumber 0x%x, HcFrameInterval 0x%x, HcFrameRemaining 0x%x", Ctrl->HCCA->CurrentFrame,
		Controller->Registers->HcFmNumber, Controller->Registers->HcFmInterval, 
		Controller->Registers->HcFmRemaining);
	TRACE("2. Current Control: 0x%x, Current Head: 0x%x",
		Controller->Registers->HcControlCurrentED, Controller->Registers->HcControlHeadED);
	TRACE("Current Control 0x%x, Current Cmd 0x%x",
		Controller->Registers->HcControl, Controller->Registers->HcCommandStatus);

	// Validate
	Transfer->Requester = UUID_INVALID;
	OhciTransactionFinalize(Controller, Transfer, 1);

	// Don't go further
	for (;;);
#endif

	// Done
	return OsSuccess;
}

/* OhciTransactionFinalize
 * Cleans up the transfer, deallocates resources and validates the td's */
OsStatus_t
OhciTransactionFinalize(
	_In_ OhciController_t *Controller,
	_In_ UsbManagerTransfer_t *Transfer,
	_In_ int Validate)
{
	// Variables
	OhciEndpointDescriptor_t *EndpointDescriptor = 
		(OhciEndpointDescriptor_t*)Transfer->EndpointDescriptor;
	OhciTransferDescriptor_t *Td = NULL;
	UsbTransferResult_t Result;
	uint ErrorCode = 0;

	/*************************
	 *** VALIDATION PHASE ****
	 *************************/
	if (Validate != 0) {
		// Iterate through all td's and validate condition code
		// We don't validate the last td
		TRACE("Flags 0x%x, Tail 0x%x, Current 0x%x", 
			EndpointDescriptor->Flags, EndpointDescriptor->TailPointer, 
			EndpointDescriptor->Current);

		// Get first td
		Td = Controller->QueueControl.TDPool[EndpointDescriptor->HeadIndex];
		while (Td->LinkIndex != -1) {
			// Extract the error code
			ErrorCode = OHCI_TD_GET_CC(Td->Flags);

			// Calculate length transferred 
			// Take into consideration the N-1 
			if (Td->BufferEnd != 0) {
				Transfer->BytesTransferred += (Td->Cbp + 1) & 0xFFF;
			}
			
			// Trace
			TRACE("Flags 0x%x, Cbp 0x%x, BufferEnd 0x%x, Condition Code %u (%s)", 
				Td->Flags, Td->Cbp, Td->BufferEnd, ErrorCode, 
				OhciErrorMessages[ErrorCode]);

			// Now validate the code
			if (ErrorCode == 0 && Transfer->Status == TransferFinished)
				Transfer->Status = TransferFinished;
			else {
				if (ErrorCode == 4)
					Transfer->Status = TransferStalled;
				else if (ErrorCode == 3)
					Transfer->Status = TransferInvalidToggles;
				else if (ErrorCode == 2 || ErrorCode == 1)
					Transfer->Status = TransferBabble;
				else if (ErrorCode == 5)
					Transfer->Status = TransferNotResponding;
				else {
					TRACE("Error: 0x%x (%s)", ErrorCode, OhciErrorMessages[ErrorCode]);
					Transfer->Status = TransferInvalidData;
				}
				break;
			}
			
			// Go to next td
			Td = Controller->QueueControl.TDPool[Td->LinkIndex];
		}
	}

	// Step one is to unallocate the td's
	// Get first td
	Td = Controller->QueueControl.TDPool[EndpointDescriptor->HeadIndex];
	while (Td) {
		// Save link-index before resetting
		int LinkIndex = Td->LinkIndex;

		// Reset the TD, nothing in there is something we store further
		memset((void*)Td, 0, sizeof(OhciTransferDescriptor_t));

		// Go to next td or terminate
		if (LinkIndex != -1) {
			Td = Controller->QueueControl.TDPool[LinkIndex];
		}
		else {
			break;
		}
	}

	// Now unallocate the ED by zeroing that
	memset((void*)EndpointDescriptor, 0, sizeof(OhciEndpointDescriptor_t));

	// Update members
	Transfer->EndpointDescriptor = NULL;

	// Should we notify the user here?...
	if (Transfer->Requester != UUID_INVALID) {
		Result.Id = Transfer->Id;
		Result.BytesTransferred = Transfer->BytesTransferred;
		Result.Status = Transfer->Status;
		PipeSend(Transfer->Requester, Transfer->ResponsePort, 
			(void*)&Result, sizeof(UsbTransferResult_t));
	}

	// Cleanup the transfer
	free(Transfer);

	// Done
	return OsSuccess;
}

/* UsbQueueTransferGeneric 
 * Queues a new transfer for the given driver
 * and pipe. They must exist. The function does not block*/
OsStatus_t
UsbQueueTransferGeneric(
	_InOut_ UsbManagerTransfer_t *Transfer)
{
	// Variables
	OhciEndpointDescriptor_t *EndpointDescriptor = NULL;
	OhciTransferDescriptor_t *FirstTd = NULL, *ItrTd = NULL, *ZeroTd = NULL;
	OhciController_t *Controller = NULL;
	size_t Address, Endpoint;
	int i;

	// Get Controller
	Controller = (OhciController_t*)UsbManagerGetController(Transfer->Device);

	// Initialize
	EndpointDescriptor = OhciTransactionInitialize(Controller, &Transfer->Transfer);

	// Update the stored information
	Transfer->TransactionCount = 0;
	Transfer->EndpointDescriptor = EndpointDescriptor;
	Transfer->Status = TransferNotProcessed;
	Transfer->BytesTransferred = 0;
	Transfer->Cleanup = 0;

	// If it's a control transfer set initial toggle 0
	if (Transfer->Transfer.Type == ControlTransfer) {
		UsbManagerSetToggle(Transfer->Device, Transfer->Pipe, 0);
	}

	// Now iterate and add the td's
	for (i = 0; i < 3; i++) {
		// Bytes
		size_t BytesToTransfer = Transfer->Transfer.Transactions[i].Length;
		size_t ByteOffset = 0;
		int AddZeroLength = 0;

		// If it's a handshake package then set toggle
		if (Transfer->Transfer.Transactions[i].Handshake) {
			UsbManagerSetToggle(Transfer->Device, Transfer->Pipe, 1);
		}

		while (BytesToTransfer 
			|| Transfer->Transfer.Transactions[i].ZeroLength == 1
			|| AddZeroLength == 1) {
			// Variables
			OhciTransferDescriptor_t *Td = NULL;
			size_t BytesStep = 0;
			int Toggle;

			// Get toggle status
			Toggle = UsbManagerGetToggle(Transfer->Device, Transfer->Pipe);

			// Allocate a new Td
			if (Transfer->Transfer.Transactions[i].Type == SetupTransaction) {
				Td = OhciTdSetup(Controller, &Transfer->Transfer.Transactions[i], 
					Transfer->Transfer.Type);

				// Consume entire setup-package
				BytesStep = BytesToTransfer;
			}
			else {
				// Depending on how much we are able to take in
				// 1 page per non-isoc, Isochronous can handle 2 pages
				if (Transfer->Transfer.Type != IsochronousTransfer) {
					BytesStep = MIN(BytesToTransfer, 
						Transfer->Transfer.Endpoint.MaxPacketSize);
				}
				else {
					BytesStep = MIN(BytesToTransfer, 0x1000);
				}

				Td = OhciTdIo(Controller, Transfer->Transfer.Type, 
					(Transfer->Transfer.Transactions[i].Type == InTransaction ? OHCI_TD_PID_IN : OHCI_TD_PID_OUT), 
					Toggle, Transfer->Transfer.Transactions[i].BufferAddress + ByteOffset, BytesStep);
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
			UsbManagerSetToggle(Transfer->Device, Transfer->Pipe, Toggle ^ 1);

			// Increase count
			Transfer->TransactionCount++;

			// Break out on zero lengths
			if (Transfer->Transfer.Transactions[i].ZeroLength == 1
				|| AddZeroLength == 1) {
				break;
			}

			// Reduce
			BytesToTransfer -= BytesStep;
			ByteOffset += BytesStep;

			// If it was out, and we had a multiple of MPS, then ZLP
			if (BytesStep == Transfer->Transfer.Endpoint.MaxPacketSize 
				&& BytesToTransfer == 0
				&& Transfer->Transfer.Type == BulkTransfer
				&& Transfer->Transfer.Transactions[i].Type == OutTransaction) {
				AddZeroLength = 1;
			}
		}
	}

	// Set last td to generate a interrupt (not null)
	ItrTd->Flags &= ~(OHCI_TD_NO_IOC);

	// Add a null-transaction (Out, Zero)
	// But do NOT update the endpoint toggle, and Isoc does not need this
	if (Transfer->Transfer.Type != IsochronousTransfer) {
		ZeroTd = OhciTdIo(Controller, Transfer->Transfer.Type, OHCI_TD_PID_OUT, 
		UsbManagerGetToggle(Transfer->Device, Transfer->Pipe), 0, 0);

		// Update physical link
		ItrTd->Link = OHCI_POOL_TDINDEX(Controller->QueueControl.TDPoolPhysical, ZeroTd->Index);

		// Not first, update links
		ItrTd->LinkIndex = ZeroTd->Index;
	}
	
	// Extract address and endpoint
	Address = HIWORD(Transfer->Pipe);
	Endpoint = LOWORD(Transfer->Pipe);

	// Finalize the endpoint-descriptor
	OhciEdInitialize(Controller, EndpointDescriptor,
		(int)FirstTd->Index, Transfer->Transfer.Type, Address, Endpoint, 
		Transfer->Transfer.Endpoint.MaxPacketSize, Transfer->Transfer.Speed);

	// Send the transaction and wait for completion
	return OhciTransactionDispatch(Controller, Transfer);
}

/* UsbDequeueTransferGeneric 
 * Removes a queued transfer from the controller's framelist */
OsStatus_t
UsbDequeueTransferGeneric(
	_In_ UsbManagerTransfer_t *Transfer)
{
	// Variables
	OhciEndpointDescriptor_t *EndpointDescriptor = 
		(OhciEndpointDescriptor_t*)Transfer->EndpointDescriptor;
	OhciController_t *Controller = NULL;

	// Get Controller
	Controller = (OhciController_t*)UsbManagerGetController(Transfer->Device);

	// Mark for unscheduling
	EndpointDescriptor->HcdFlags |= OHCI_ED_UNSCHEDULE;

	// Enable SOF, ED is not scheduled before
	Controller->Registers->HcInterruptStatus = OHCI_INTR_SOF;
	Controller->Registers->HcInterruptEnable = OHCI_INTR_SOF;

	// Done, rest of cleanup happens in Finalize
	return OsSuccess;
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
