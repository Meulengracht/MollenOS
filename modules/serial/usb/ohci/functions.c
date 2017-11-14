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
#include <ds/collection.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* OhciTransactionInitialize
 * Initializes a transaction by allocating a new endpoint-descriptor
 * and preparing it for usage */
OhciQueueHead_t*
OhciTransactionInitialize(
	_In_ OhciController_t *Controller, 
	_In_ UsbTransfer_t *Transfer)
{
	// Variables
	OhciQueueHead_t *Qh = NULL;

	// Allocate a new descriptor
	Qh = OhciQhAllocate(Controller);
	if (Qh == NULL) {
		return NULL;
	}

	// Mark it inactive untill ready
	Qh->Flags |= OHCI_QH_SKIP;

	// Calculate bandwidth and interval
	Qh->Bandwidth =
		(UsbCalculateBandwidth(Transfer->Speed, 
		Transfer->Endpoint.Direction, Transfer->Type, 
		Transfer->Endpoint.MaxPacketSize) / 1000);
        Qh->Interval = (uint16_t)Transfer->Endpoint.Interval;

	// Done
	return Qh;
}

/* OhciTransactionDispatch
 * Queues the transfer up in the controller hardware, after finalizing the
 * transactions and preparing them. */
UsbTransferStatus_t
OhciTransactionDispatch(
	_In_ OhciController_t *Controller,
	_In_ UsbManagerTransfer_t *Transfer)
{
	// Variables
	OhciQueueHead_t *Qh     = NULL;
	uintptr_t QhAddress     = 0;
	DataKey_t Key;

	/*************************
	 ****** SETUP PHASE ******
	 *************************/
	Qh = (OhciQueueHead_t*)Transfer->EndpointDescriptor;

	// Lookup physical
	QhAddress = OHCI_POOL_QHINDEX(Controller, Qh->Index);

	// Store transaction in queue
	Key.Value = 0;
	CollectionAppend(Controller->QueueControl.TransactionList, 
		CollectionCreateNode(Key, Transfer));

	// Clear pauses
	Qh->Flags &= ~OHCI_QH_SKIP;
	Qh->Current &= ~OHCI_LINK_HALTED;

	// Trace
	TRACE("Qh Address 0x%x, Flags 0x%x, Tail 0x%x, Current 0x%x, Link 0x%x", 
        QhAddress, Qh->Flags, Qh->TailPointer, Qh->Current, Qh->Link);

	/*************************
	 **** LINKING PHASE ******
	 *************************/
	
	// Set the schedule flag on ED and
	// enable SOF, ED is not scheduled before this interrupt
	Qh->HcdInformation |= OHCI_QH_SCHEDULE;
	Controller->Registers->HcInterruptStatus = OHCI_SOF_EVENT;
	Controller->Registers->HcInterruptEnable = OHCI_SOF_EVENT;

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
	return TransferQueued;
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
	OhciQueueHead_t *Qh             = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
	OhciTransferDescriptor_t *Td    = NULL;
	int ErrorCode                   = 0;
	UsbTransferResult_t Result;

	/*************************
	 *** VALIDATION PHASE ****
	 *************************/
	if (Validate != 0) {
		// Iterate through all td's and validate condition code
		// We don't validate the last td
		TRACE("Flags 0x%x, Tail 0x%x, Current 0x%x", 
            Qh->Flags, Qh->TailPointer,  Qh->Current);

		// Get first td
		Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
		while (Td->LinkIndex != OHCI_NO_INDEX) {
            // Extract the error code
			ErrorCode = OHCI_TD_ERRORCODE(Td->Flags);

			// Calculate length transferred 
			// Take into consideration the N-1 
			if (Td->BufferEnd != 0) {
				Transfer->BytesTransferred += (Td->Cbp + 1) & 0xFFF;
			}
			
			// Trace
			TRACE("Flags 0x%x, Cbp 0x%x, BufferEnd 0x%x, Condition Code %u", 
				Td->Flags, Td->Cbp, Td->BufferEnd, ErrorCode);

			// Now validate the code
			if (ErrorCode == 0 && Transfer->Status == TransferFinished)
				Transfer->Status = TransferFinished;
			else {
				Transfer->Status = OhciGetStatusCode(ErrorCode);
				break;
			}
			
			// Go to next td
			Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
		}
	}

	// Step one is to unallocate the td's
	// Get first td
	Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
	while (Td) {
		// Save link-index before resetting
		int LinkIndex = Td->LinkIndex;

		// Reset the TD, nothing in there is something we store further
		memset((void*)Td, 0, sizeof(OhciTransferDescriptor_t));

		// Go to next td or terminate
		if (LinkIndex != OHCI_NO_INDEX) {
			Td = &Controller->QueueControl.TDPool[LinkIndex];
		}
		else {
			break;
		}
	}

	// Now unallocate the ED by zeroing that
	memset((void*)Qh, 0, sizeof(OhciQueueHead_t));
	Transfer->EndpointDescriptor = NULL;

	// Should we notify the user here?...
	if (Transfer->Requester != UUID_INVALID
        && (Transfer->Transfer.Type == ControlTransfer
            || Transfer->Transfer.Type == BulkTransfer)) {
		Result.Id = Transfer->Id;
		Result.BytesTransferred = Transfer->BytesTransferred;
		Result.Status = Transfer->Status;
		PipeSend(Transfer->Requester, Transfer->ResponsePort, 
			(void*)&Result, sizeof(UsbTransferResult_t));
	}

	// Cleanup the transfer
	free(Transfer);
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
	OhciQueueHead_t *EndpointDescriptor = NULL;
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
				Td = OhciTdSetup(Controller, &Transfer->Transfer.Transactions[i]);

				// Consume entire setup-package
				BytesStep = BytesToTransfer;
			}
			else {
				// Depending on how much we are able to take in
				// 1 page per non-isoc, Isochronous can handle 2 pages
                BytesStep = MIN(BytesToTransfer, Transfer->Transfer.Endpoint.MaxPacketSize);

				Td = OhciTdIo(Controller, Transfer->Transfer.Type, 
					(Transfer->Transfer.Transactions[i].Type == InTransaction ? OHCI_TD_IN : OHCI_TD_OUT), 
					Toggle, Transfer->Transfer.Transactions[i].BufferAddress + ByteOffset, BytesStep);
			}

			// Store first
			if (FirstTd == NULL) {
				FirstTd = Td;
				ItrTd = Td;
			}
			else {
				// Update physical link
				ItrTd->Link = OHCI_POOL_TDINDEX(Controller, Td->Index);

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
	ItrTd->Flags &= ~OHCI_TD_IOC_NONE;

	// Add a null-transaction (Out, Zero)
	// But do NOT update the endpoint toggle, and Isoc does not need this
	if (Transfer->Transfer.Type != IsochronousTransfer) {
		ZeroTd = OhciTdIo(Controller, Transfer->Transfer.Type, OHCI_TD_OUT, 
		UsbManagerGetToggle(Transfer->Device, Transfer->Pipe), 0, 0);

		// Update physical link
		ItrTd->Link = OHCI_POOL_TDINDEX(Controller, ZeroTd->Index);

		// Not first, update links
		ItrTd->LinkIndex = ZeroTd->Index;
	}
	
	// Extract address and endpoint
	Address = HIWORD(Transfer->Pipe);
	Endpoint = LOWORD(Transfer->Pipe);

	// Finalize the endpoint-descriptor
	OhciQhInitialize(Controller, EndpointDescriptor,
		(int)FirstTd->Index, Transfer->Transfer.Type, Address, Endpoint, 
		Transfer->Transfer.Endpoint.MaxPacketSize, Transfer->Transfer.Speed);

	// Send the transaction and wait for completion
	return OhciTransactionDispatch(Controller, Transfer);
}

/* UsbDequeueTransferGeneric 
 * Removes a queued transfer from the controller's framelist */
UsbTransferStatus_t
UsbDequeueTransferGeneric(
	_In_ UsbManagerTransfer_t *Transfer)
{
	// Variables
	OhciQueueHead_t *Qh             = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
	OhciController_t *Controller    = NULL;

	// Get Controller
	Controller = (OhciController_t*)UsbManagerGetController(Transfer->Device);

	// Mark for unscheduling and
	// enable SOF, ED is not scheduled before
	Qh->HcdInformation |= OHCI_QH_UNSCHEDULE;
	Controller->Registers->HcInterruptStatus = OHCI_SOF_EVENT;
	Controller->Registers->HcInterruptEnable = OHCI_SOF_EVENT;
	return TransferFinished;
}
