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
#include <os/condition.h>
#include <os/thread.h>
#include <os/utils.h>

#include "../common/manager.h"
#include "ohci.h"

/* Includes
 * - Library */
#include <ds/list.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Globals
 * Use these for state-keeping the thread */
static UUId_t __GlbFinalizerThreadId = UUID_INVALID;
static Condition_t *__GlbFinalizerEvent = NULL;

/* FinalizerEntry 
 * Entry of the finalizer thread, this thread handles
 * all completed transactions to notify users */
int FinalizerEntry(void *Argument)
{
	// Variables
	ListNode_t *cNode = NULL;
	Mutex_t EventLock;

	// Unused
	_CRT_UNUSED(Argument);

	// Create the mutex
	MutexConstruct(&EventLock, MUTEX_PLAIN);

	// Forever-loop
	while (1) {
		// Wait for events
		ConditionWait(__GlbFinalizerEvent, &EventLock);

		// Iterate through all transactions for all controllers
		_foreach(cNode, UsbManagerGetControllers()) {
			// Instantiate a controller pointer
			OhciController_t *Controller = 
				(OhciController_t*)cNode->Data;
			
			// Iterate transactions
			foreach_nolink(tNode, Controller->QueueControl.TransactionList) {
				// Instantiate a transaction pointer
				UsbManagerTransfer_t *Transfer = 
					(UsbManagerTransfer_t*)tNode->Data;

				// Cleanup?
				if (Transfer->Cleanup) {
					// Temporary copy of pointer
					ListNode_t *Temp = tNode;

					// Notify requester and finalize
					OhciTransactionFinalize(Controller, Transfer, 1);
				
					// Remove from list (in-place, tricky)
					tNode = ListUnlinkNode(
						Controller->QueueControl.TransactionList,
						tNode);

					// Cleanup
					ListDestroyNode(
						Controller->QueueControl.TransactionList, 
						Temp);
				}
				else {
					tNode = ListNext(tNode);
				}
			}
		}
	}

	// Done
	return 0;
}

/* OnFastInterrupt
 * Is called for the sole purpose to determine if this source
 * has invoked an irq. If it has, silence and return (Handled) */
InterruptStatus_t
OnFastInterrupt(
    _In_Opt_ void *InterruptData)
{
	// Variables
	OhciController_t *Controller = NULL;
	reg32_t InterruptStatus;

	// Instantiate the pointer
	Controller = (OhciController_t*)InterruptData;

	// There are two cases where it might be, just to be sure
	// we don't miss an interrupt, if the HeadDone is set or the
	// intr is set
	if (Controller->Hcca->HeadDone != 0) {
		InterruptStatus = OHCI_INTR_PROCESS_HEAD;
		// If halted bit is set, get rest of interrupt
		if (Controller->Hcca->HeadDone & 0x1) {
			InterruptStatus |= (Controller->Registers->HcInterruptStatus
				& Controller->Registers->HcInterruptEnable);
		}
	}
	else {
		// Was it this Controller that made the interrupt?
		// We only want the interrupts we have set as enabled
		InterruptStatus = (Controller->Registers->HcInterruptStatus
			& Controller->Registers->HcInterruptEnable);
	}

	// Trace
	TRACE("Interrupt - Status 0x%x", InterruptStatus);

	// Was the interrupt even from this controller?
	if (!InterruptStatus) {
		return InterruptNotHandled;
    }

    // Acknowledge interrupts and clear
    Controller->Registers->HcInterruptDisable = (reg32_t)OHCI_INTR_MASTER;
    Controller->Base.InterruptStatus |= InterruptStatus;
	return InterruptHandled;
}

/* OnInterrupt
 * Is called by external services to indicate an external interrupt.
 * This is to actually process the device interrupt */
InterruptStatus_t 
OnInterrupt(
    _In_Opt_ void *InterruptData,
    _In_Opt_ size_t Arg0,
    _In_Opt_ size_t Arg1,
    _In_Opt_ size_t Arg2)
{
	// Variables
	OhciController_t *Controller = NULL;
    reg32_t InterruptStatus;
    
    // Unusued
    _CRT_UNUSED(Arg0);
    _CRT_UNUSED(Arg1);
	_CRT_UNUSED(Arg2);

	// Instantiate the pointer
    Controller = (OhciController_t*)InterruptData;
    InterruptStatus = Controller->Base.InterruptStatus;
    Controller->Base.InterruptStatus = 0;
    
    // Disable interrupts
    //Controller->Registers->HcInterruptDisable = (reg32_t)OHCI_INTR_MASTER;
    
    // Fatal Error Check
	if (InterruptStatus & OHCI_INTR_FATAL_ERROR) {
		// Reset controller and restart transactions
		ERROR("Fatal Error (OHCI)");
	}

	// Flag for end of frame Type interrupts
	if (InterruptStatus & (OHCI_INTR_SCHEDULING_OVERRUN | OHCI_INTR_PROCESS_HEAD
		| OHCI_INTR_SOF | OHCI_INTR_FRAME_OVERFLOW)) {
		InterruptStatus |= OHCI_INTR_MASTER;
	}

	// Scheduling Overrun?
	if (InterruptStatus & OHCI_INTR_SCHEDULING_OVERRUN) {
		ERROR("Scheduling Overrun");

		// Acknowledge
		Controller->Registers->HcInterruptStatus = OHCI_INTR_SCHEDULING_OVERRUN;
		InterruptStatus = InterruptStatus & ~(OHCI_INTR_SCHEDULING_OVERRUN);
	}

	// Resume Detection? 
	// We must wait 20 ms before putting Controller to Operational
	if (InterruptStatus & OHCI_INTR_RESUMEDETECT) {
		ThreadSleep(20);
		OhciSetMode(Controller, OHCI_CONTROL_ACTIVE);

		// Acknowledge
		Controller->Registers->HcInterruptStatus = OHCI_INTR_RESUMEDETECT;
		InterruptStatus = InterruptStatus & ~(OHCI_INTR_RESUMEDETECT);
	}

	// Frame Overflow
	// Happens when it rolls over from 0xFFFF to 0
	if (InterruptStatus & OHCI_INTR_FRAME_OVERFLOW) {
		// Acknowledge
		Controller->Registers->HcInterruptStatus = OHCI_INTR_FRAME_OVERFLOW;
		InterruptStatus = InterruptStatus & ~(OHCI_INTR_FRAME_OVERFLOW);
	}

	// This happens if a transaction has completed
	if (InterruptStatus & OHCI_INTR_PROCESS_HEAD) {
		reg32_t TdAddress = (Controller->Hcca->HeadDone & ~(0x00000001));
		OhciProcessDoneQueue(Controller, TdAddress);

		// Acknowledge
		Controller->Hcca->HeadDone = 0;
		Controller->Registers->HcInterruptStatus = OHCI_INTR_PROCESS_HEAD;
		InterruptStatus = InterruptStatus & ~(OHCI_INTR_PROCESS_HEAD);
	}

	// Root Hub Status Change
	// This occurs on disconnect/connect events
	if (InterruptStatus & OHCI_INTR_ROOTHUB_EVENT) {
		// PortCheck
		OhciPortsCheck(Controller);

		// Acknowledge
		Controller->Registers->HcInterruptStatus = OHCI_INTR_ROOTHUB_EVENT;
		InterruptStatus = InterruptStatus & ~(OHCI_INTR_ROOTHUB_EVENT);
	}

	// Start of frame only comes if we should link in or unlink
	// an interrupt td, this is a safe way of doing it
	if (InterruptStatus & OHCI_INTR_SOF) {
		// If this occured we have linking/unlinking to do!
		OhciProcessTransactions(Controller);

		// Acknowledge Interrupt
		// - But mask this interrupt again
		Controller->Registers->HcInterruptStatus = OHCI_INTR_SOF;
	}

	// Mask out remaining interrupts, we dont use them
	if (InterruptStatus & ~(OHCI_INTR_MASTER)) {
		Controller->Registers->HcInterruptDisable = InterruptStatus;
	}

    // Enable interrupts again
    Controller->Registers->HcInterruptEnable = (reg32_t)OHCI_INTR_MASTER;
	return InterruptHandled;
}

/* OnTimeout
 * Is called when one of the registered timer-handles
 * times-out. A new timeout event is generated and passed
 * on to the below handler */
OsStatus_t
OnTimeout(
	_In_ UUId_t Timer,
	_In_ void *Data)
{
	return OsSuccess;
}

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
OsStatus_t OnLoad(void)
{
	// Create event semaphore
	__GlbFinalizerEvent = ConditionCreate();

	// Start finalizer thread
	__GlbFinalizerThreadId = ThreadCreate(FinalizerEntry, NULL);

	// Initialize the device manager here
	return UsbManagerInitialize();
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t OnUnload(void)
{
	// Stop thread
	ThreadKill(__GlbFinalizerThreadId);

	// Cleanup semaphore
	ConditionDestroy(__GlbFinalizerEvent);

	// Cleanup the internal device manager
	return UsbManagerDestroy();
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
OsStatus_t OnRegister(MCoreDevice_t *Device)
{
	// Variables
	OhciController_t *Controller = NULL;
	
	// Register the new controller
	Controller = OhciControllerCreate(Device);

	// Sanitize
	if (Controller == NULL) {
		return OsError;
	}

	// Done - Register with service
	return UsbManagerCreateController(&Controller->Base);
}

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
OsStatus_t OnUnregister(MCoreDevice_t *Device)
{
	// Variables
	OhciController_t *Controller = NULL;
	
	// Lookup controller
	Controller = (OhciController_t*)UsbManagerGetController(Device->Id);

	// Sanitize lookup
	if (Controller == NULL) {
		return OsError;
	}

	// Unregister, then destroy
	UsbManagerDestroyController(&Controller->Base);

	// Destroy it
	return OhciControllerDestroy(Controller);
}

/* OnQuery
 * Occurs when an external process or server quries
 * this driver for data, this will correspond to the query
 * function that is defined in the contract */
OsStatus_t 
OnQuery(_In_ MContractType_t QueryType, 
		_In_ int QueryFunction, 
		_In_Opt_ RPCArgument_t *Arg0,
		_In_Opt_ RPCArgument_t *Arg1,
		_In_Opt_ RPCArgument_t *Arg2, 
		_In_ UUId_t Queryee, 
		_In_ int ResponsePort)
{
	// Variables
	UsbManagerTransfer_t *Transfer = NULL;
	OhciController_t *Controller = NULL;
	UUId_t Device = UUID_INVALID, Pipe = UUID_INVALID;
	OsStatus_t Result = OsError;

	// Instantiate some variables
	Device = (UUId_t)Arg0->Data.Value;
	Pipe = (UUId_t)Arg1->Data.Value;
	
	// Lookup controller
	Controller = (OhciController_t*)UsbManagerGetController(Device);

	// Sanitize we have a controller
	if (Controller == NULL) {
		// Null response
		return PipeSend(Queryee, ResponsePort, 
			(void*)&Result, sizeof(OsStatus_t));
	}

	switch (QueryFunction) {
		// Generic Queue
		case __USBHOST_QUEUETRANSFER: {
            // Variables
			UsbTransferResult_t ResPackage;

			// Create and setup new transfer
			Transfer = UsbManagerCreateTransfer(
				(UsbTransfer_t*)Arg2->Data.Buffer,
                Queryee, ResponsePort, Device, Pipe);
                
            // Queue the periodic transfer
            ResPackage.Status = UsbQueueTransferGeneric(Transfer);
			ResPackage.Id = Transfer->Id;
			ResPackage.BytesTransferred = 0;

            // Send back package?
            if (ResPackage.Status != TransferQueued) {
                return PipeSend(Queryee, ResponsePort, 
                    (void*)&ResPackage, sizeof(UsbTransferResult_t));
            }
            else {
                return OsSuccess;
            }
		} break;

		// Periodic Queue
		case __USBHOST_QUEUEPERIODIC: {
			// Variables
			UsbTransferResult_t ResPackage;

			// Create and setup new transfer
			Transfer = UsbManagerCreateTransfer(
				(UsbTransfer_t*)Arg2->Data.Buffer,
				Queryee, ResponsePort, Device, Pipe);

            // Queue the periodic transfer
            ResPackage.Status = UsbQueueTransferGeneric(Transfer);
			ResPackage.Id = Transfer->Id;
			ResPackage.BytesTransferred = 0;

			// Send back package
			return PipeSend(Queryee, ResponsePort, 
				(void*)&ResPackage, sizeof(UsbTransferResult_t));
		} break;

		// Dequeue Transfer
		case __USBHOST_DEQUEUEPERIODIC: {
			// Variables
			UsbManagerTransfer_t *Transfer = NULL;
			UUId_t Id = (UUId_t)Arg1->Data.Value;
			UsbTransferStatus_t Status = TransferInvalid;

			// Lookup transfer by iterating through
			// available transfers
			foreach(tNode, Controller->QueueControl.TransactionList) {
				// Cast data to our type
				UsbManagerTransfer_t *NodeTransfer = 
					(UsbManagerTransfer_t*)tNode->Data;
				if (NodeTransfer->Id == Id) {
					Transfer = NodeTransfer;
					break;
				}
			}

			// Dequeue and send result back
			if (Transfer != NULL) {
				Status = UsbDequeueTransferGeneric(Transfer);
            }

            // Send back package
			return PipeSend(Queryee, ResponsePort, 
				(void*)&Status, sizeof(UsbTransferStatus_t));
		} break;

		// Reset port
		case __USBHOST_RESETPORT: {
			// Call reset procedure, then let it fall through
			// to QueryPort
			OhciPortPrepare(Controller, (int)Pipe);
		};
		// Query port
		case __USBHOST_QUERYPORT: {
			// Variables
			UsbHcPortDescriptor_t Descriptor;

			// Fill port descriptor
			OhciPortGetStatus(Controller, (int)Pipe, &Descriptor);

			// Send descriptor back
			return PipeSend(Queryee, ResponsePort, 
				(void*)&Descriptor, sizeof(UsbHcPortDescriptor_t));
		} break;

        // Reset endpoint toggles
        case __USBHOST_RESETENDPOINT: {
            Result = UsbManagerSetToggle(Device, Pipe, 0);
        } break;

		// Fall-through, error
		default:
			break;
	}

	// Dunno, fall-through case
	// Return status response
	return PipeSend(Queryee, ResponsePort, (void*)&Result, sizeof(OsStatus_t));
}
