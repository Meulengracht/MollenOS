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
#include "ehci.h"

/* Includes
 * - Library */
#include <ds/list.h>
#include <string.h>

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

	DataKey_t Key;
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
	Key.Value = 0;
	ListAppend((List_t*)Controller->TransactionList, ListCreateNode(Key, Key, Request));

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
			/* Vars */
			size_t StartFrame = 0, FrameMask = 0;
			size_t EpBandwidth = Request->Endpoint->Bandwidth;

			/* If we use completion masks
			 * we'll need another transfer for start */
			if (Request->Speed != HighSpeed)
				EpBandwidth++;

			/* Allocate Bandwidth */
			if (UsbSchedulerReserveBandwidth(Controller->Scheduler, Qh->Interval, Qh->Bandwidth,
				EpBandwidth, &StartFrame, &FrameMask)) {
				LogFatal("EHCI", "Failed to schedule Qh, lack of scheduler-space.");
				return;
			}

			/* Store scheduling info */
			Qh->sFrame = StartFrame;
			Qh->sMask = FrameMask;

			/* Set start mask of Qh */
			Qh->FStartMask = (uint8_t)FirstSetBit(FrameMask);

			/* Set completion mask of Qh */
			if (Request->Speed != HighSpeed) {
				Qh->FCompletionMask = (uint8_t)(FrameMask & 0xFF);
				Qh->FCompletionMask &= ~(1 << Qh->FStartMask);
			}
			else
				Qh->FCompletionMask = 0;

			/* Update saved copies, now all is prepaired */
			Transaction = Request->Transactions;
			while (Transaction)
			{
				/* Do an exact copy */
				memcpy(Transaction->TransferDescriptorCopy, 
					Transaction->TransferDescriptor, sizeof(EhciTransferDescriptor_t));

				/* Next */
				Transaction = Transaction->Link;
			}

			/* Link */
			EhciLinkPeriodicQh(Controller, Qh);
		}
		else
		{
			LogFatal("EHCI", "Scheduling Isochronous");
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
	SchedulerSleepThread((Addr_t*)Request->Data, 5000);

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
	ListNode_t *Node = NULL;

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
			EhciUnlinkPeriodic(Controller, (Addr_t)Qh, Qh->Interval, Qh->sFrame);

			/* Release Bandwidth */
			UsbSchedulerReleaseBandwidth(Controller->Scheduler, 
				Qh->Interval, Qh->Bandwidth, Qh->sFrame, Qh->sMask);

			/* Release lock */
			SpinlockRelease(&Controller->Lock);

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
	_foreach(Node, ((List_t*)Controller->TransactionList)) {
		if (Node->Data == Request)
			break;
	}

	/* Sanity */
	if (Node != NULL) {
		ListRemoveByNode((List_t*)Controller->TransactionList, Node);
		kfree(Node);
	}
}
