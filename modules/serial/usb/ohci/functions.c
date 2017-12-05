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
#include <os/utils.h>
#include "ohci.h"

/* Includes
 * - Library */
#include <ds/collection.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* OhciQueueDebug
 * Dumps the QH-settings and all the attached td's */
void
OhciQueueDebug(
    OhciController_t *Controller,
    OhciQueueHead_t *Qh)
{
    // Variables
    OhciTransferDescriptor_t *Td = NULL;
    uintptr_t PhysicalAddress = 0;

    PhysicalAddress = OHCI_POOL_QHINDEX(Controller, Qh->Index);
    TRACE("QH(0x%x): Flags 0x%x, NextQh 0x%x, FirstChild 0x%x", 
        PhysicalAddress, Qh->Flags, Qh->LinkPointer, Qh->Current);

    // Get first td
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    while (Td != NULL) {
        PhysicalAddress = OHCI_POOL_TDINDEX(Controller, Td->Index);
        TRACE("TD(0x%x): Link 0x%x, Flags 0x%x, Cbp 0x%x, BufferEnd 0x%x", 
            PhysicalAddress, Td->Link, Td->Flags, Td->Cbp, Td->BufferEnd);
        // Go to next td
        if (Td->LinkIndex != OHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
        }
        else {
            Td = NULL;
        }
    }
}

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
	Qh->Bandwidth = (UsbCalculateBandwidth(Transfer->Speed, 
		Transfer->Endpoint.Direction, Transfer->Type, 
		Transfer->Endpoint.MaxPacketSize) / 1000);
    Qh->Interval = (uint16_t)Transfer->Endpoint.Interval;
	return Qh;
}

/* OhciTransactionCount
 * Returns the number of transactions neccessary for the transfer. */
OsStatus_t
OhciTransactionCount(
    _In_ OhciController_t       *Controller,
    _In_ UsbManagerTransfer_t   *Transfer,
    _Out_ int                   *TransactionsTotal)
{
    // Variables
    int TransactionCount    = 0;
    int i;

    // Get next address from which we need to load
    for (i = 0; i < Transfer->Transfer.TransactionCount; i++) {
        UsbTransactionType_t Type   = Transfer->Transfer.Transactions[i].Type;
        size_t BytesToTransfer      = Transfer->Transfer.Transactions[i].Length;
        size_t ByteOffset           = 0;
        size_t ByteStep             = 0;
        int AddZeroLength           = 0;

        // Keep adding td's
        while (BytesToTransfer || AddZeroLength == 1
            || Transfer->Transfer.Transactions[i].ZeroLength == 1) {
            if (Type == SetupTransaction) {
                ByteStep    = BytesToTransfer;
            }
            else {
                ByteStep    = MIN(BytesToTransfer, Transfer->Transfer.Endpoint.MaxPacketSize);
            }
            TransactionCount++;

            // Break out on zero lengths
            if (Transfer->Transfer.Transactions[i].ZeroLength == 1
                || AddZeroLength == 1) {
                break;
            }

            // Reduce
            BytesToTransfer -= ByteStep;
            ByteOffset      += ByteStep;

            // If it was out, and we had a multiple of MPS, then ZLP
            if (ByteStep == Transfer->Transfer.Endpoint.MaxPacketSize 
                && BytesToTransfer == 0
                && Transfer->Transfer.Type == BulkTransfer
                && Transfer->Transfer.Transactions[i].Type == OutTransaction) {
                AddZeroLength = 1;
            }
        }
    }
    *TransactionsTotal = TransactionCount;
    return OsSuccess;
}

/* OhciTransferFill 
 * Fills the transfer with as many transfer-descriptors as possible/needed. */
OsStatus_t
OhciTransferFill(
    _In_ OhciController_t           *Controller,
    _InOut_ UsbManagerTransfer_t    *Transfer)
{
    // Variables
    OhciTransferDescriptor_t *InitialTd     = NULL;
    OhciTransferDescriptor_t *PreviousTd    = NULL;
    OhciTransferDescriptor_t *Td            = NULL;
    OhciTransferDescriptor_t *ZeroTd        = NULL;
    size_t Address, Endpoint;
    int OutOfResources                      = 0;
    int i;

    // Debug
    TRACE("OhciTransferFill()");

    // Extract address and endpoint
    Address = HIWORD(Transfer->Pipe);
    Endpoint = LOWORD(Transfer->Pipe);

    // Start out by allocating a zero-td
    if (Transfer->Transfer.Type != IsochronousTransfer) {
        ZeroTd = OhciTdIo(Controller, Transfer->Transfer.Type, OHCI_TD_OUT, 
		        UsbManagerGetToggle(Transfer->DeviceId, Transfer->Pipe), 0, 0);
        if (ZeroTd == NULL) {
            return OsError;
        }
    }

    // Get next address from which we need to load
    for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
        UsbTransactionType_t Type   = Transfer->Transfer.Transactions[i].Type;
        size_t BytesToTransfer      = Transfer->Transfer.Transactions[i].Length;
        size_t ByteOffset           = 0;
        size_t ByteStep             = 0;
        int PreviousToggle          = -1;
        int Toggle                  = 0;

        // Adjust offsets
        ByteOffset                  = Transfer->BytesTransferred[i];
        BytesToTransfer            -= Transfer->BytesTransferred[i];
        if (BytesToTransfer == 0 && Transfer->Transfer.Transactions[i].ZeroLength != 1) {
            continue;
        }

        // If it's a handshake package AND it's first td
        // of package, then set toggle
        if (ByteOffset == 0 && Transfer->Transfer.Transactions[i].Handshake) {
            Transfer->Transfer.Transactions[i].Handshake = 0;
            PreviousToggle          = UsbManagerGetToggle(Transfer->DeviceId, Transfer->Pipe);
            UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, 1);
        }

        // Keep adding td's
        while (BytesToTransfer || Transfer->Transfer.Transactions[i].ZeroLength == 1) {
            Toggle          = UsbManagerGetToggle(Transfer->DeviceId, Transfer->Pipe);
            if (Type == SetupTransaction) {
                ByteStep    = BytesToTransfer;
                Td          = OhciTdSetup(Controller, &Transfer->Transfer.Transactions[i]);
            }
            else {
                ByteStep    = MIN(BytesToTransfer, Transfer->Transfer.Endpoint.MaxPacketSize);
                Td          = OhciTdIo(Controller, Transfer->Transfer.Type, 
                    (Type == InTransaction ? OHCI_TD_IN : OHCI_TD_OUT), Toggle, 
                    Transfer->Transfer.Transactions[i].BufferAddress + ByteOffset, ByteStep);
            }

            // If we didn't allocate a td, we ran out of 
            // resources, and have to wait for more. Queue up what we have
            if (Td == NULL) {
                if (PreviousToggle != -1) {
                    UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, PreviousToggle);
                    Transfer->Transfer.Transactions[i].Handshake = 1;
                }
                OutOfResources = 1;
                break;
            }
            else {
                // Store first
                if (InitialTd == NULL) {
                    InitialTd   = Td;
                    PreviousTd  = Td;
                }
                else {
                    // Update physical link
                    PreviousTd->LinkIndex   = Td->Index;
                    PreviousTd->Link        = OHCI_POOL_TDINDEX(Controller, Td->Index);
                    PreviousTd              = Td;
                }

                // Update toggle by flipping
                UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, Toggle ^ 1);

                // Break out on zero lengths
                if (Transfer->Transfer.Transactions[i].ZeroLength == 1) {
                    Transfer->Transfer.Transactions[i].ZeroLength = 0;
                    break;
                }

                // Reduce
                BytesToTransfer -= ByteStep;
                ByteOffset      += ByteStep;

                // If it was out, and we had a multiple of MPS, then ZLP
                if (ByteStep == Transfer->Transfer.Endpoint.MaxPacketSize 
                    && BytesToTransfer == 0
                    && Transfer->Transfer.Type == BulkTransfer
                    && Transfer->Transfer.Transactions[i].Type == OutTransaction) {
                    Transfer->Transfer.Transactions[i].ZeroLength = 1;
                }
            }
        }

        // Cancel?
        if (OutOfResources == 1) {
            break;
        }
    }

    // If we ran out of resources it can be pretty serious
    // Add a null-transaction (Out, Zero)
    if (Transfer->Transfer.Type != IsochronousTransfer) {
        if (OutOfResources == 1) {
            // If we allocated zero we have to unallocate zero and try again later
            if (InitialTd == NULL) {
                // Unallocate, do nothing
                memset(ZeroTd, 0, sizeof(OhciTransferDescriptor_t));
                return OsError;
            }
        }
        PreviousTd->Link            = OHCI_POOL_TDINDEX(Controller, ZeroTd->Index);
        PreviousTd->LinkIndex       = ZeroTd->Index;
        PreviousTd->Flags           &= ~OHCI_TD_IOC_NONE;
        PreviousTd->OriginalFlags   = PreviousTd->Flags;
    }

    // End of <transfer>?
    if (InitialTd != NULL) {
        OhciQhInitialize(Controller, Transfer->EndpointDescriptor, (int)InitialTd->Index, Transfer->Transfer.Type, 
            Address, Endpoint, Transfer->Transfer.Endpoint.MaxPacketSize, Transfer->Transfer.Speed);
        return OsSuccess;
    }
    else {
        // Queue up for later
        return OsError;
    }
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

    // Initiate some variables
	Qh = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
	QhAddress = OHCI_POOL_QHINDEX(Controller, Qh->Index);

	// Clear pauses
	Qh->Flags &= ~OHCI_QH_SKIP;
	Qh->Current &= ~OHCI_LINK_HALTED;

	// Trace
	TRACE("Qh Address 0x%x, Flags 0x%x, Tail 0x%x, Current 0x%x, Link 0x%x", 
        QhAddress, Qh->Flags, Qh->EndPointer, Qh->Current, Qh->LinkPointer);

	// Set the schedule flag on ED and
	// enable SOF, ED is not scheduled before this interrupt
	Qh->HcdInformation |= OHCI_QH_SCHEDULE;
	Controller->Registers->HcInterruptStatus = OHCI_SOF_EVENT;
	Controller->Registers->HcInterruptEnable = OHCI_SOF_EVENT;

	// Done
    Transfer->Status = TransferQueued;
	return TransferQueued;
}

/* OhciTransactionFinalize
 * Cleans up the transfer, deallocates resources and validates the td's */
OsStatus_t
OhciTransactionFinalize(
	_In_ OhciController_t       *Controller,
	_In_ UsbManagerTransfer_t   *Transfer,
	_In_ int                     Validate)
{
	// Variables
	OhciQueueHead_t *Qh             = (OhciQueueHead_t*)Transfer->EndpointDescriptor;
	OhciTransferDescriptor_t *Td    = NULL;
    CollectionItem_t *Node          = NULL;
    int ShortTransfer               = 0;
    int BytesLeft                   = 0;
    int i;
	UsbTransferResult_t Result;

    // Debug
    TRACE("OhciTransactionFinalize()");

    // Set status to finished initially
    Transfer->Status = TransferFinished;

	/*************************
	 *** VALIDATION PHASE ****
	 *************************/
	if (Validate != 0) {
		// Get first td, iterate untill null-td
		Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
		while (Td->LinkIndex != OHCI_NO_INDEX) {
            // Extract the error code
			int ErrorCode = OHCI_TD_ERRORCODE(Td->Flags);
            Transfer->TransactionsExecuted++;

			// Calculate length transferred 
			// Take into consideration the N-1 
			if (Td->BufferEnd != 0) {
                int BytesTransferred    = 0;
                int BytesRequested      = (Td->BufferEnd - Td->OriginalCbp) + 1;
                if (Td->Cbp == 0) {
                    BytesTransferred = BytesRequested;
                }
                else {
                    BytesTransferred = Td->Cbp - Td->OriginalCbp;
                }
				if (BytesTransferred < BytesRequested) {
                    ShortTransfer = 1;
                }
                for (i = 0; i < USB_TRANSACTIONCOUNT; i++) {
                    if (Transfer->Transfer.Transactions[i].Length > Transfer->BytesTransferred[i]) {
                        Transfer->BytesTransferred[i] += BytesTransferred;
                        break;
                    }
                }
			}
			
			// Trace
			TRACE("Flags 0x%x, Cbp 0x%x, BufferEnd 0x%x, Condition Code %u", 
				Td->Flags, Td->Cbp, Td->BufferEnd, ErrorCode);

			// Now validate the code
			if (ErrorCode != 0 && Transfer->Status == TransferFinished) {
				Transfer->Status = OhciGetStatusCode(ErrorCode);
			}
			Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
		}
	}

	// Step one is to unallocate the td's
	// Get first td
	Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
	while (Td) {
		// Save link-index before resetting
		int LinkIndex = Td->LinkIndex;
		memset((void*)Td, 0, sizeof(OhciTransferDescriptor_t));

		// Go to next td or terminate
		if (LinkIndex != OHCI_NO_INDEX) {
			Td = &Controller->QueueControl.TDPool[LinkIndex];
		}
		else {
			break;
		}
	}

    // Is the transfer done?
    if ((Transfer->Transfer.Type == ControlTransfer
        || Transfer->Transfer.Type == BulkTransfer)
        && Transfer->Status == TransferFinished
        && Transfer->TransactionsExecuted != Transfer->TransactionsTotal) {
        BytesLeft = 1;
    }

    // We don't allocate the queue head before the transfer
    // is done, we might not be done yet
    if (BytesLeft == 1 && ShortTransfer == 0 && Validate == 1) {
        // Queue up more data
        if (OhciTransferFill(Controller, Transfer) == OsSuccess) {
            OhciTransactionDispatch(Controller, Transfer);
        }
        return OsError;
    }
    else {
        // Now unallocate the ED by zeroing that
        memset((void*)Qh, 0, sizeof(OhciQueueHead_t));
        Transfer->EndpointDescriptor = NULL;

        // Should we notify the user here?...
        if (Transfer->Requester != UUID_INVALID
            && (Transfer->Transfer.Type == ControlTransfer
                || Transfer->Transfer.Type == BulkTransfer)) {
            Result.Id = Transfer->Id;
            Result.BytesTransferred = Transfer->BytesTransferred[0];
            Result.BytesTransferred += Transfer->BytesTransferred[1];
            Result.BytesTransferred += Transfer->BytesTransferred[2];
            Result.Status = Transfer->Status;
            PipeSend(Transfer->Requester, Transfer->ResponsePort, 
                (void*)&Result, sizeof(UsbTransferResult_t));
        }

        // Cleanup the transfer
        free(Transfer);

        // Now run through transactions and check if any are ready to run
        _foreach(Node, Controller->QueueControl.TransactionList) {
            UsbManagerTransfer_t *NextTransfer = (UsbManagerTransfer_t*)Node->Data;
            if (NextTransfer->Status == TransferNotProcessed) {
                if (OhciTransferFill(Controller, Transfer) == OsSuccess) {
                    OhciTransactionDispatch(Controller, Transfer);
                }
            }
        }
	    return OsSuccess;
    }
}

/* UsbQueueTransferGeneric 
 * Queues a new transfer for the given driver
 * and pipe. They must exist. The function does not block*/
UsbTransferStatus_t
UsbQueueTransferGeneric(
	_InOut_ UsbManagerTransfer_t *Transfer)
{
	// Variables
	OhciQueueHead_t *EndpointDescriptor     = NULL;
	OhciController_t *Controller            = NULL;
    DataKey_t Key;

	// Get Controller
	Controller = (OhciController_t*)UsbManagerGetController(Transfer->DeviceId);

	// Initialize
	EndpointDescriptor = OhciTransactionInitialize(Controller, &Transfer->Transfer);

	// Update the stored information
	Transfer->EndpointDescriptor = EndpointDescriptor;
	Transfer->Status = TransferNotProcessed;

	// If it's a control transfer set initial toggle 0
	if (Transfer->Transfer.Type == ControlTransfer) {
		UsbManagerSetToggle(Transfer->DeviceId, Transfer->Pipe, 0);
	}

    // Store transaction in queue
    Key.Value = 0;
    CollectionAppend(Controller->QueueControl.TransactionList, 
        CollectionCreateNode(Key, Transfer));

    // Count the transaction count
    OhciTransactionCount(Controller, Transfer, &Transfer->TransactionsTotal);

    // Fill transfer
    if (OhciTransferFill(Controller, Transfer) != OsSuccess) {
        return TransferQueued;
    }

	// Send the transaction and wait for completion
    //OhciQueueDebug(Controller, EndpointDescriptor);
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
	Controller = (OhciController_t*)UsbManagerGetController(Transfer->DeviceId);

	// Mark for unscheduling and
	// enable SOF, ED is not scheduled before
	Qh->HcdInformation |= OHCI_QH_UNSCHEDULE;
	Controller->Registers->HcInterruptStatus = OHCI_SOF_EVENT;
	Controller->Registers->HcInterruptEnable = OHCI_SOF_EVENT;
	return TransferFinished;
}
