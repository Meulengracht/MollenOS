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


/* UsbQueueDebug
 * Dumps the QH-settings and all the attached td's */
void
UsbQueueDebug(
    UhciController_t *Controller,
    UhciQueueHead_t *Qh)
{
    // Variables
    UhciTransferDescriptor_t *Td = NULL;
    uintptr_t PhysicalAddress = 0;

    PhysicalAddress = UHCI_POOL_QHINDEX(Controller, UHCI_QH_GET_INDEX(Qh->Flags));
    TRACE("QH(0x%x): Flags 0x%x, NextQh 0x%x, FirstChild 0x%x", 
        PhysicalAddress, Qh->Flags, Qh->Link, Qh->Child);

    // Get first td
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    while (Td != NULL) {
        PhysicalAddress = UHCI_POOL_TDINDEX(Controller, UHCI_TD_GET_INDEX(Td->HcdFlags));
        TRACE("TD(0x%x): Link 0x%x, Flags 0x%x, Header 0x%x, Buffer 0x%x", 
            PhysicalAddress, Td->Link, Td->Flags, Td->Header, Td->Buffer);
        // Go to next td
        if (Td->LinkIndex != UHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
        }
        else {
            Td = NULL;
        }
    }
}

/* UhciTransactionInitialize
 * Initializes a transaction by allocating a new endpoint-descriptor
 * and preparing it for usage */
OsStatus_t
UhciTransactionInitialize(
	_In_ UhciController_t *Controller, 
    _In_ UsbTransfer_t *Transfer,
    _Out_ UhciQueueHead_t **QhResult)
{
	// Variables
	UhciQueueHead_t *Qh = NULL;
	size_t TransactionCount = 0;

	// Allocate a new queue-head
	*QhResult = Qh = UhciQhAllocate(Controller, Transfer->Type, Transfer->Speed);

	// Calculate transaction count
	TransactionCount = DIVUP(Transfer->Length, Transfer->Endpoint.MaxPacketSize);

	// Handle bandwidth allocation if neccessary
	if (Qh != NULL && (Qh->Flags & UHCI_QH_BANDWIDTH_ALLOC)) {
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
			ERROR("UHCI: Invalid interval %u", Transfer->Endpoint.Interval);
			Exponent = 0;
		}

		// Calculate the bandwidth
		Qh->Bandwidth = UsbCalculateBandwidth(Transfer->Speed, 
			Transfer->Endpoint.Direction, Transfer->Type, Transfer->Length);

		// Make sure we have enough bandwidth for the transfer
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
			return OsError;
		}

		// Reserve and done!
		UsbSchedulerReserveBandwidth(Controller->Scheduler,
					Qh->Period, Qh->Bandwidth, TransactionCount, 
					&Qh->StartFrame, NULL);
	}

	// Done
	return OsSuccess;
}

/* UhciTransactionDispatch
 * Queues the transfer up in the controller hardware, after finalizing the
 * transactions and preparing them. */
UsbTransferStatus_t
UhciTransactionDispatch(
	_In_ UhciController_t *Controller,
	_In_ UsbManagerTransfer_t *Transfer)
{
	// Variables
	UhciQueueHead_t *Qh = NULL;
	uintptr_t QhAddress;
	DataKey_t Key;
	int QhIndex = -1;
	int Queue = -1;

	/*************************
	 ****** SETUP PHASE ******
	 *************************/
	Qh = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
	QhIndex = UHCI_QH_GET_INDEX(Qh->Flags);
	Queue = UHCI_QH_GET_QUEUE(Qh->Flags);

	// Lookup physical
	QhAddress = UHCI_POOL_QHINDEX(Controller, QhIndex);

	// Store transaction in queue
	Key.Value = 0;
	ListAppend(Controller->QueueControl.TransactionList, 
		ListCreateNode(Key, Key, Transfer));

	// Trace
	TRACE("UHCI: QH at 0x%x, FirstTd 0x%x, NextQh 0x%x", 
		QhAddress, Qh->Child, Qh->Link);
	TRACE("UHCI: Queue %u, StartFrame %u, Flags 0x%x", 
        Queue, Qh->StartFrame, Qh->Flags);

	/*************************
	 **** LINKING PHASE ******
	 *************************/
	UhciUpdateCurrentFrame(Controller);

	// Asynchronous requests, Control & Bulk
	if (Queue >= UHCI_QH_ASYNC) {
		
		// Variables
		UhciQueueHead_t *PrevQh = &Controller->QueueControl.QHPool[UHCI_QH_ASYNC];
		int PrevQueue = UHCI_QH_GET_QUEUE(PrevQh->Flags);

        // Iterate and find a spot, based on the queue priority
        TRACE("(%u) Linking asynchronous queue-head (async-next: %i)", 
            Controller->QueueControl.Frame, PrevQh->LinkIndex);
        TRACE("Controller status: 0x%x", UhciRead16(Controller, UHCI_REGISTER_COMMAND));
		while (PrevQh->LinkIndex != UHCI_NO_INDEX) {
			PrevQueue = UHCI_QH_GET_QUEUE(PrevQh->Flags);
			if (PrevQueue <= Queue) {
				break;
			}

			// Go to next qh
			PrevQh = &Controller->QueueControl.QHPool[PrevQh->LinkIndex];
		}

		// Insert in-between the two by transfering link
		// Make use of a memory-barrier to flush
		Qh->Link = PrevQh->Link;
		Qh->LinkIndex = PrevQh->LinkIndex;
		MemoryBarrier();
		PrevQh->Link = (QhAddress | UHCI_LINK_QH);
        PrevQh->LinkIndex = QhIndex;
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
	// Periodic requests
	else if (Queue > UHCI_QH_ISOCHRONOUS && Queue < UHCI_QH_ASYNC) {
		
		// Variables
        UhciQueueHead_t *ExistingQueueHead = 
            &Controller->QueueControl.QHPool[Queue];

        // Iterate to end of interrupt list
        while (ExistingQueueHead->LinkIndex != UHCI_QH_ASYNC) {
            ExistingQueueHead = &Controller->QueueControl.
                QHPool[ExistingQueueHead->LinkIndex];
        }

		// Insert in-between the two by transfering link
        // Make use of a memory-barrier to flush
		Qh->Link = ExistingQueueHead->Link;
		Qh->LinkIndex = ExistingQueueHead->LinkIndex;
		MemoryBarrier();
		ExistingQueueHead->Link = (QhAddress | UHCI_LINK_QH);
		ExistingQueueHead->LinkIndex = QhIndex;
	}
	else {
		UhciLinkIsochronous(Controller, Qh);
	}

	// Done
	return TransferQueued;
}

/* UhciTransactionFinalize
 * Cleans up the transfer, deallocates resources and validates the td's */
OsStatus_t
UhciTransactionFinalize(
	_In_ UhciController_t *Controller,
	_In_ UsbManagerTransfer_t *Transfer,
	_In_ int Validate)
{
	// Variables
	UhciQueueHead_t *Qh = (UhciQueueHead_t*)Transfer->EndpointDescriptor;
	UhciTransferDescriptor_t *Td = NULL;
	UsbTransferResult_t Result;
	int QhIndex = -1;
    int ErrorCode = 0;
    
    // Debug
    TRACE("UhciTransactionFinalize()");

	/*************************
	 *** VALIDATION PHASE ****
	 *************************/
	QhIndex = UHCI_QH_GET_INDEX(Qh->Flags);
	if (Validate != 0) {
		// Iterate through all td's and validate condition code
		// We don't validate the last td
		TRACE("Flags 0x%x, Tail 0x%x, Current 0x%x", 
			Qh->Flags, Qh->Link, Qh->Child);

		// Get first td
		Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
		while (Td) {
			// Extract the error code
			ErrorCode = UhciConditionCodeToIndex(UHCI_TD_STATUS(Td->Flags));

			// Calculate length transferred 
			// Take into consideration the N-1 
			if (Td->Buffer != 0) {
				Transfer->BytesTransferred += UHCI_TD_ACTUALLENGTH(Td->Flags + 1);
			}

			// Trace
			TRACE("Flags 0x%x, Header 0x%x, Buffer 0x%x, Td Condition Code %u", 
				Td->Flags, Td->Header, Td->Buffer, ErrorCode);

			// Now validate the code
			if (ErrorCode == 0 && Transfer->Status == TransferFinished)
				Transfer->Status = TransferFinished;
			else {
				Transfer->Status = UhciGetStatusCode(ErrorCode);
				break;
			}
			
			// Go to next td
			if (Td->LinkIndex != UHCI_NO_INDEX) {
				Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
			}
			else {
				break;
			}
		}
	}

	// Unlink qh
	if (Transfer->Transfer.Type == ControlTransfer
		|| Transfer->Transfer.Type == BulkTransfer)
	{
		// Variables
		UhciQueueHead_t *PrevQh = &Controller->QueueControl.QHPool[UHCI_QH_ASYNC];

		// Iterate untill the current qh
		while (PrevQh->LinkIndex != QhIndex) {
			if (PrevQh->LinkIndex == UHCI_NO_INDEX) {
				break;
			}
			PrevQh = &Controller->QueueControl.QHPool[PrevQh->LinkIndex];
		}

		// Check that the qh even exists
		if (PrevQh->LinkIndex != QhIndex) {
			TRACE("UHCI: Couldn't find Qh in frame-list");
		}
		else {
			// Transfer the link to previous
			PrevQh->Link = Qh->Link;
			PrevQh->LinkIndex = Qh->LinkIndex;
			MemoryBarrier();

#ifdef UHCI_FSBR
			/* Get */
			int PrevQueue = UHCI_QH_GET_QUEUE(PrevQh->Flags);

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
		}
	}
	else if (Transfer->Transfer.Type == InterruptTransfer) {
		
		// Variables
		UhciQueueHead_t *ItrQh = NULL, *PrevQh = NULL;
		int Queue = UHCI_QH_GET_QUEUE(Qh->Flags);

		// Get initial qh of the queue
		// and find the correct spot
		ItrQh = &Controller->QueueControl.QHPool[Queue];
		while (ItrQh != Qh) {
			if (ItrQh->LinkIndex == UHCI_QH_NULL
				|| ItrQh->LinkIndex == UHCI_NO_INDEX) {
				ItrQh = NULL;
				break;
			}

			// Go to next
			PrevQh = ItrQh;
			ItrQh = &Controller->QueueControl.QHPool[ItrQh->LinkIndex];
		}

		// If ItrQh is null it didn't exist
		if (ItrQh == NULL) {
			TRACE("UHCI: Tried to unschedule a queue-qh that didn't exist in queue");
		}
		else {
			// If there is a previous transfer link, should always happen
			if (PrevQh != NULL) {
				PrevQh->Link = Qh->Link;
				PrevQh->LinkIndex = Qh->LinkIndex;
				MemoryBarrier();
			}
		}

		// Release bandwidth
		UsbSchedulerReleaseBandwidth(Controller->Scheduler, Qh->Period,
			Qh->Bandwidth, Qh->StartFrame, 1);
	}
	else {
		// Unlink and release bandwidth
		UhciUnlinkIsochronous(Controller, Qh);
		UsbSchedulerReleaseBandwidth(Controller->Scheduler, Qh->Period,
			Qh->Bandwidth, Qh->StartFrame, 1);
	}

	// Step two is to unallocate the td's
	// Get first td
	Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
	while (Td) {
		// Save link-index before resetting
		int LinkIndex = Td->LinkIndex;

		// Reset the TD, nothing in there is something we store further
		memset((void*)Td, 0, sizeof(UhciTransferDescriptor_t));

		// Go to next td or terminate
		if (LinkIndex != UHCI_NO_INDEX 
			&& LinkIndex != UHCI_POOL_TDNULL) {
			Td = &Controller->QueueControl.TDPool[LinkIndex];
		}
		else {
			break;
		}
	}

	// Now unallocate theQhED by zeroing that
	memset((void*)Qh, 0, sizeof(UhciQueueHead_t));

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
UsbTransferStatus_t
UsbQueueTransferGeneric(
	_InOut_ UsbManagerTransfer_t *Transfer)
{
	// Variables
	UhciQueueHead_t *Qh = NULL;
	UhciTransferDescriptor_t *FirstTd = NULL, *ItrTd = NULL;
	UhciController_t *Controller = NULL;
	size_t Address, Endpoint;
    int i;
    
    // Debug
    TRACE("UsbQueueTransferGeneric()");

	// Get Controller
	Controller = (UhciController_t*)UsbManagerGetController(Transfer->Device);

	// Initialize
	if (UhciTransactionInitialize(Controller, &Transfer->Transfer, &Qh) != OsSuccess) {
        return TransferNoBandwidth;
    }

	// Update the stored information
	Transfer->TransactionCount = 0;
	Transfer->EndpointDescriptor = Qh;
	Transfer->Status = TransferNotProcessed;
	Transfer->BytesTransferred = 0;
	Transfer->Cleanup = 0;
	
	// Extract address and endpoint
	Address = HIWORD(Transfer->Pipe);
	Endpoint = LOWORD(Transfer->Pipe);

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
			UhciTransferDescriptor_t *Td = NULL;
			size_t BytesStep = 0;
			int Toggle;

			// Get toggle status
			Toggle = UsbManagerGetToggle(Transfer->Device, Transfer->Pipe);

			// Allocate a new Td
			if (Transfer->Transfer.Transactions[i].Type == SetupTransaction) {
				Td = UhciTdSetup(Controller, &Transfer->Transfer.Transactions[i], 
					Address, Endpoint, Transfer->Transfer.Type, 
					Transfer->Transfer.Speed);

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

				Td = UhciTdIo(Controller, Transfer->Transfer.Type, 
					(Transfer->Transfer.Transactions[i].Type == InTransaction ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT), 
					Toggle, Address, Endpoint, Transfer->Transfer.Endpoint.MaxPacketSize,
					Transfer->Transfer.Speed, 
					Transfer->Transfer.Transactions[i].BufferAddress + ByteOffset, BytesStep);
			}

			// Store first
			if (FirstTd == NULL) {
				FirstTd = Td;
				ItrTd = Td;
			}
			else {
				// Update physical link
				ItrTd->LinkIndex = UHCI_TD_GET_INDEX(Td->HcdFlags);
				ItrTd->Link = (UHCI_POOL_TDINDEX(Controller, ItrTd->LinkIndex) | UHCI_LINK_DEPTH);
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
	ItrTd->Flags |= UHCI_TD_IOC;

	// Finalize the endpoint-descriptor
	UhciQhInitialize(Controller, Qh, UHCI_TD_GET_INDEX(FirstTd->HcdFlags));

    // Send the transaction and wait for completion
    UsbQueueDebug(Controller, Qh);
	return UhciTransactionDispatch(Controller, Transfer);
}

/* UsbDequeueTransferGeneric 
 * Removes a queued transfer from the controller's framelist */
UsbTransferStatus_t
UsbDequeueTransferGeneric(
	_In_ UsbManagerTransfer_t *Transfer)
{
	// Variables
	UhciQueueHead_t *Qh = 
		(UhciQueueHead_t*)Transfer->EndpointDescriptor;
	UhciController_t *Controller = NULL;

	// Get Controller
	Controller = (UhciController_t*)UsbManagerGetController(Transfer->Device);

	// Mark for unscheduling on next interrupt/check
	Qh->Flags |= UHCI_QH_UNSCHEDULE;

	// Done, rest of cleanup happens in Finalize
	return TransferFinished;
}
