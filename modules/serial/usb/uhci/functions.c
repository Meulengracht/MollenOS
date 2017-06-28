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

/* OhciErrorMessages
 * Textual representations of the possible error codes */
const char *OhciErrorMessages[] = {
	"No Error",
	"CRC Error",
	"Bit Stuffing Violation",
	"Data Toggle Mismatch",
	"Stall PID recieved",
	"Device Not Responding",
	"PID Check Failure",
	"Unexpected PID",
	"Data Overrun",
	"Data Underrun",
	"Reserved",
	"Reserved",
	"Buffer Overrun",
	"Buffer Underrun",
	"Not Accessed",
	"Not Accessed"
};

/* OhciTransactionInitialize
 * Initializes a transaction by allocating a new endpoint-descriptor
 * and preparing it for usage */
OhciEndpointDescriptor_t*
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
	Ed->Interval = (uint16_t)Transfer->Endpoint.Interval;

	// Done
	return Ed;
}

/* OhciTransactionDispatch
 * Queues the transfer up in the controller hardware, after finalizing the
 * transactions and preparing them. */
OsStatus_t
OhciTransactionDispatch(
	_In_ OhciController_t *Controller,
	_In_ UsbManagerTransfer_t *Transfer)
{
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
