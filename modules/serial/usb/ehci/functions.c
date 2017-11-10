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
#include <os/utils.h>
#include "ehci.h"

/* Includes
 * - Library */
#include <ds/list.h>
#include <stddef.h>
#include <string.h>

/* EhciTransactionInitialize
 * Initializes a transaction by allocating a new endpoint-descriptor
 * and preparing it for usage */
OsStatus_t
EhciTransactionInitialize(
    _In_ EhciController_t *Controller,
    _In_ UsbTransfer_t *Transfer,
    _In_ size_t Pipe,
    _Out_ EhciQueueHead_t **QhOut)
{
    // Variables
    EhciQueueHead_t *Qh = NULL;
    size_t TransactionCount = 0;
    size_t Address, Endpoint;

    // Extract address and endpoint
    Address = HIWORD(Pipe);
    Endpoint = LOWORD(Pipe);

    // Calculate transaction count
    TransactionCount = DIVUP(Transfer->Length, Transfer->Endpoint.MaxPacketSize);

    // We handle Isochronous transfers a bit different
    if (Transfer->Type != IsochronousTransfer) {
        *QhOut = Qh = EhciQhAllocate(Controller);

        // Calculate the bus-time
        if (Transfer->Type == InterruptTransfer) {
            if (EhciQhInitialize(Controller, Qh, Transfer->Speed, 
                Transfer->Endpoint.Direction,
                Transfer->Type, Transfer->Endpoint.Interval,
                Transfer->Endpoint.MaxPacketSize, Transfer->Length) != OsSuccess) {
                return OsError;
            }
        }

        // Initialize the QH
        Qh->Flags = EHCI_QH_DEVADDR(Address);
        Qh->Flags |= EHCI_QH_EPADDR(Endpoint);
        Qh->Flags |= EHCI_QH_DTC;

        // The thing with maxlength is
        // that it needs to be MIN(TransferLength, MPS)
        Qh->Flags |= EHCI_QH_MAXLENGTH(Transfer->Endpoint.MaxPacketSize);

        // Now, set additionals depending on speed
        if (Transfer->Speed == LowSpeed 
            || Transfer->Speed == FullSpeed) {
            if (Transfer->Type == ControlTransfer) {
                Qh->Flags |= EHCI_QH_CONTROLEP;
            }

            // On low-speed, set this bit
            if (Transfer->Speed == LowSpeed) {
                Qh->Flags |= EHCI_QH_LOWSPEED;
            }

            // Set nak-throttle to 0
            Qh->Flags |= EHCI_QH_RL(0);

            // We need to fill the TT's hub-addr
            // and port-addr (@todo)

            // Last thing to do in full/low is to set multiplier to 1
            Qh->State = EHCI_QH_MULTIPLIER(1);
        }
        else {
            // High speed device, no transaction translator
            Qh->Flags |= EHCI_QH_HIGHSPEED;

            // Set nak-throttle to 4 if control or bulk
            if (Transfer->Type == ControlTransfer 
                || Transfer->Type == BulkTransfer) {
                Qh->Flags |= EHCI_QH_RL(4);
            }
            else {
                Qh->Flags |= EHCI_QH_RL(0);
            }

            // If the endpoint is interrupt use the bandwidth specifier
            // otherwise set multiplier to 1
            if (Transfer->Type == InterruptTransfer) {
                Qh->State = EHCI_QH_MULTIPLIER(Transfer->Endpoint.Bandwidth);
            }
            else {
                Qh->State = EHCI_QH_MULTIPLIER(1);
            }
        }
    }
    else {
        // Isochronous transfers (@todo)
    }

    // Done
    return OsSuccess;
}

/* EhciTransactionDispatch
 * Queues the transfer up in the controller hardware, after finalizing the
 * transactions and preparing them. */
UsbTransferStatus_t
EhciTransactionDispatch(
    _In_ EhciController_t *Controller,
    _In_ UsbManagerTransfer_t *Transfer)
{
    // Variables
    EhciTransferDescriptor_t *Td = NULL;
    EhciQueueHead_t *Qh = NULL;
    uintptr_t QhAddress;
    DataKey_t Key;

    /*************************
	 ****** SETUP PHASE ******
	 *************************/
    Qh = (EhciQueueHead_t *)Transfer->EndpointDescriptor;
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];

    // Lookup physical
    QhAddress = EHCI_POOL_QHINDEX(Controller, Qh->Index);

    // Handle the initialization a bit differently for isoc
    if (Transfer->Transfer.Type != IsochronousTransfer) {
        Transfer->Status = TransferNotProcessed;

        // Initialize overlay
        memset(&Qh->Overlay, 0, sizeof(EhciQueueHeadOverlay_t));
        Qh->Overlay.NextTD = EHCI_POOL_TDINDEX(Controller, Td->Index);
        Qh->Overlay.NextAlternativeTD = EHCI_LINK_END;

        // Debug
        TRACE("Qh Address 0x%x, Flags 0x%x, State 0x%x, Current 0x%x, Next 0x%x",
              QhAddress, Qh->Flags, Qh->State, Qh->CurrentTD, Qh->Overlay.NextTD);
    }
    else {
        // Initialize isochronous transfer
        // @todo
    }

    // Store transaction in queue
    Key.Value = 0;
    ListAppend(Controller->QueueControl.TransactionList,
               ListCreateNode(Key, Key, Transfer));

    // Trace
    TRACE("UHCI: QH at 0x%x, FirstTd 0x%x, NextQh 0x%x",
          QhAddress, Qh->CurrentTD, Qh->LinkPointer);
    TRACE("UHCI: Bandwidth %u, StartFrame %u, Flags 0x%x",
          Qh->Bandwidth, Qh->sFrame, Qh->Flags);

    /*************************
	 **** LINKING PHASE ******
	 *************************/

    // Acquire the spinlock for atomic queue access
    SpinlockAcquire(&Controller->Base.Lock);

    // Bulk and control are asynchronous transfers
    if (Transfer->Transfer.Type == ControlTransfer 
        || Transfer->Transfer.Type == BulkTransfer) {
        // Transfer existing links
        Qh->LinkPointer = Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkPointer;
        Qh->LinkIndex = Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkIndex;
        MemoryBarrier();

        // Insert at the start of queue
        Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkIndex = Qh->Index;
        Controller->QueueControl.QHPool[EHCI_POOL_QH_ASYNC].LinkPointer = QhAddress | EHCI_LINK_QH;

        // Enable the asynchronous scheduler
        Controller->QueueControl.AsyncTransactions++;
        EhciEnableAsyncScheduler(Controller);
    }
    else {
        // Isochronous or interrupt, but handle each differently
        if (Transfer->Transfer.Type == InterruptTransfer) {
            size_t StartFrame = 0, FrameMask = 0;
            size_t EpBandwidth = Transfer->Transfer.Endpoint.Bandwidth;

            // If we use completion masks
            // we'll need another transfer for start
            if (Transfer->Transfer.Speed != HighSpeed) {
                EpBandwidth++;
            }

            // Allocate the needed bandwidth
            if (UsbSchedulerReserveBandwidth(Controller->Scheduler,
                                             Qh->Interval, Qh->Bandwidth, EpBandwidth,
                                             &Qh->sFrame, &Qh->sMask) != OsSuccess) {
                ERROR("EHCI::Failed to allocate bandwidth for qh");
                for (;;);
            }

            // Save scheduling information for cleanup
            Qh->sFrame = StartFrame;
            Qh->sMask = FrameMask;

            // Calculate both the frame start and completion mask
            Qh->FrameStartMask = (uint8_t)FirstSetBit(FrameMask);
            if (Transfer->Transfer.Speed != HighSpeed) {
                Qh->FrameCompletionMask = (uint8_t)(FrameMask & 0xFF);
                Qh->FrameCompletionMask &= ~(1 << Qh->FrameStartMask);
            }
            else {
                Qh->FrameCompletionMask = 0;
            }

            // Link the periodic queuehead
            EhciLinkPeriodicQh(Controller, Qh);
        }
        else {
            ERROR("EHCI::Scheduling Isochronous");
            for (;;);
        }
    }

    // All queue operations are now done
    SpinlockRelease(&Controller->Base.Lock);

// Manually inspect
#ifdef __DEBUG
    ThreadSleep(5000);
    LogInformation("EHCI", "Qh Address 0x%x, Flags 0x%x, State 0x%x, Current 0x%x, Next 0x%x\n",
                   QhAddress, Qh->Flags, Qh->State, Qh->CurrentTD, Qh->Overlay.NextTD);
#endif

    // Done
    return TransferQueued;
}

/* EhciTransactionFinalize
 * Cleans up the transfer, deallocates resources and validates the td's */
OsStatus_t
EhciTransactionFinalize(
    _In_ EhciController_t *Controller,
    _In_ UsbManagerTransfer_t *Transfer,
    _In_ int Validate)
{
    // Variables
    UsbTransferStatus_t Completed = TransferNotProcessed;
    EhciTransferDescriptor_t *Td = NULL;
    EhciQueueHead_t *Qh = NULL;
    unsigned Index = 0;
    int CondCode = 0;

    // Retrieve both qh and first td
    Qh = (EhciQueueHead_t *)Transfer->EndpointDescriptor;
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];

    /*************************
	 *** VALIDATION PHASE ****
	 *************************/

    // Iterate td's and sanitize their op-codes
	while (Td) {
        CondCode = EhciConditionCodeToIndex(Transfer->Transfer.Speed == HighSpeed ? Td->Status & 0xFC : Td->Status);

        // Calculate the number of bytes transfered
        if ((Td->OriginalLength & EHCI_TD_LENGTHMASK) != 0) {
            size_t BytesRemaining = Td->Length & EHCI_TD_LENGTHMASK;
            Transfer->BytesTransferred += (Td->OriginalLength & EHCI_TD_LENGTHMASK) - BytesRemaining;
        }

        // Debug
        TRACE("Td (Id %u) Token 0x%x, Status 0x%x, Length 0x%x, Buffer 0x%x, Link 0x%x\n",
            Td->Index, Td->Token, Td->Status, Td->Length, Td->Buffers[0], Td->Link);

        // Validate the condition code based on previous validation
        if (CondCode == 0 && Completed == TransferFinished) {
            Completed = TransferFinished;
        }
        else {
            Completed = EhciGetStatusCode(CondCode);
            break;
        }

        // Switch to next transfer descriptor
        if (Td->LinkIndex != EHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[Td->LinkIndex];
        }
        else {
            Td = NULL;
            break;
        }
    }

    // Finalize transfer status
    Transfer->Status = Completed;

#ifdef __DEBUG
    for (;;);
#endif
    /*************************
	 ***** CLEANUP PHASE *****
     *************************/
    if (Transfer->Transfer.Type == ControlTransfer 
        || Transfer->Transfer.Type == BulkTransfer) {
        // Mark Qh for unscheduling, this will then be handled
        // at the next door-bell
        Qh->HcdFlags |= EHCI_QH_UNSCHEDULE;
        EhciRingDoorbell(Controller);
        //@todo
        //WaitForDoorBell();
    }
    else {
        // Unlinking periodics is an atomic operation
        SpinlockAcquire(&Controller->Base.Lock);
        EhciUnlinkPeriodic(Controller, (uintptr_t)Qh, 
                           Qh->Interval, Qh->sFrame);
        UsbSchedulerReleaseBandwidth(Controller->Scheduler,
                                     Qh->Interval, Qh->Bandwidth, 
                                     Qh->sFrame, Qh->sMask);
        SpinlockRelease(&Controller->Base.Lock);
    }
    
    // Free all the td's while we hopefully get unscheduled
    Td = &Controller->QueueControl.TDPool[Qh->ChildIndex];
    while (Td) {
        int LinkIndex = Td->LinkIndex;

        // Reset structure but store index
        Index = Td->Index;
        memset((void *)Td, 0, sizeof(EhciTransferDescriptor_t));
        Td->Index = Index;

        // Switch to next transfer descriptor
        if (LinkIndex != EHCI_NO_INDEX) {
            Td = &Controller->QueueControl.TDPool[LinkIndex];
        }
        else {
            Td = NULL;
            break;
        }
    }

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
    EhciQueueHead_t *Qh = NULL;
    EhciTransferDescriptor_t *FirstTd = NULL, *ItrTd = NULL;
    EhciController_t *Controller = NULL;
    size_t Address, Endpoint;
    int i;

    // Get Controller
    Controller = (EhciController_t *)UsbManagerGetController(Transfer->Device);

    // Initialize
    if (EhciTransactionInitialize(Controller, &Transfer->Transfer, 
        Transfer->Pipe, &Qh) != OsSuccess) {
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
            EhciTransferDescriptor_t *Td = NULL;
            size_t BytesStep = 0;
            int Toggle;

            // Get toggle status
            Toggle = UsbManagerGetToggle(Transfer->Device, Transfer->Pipe);

            // Allocate a new Td
            if (Transfer->Transfer.Transactions[i].Type == SetupTransaction) {
                Td = EhciTdSetup(Controller, &Transfer->Transfer.Transactions[i]);

                // Consume entire setup-package
                BytesStep = BytesToTransfer;
            }
            else {
                Td = EhciTdIo(Controller, &Transfer->Transfer,
                              &Transfer->Transfer.Transactions[i], Toggle);
                BytesStep = (Td->Length & EHCI_TD_LENGTHMASK);
            }

            // Store first
            if (FirstTd == NULL) {
                FirstTd = Td;
                ItrTd = Td;
            }
            else {
                // Update physical link
                ItrTd->LinkIndex = Td->Index;
                ItrTd->Link = EHCI_POOL_TDINDEX(Controller, ItrTd->LinkIndex);
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
    ItrTd->Token |= EHCI_TD_IOC;
    ItrTd->OriginalToken |= EHCI_TD_IOC;

    // Finalize the endpoint-descriptor
    Qh->ChildIndex = FirstTd->Index;
    Qh->CurrentTD = EHCI_POOL_TDINDEX(Controller, FirstTd->Index);

    // Send the transaction and wait for completion
    return EhciTransactionDispatch(Controller, Transfer);
}

/* UsbDequeueTransferGeneric 
  * Removes a queued transfer from the controller's framelist */
UsbTransferStatus_t
UsbDequeueTransferGeneric(
    _In_ UsbManagerTransfer_t *Transfer)
{
    // Variables
    EhciQueueHead_t *Qh =
        (EhciQueueHead_t *)Transfer->EndpointDescriptor;
    EhciController_t *Controller = NULL;

    // Get Controller
    Controller = (EhciController_t *)UsbManagerGetController(Transfer->Device);

    // Mark for unscheduling
    Qh->HcdFlags |= EHCI_QH_UNSCHEDULE;

    // Notice finalizer-thread? Or how to induce interrupt
    // @todo

    // Done, rest of cleanup happens in Finalize
    return TransferFinished;
}
