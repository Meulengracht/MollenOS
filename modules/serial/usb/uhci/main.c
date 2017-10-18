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
#define __TRACE

/* Includes 
 * - System */
#include <os/driver/timers.h>
#include <os/mollenos.h>
#include <os/condition.h>
#include <os/thread.h>
#include <os/utils.h>

#include "../common/manager.h"
#include "uhci.h"

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
static UUId_t __GlbTimerEvent = UUID_INVALID;

/* FinalizerWake
 * Informs the finalizer-thread that there is new events */
OsStatus_t
FinalizerWake(void)
{
    TRACE("FinalizerWake()");
    if (__GlbFinalizerEvent != NULL) {
        return ConditionSignal(__GlbFinalizerEvent);
    }
    return OsSuccess;
}

/* FinalizerEntry 
 * Entry of the finalizer thread, this thread handles
 * all completed transactions to notify users */
int
FinalizerEntry(
    _In_Opt_ void *Argument)
{
	// Variables
	ListNode_t *cNode = NULL;
	Mutex_t EventLock;

	// Unused
    _CRT_UNUSED(Argument);
    
    // Debug
    TRACE("FinalizerEntry()");

	// Create the mutex
	MutexConstruct(&EventLock, MUTEX_PLAIN);

	// Forever-loop
	while (1) {
		// Wait for events
		ConditionWait(__GlbFinalizerEvent, &EventLock);

		// Iterate through all transactions for all controllers
		_foreach(cNode, UsbManagerGetControllers()) {
			// Instantiate a controller pointer
			UhciController_t *Controller = 
				(UhciController_t*)cNode->Data;
			
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
					UhciTransactionFinalize(Controller, Transfer, 1);
				
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

/* OnInterrupt
 * This driver uses fast-interrupt */
InterruptStatus_t
OnInterrupt(
    _In_Opt_ void *InterruptData,
    _In_Opt_ size_t Arg0,
    _In_Opt_ size_t Arg1,
    _In_Opt_ size_t Arg2)
{
    // Unused
    _CRT_UNUSED(InterruptData);
    _CRT_UNUSED(Arg0);
    _CRT_UNUSED(Arg1);
    _CRT_UNUSED(Arg2);
	return InterruptHandled;
}

/* OnFastInterrupt
 * Is called when one of the registered devices
 * produces an interrupt. On successful handled
 * interrupt return OsSuccess, otherwise the interrupt
 * won't be acknowledged */
InterruptStatus_t
OnFastInterrupt(
    _In_Opt_ void *InterruptData)
{
	// Variables
	UhciController_t *Controller = NULL;
	uint16_t InterruptStatus;

	// Instantiate the pointer
	Controller = (UhciController_t*)InterruptData;

	// Read interrupt status from i/o
    InterruptStatus = UhciRead16(Controller, UHCI_REGISTER_STATUS);
    
	// Trace
	TRACE("UHCI Interrupt - Status 0x%x", InterruptStatus);
	
	// Was the interrupt even from this controller?
	if (!(InterruptStatus & 0x1F)) {
		return InterruptNotHandled;
	}

	// Clear interrupt bits
	UhciWrite16(Controller, UHCI_REGISTER_STATUS, InterruptStatus);

	// If either interrupt or error is present, it means a change happened
	// in one of our transactions
	if (InterruptStatus & (UHCI_STATUS_USBINT | UHCI_STATUS_INTR_ERROR)) {
		UhciProcessTransfers(Controller);
	}

	// The controller is telling us to perform resume
	if (InterruptStatus & UHCI_STATUS_RESUME_DETECT) {
		UhciStart(Controller, 0);
	}

	// If an host error occurs we should restart controller
	if (InterruptStatus & UHCI_STATUS_HOST_SYSERR) {
		UhciReset(Controller);
		UhciStart(Controller, 0);
	}

	// Processing error, queue stopped
	if (InterruptStatus & UHCI_STATUS_PROCESS_ERR) {
		ERROR("UHCI: Processing Error :/");
        // Clear queue and all waiting
        // @todo
		//UhciReset(Controller);
		//UhciStart(Controller, 0);
	}

	// Done
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
    // Unused variables
    _CRT_UNUSED(Timer);
    _CRT_UNUSED(Data);

    // Do a port-check and optionally make finalizer
    // thread perform a data-check to make sure we don't miss anything
    foreach(cNode, UsbManagerGetControllers()) {
        UhciPortsCheck((UhciController_t*)cNode->Data);
    }
	return OsSuccess;
}

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
OsStatus_t
OnLoad(void)
{
    // Variables
    OsStatus_t Result = OsError;

    // Debug
    TRACE("OnLoad()");

	// Create event semaphore
	__GlbFinalizerEvent = ConditionCreate();

	// Start finalizer thread
    __GlbFinalizerThreadId = ThreadCreate(FinalizerEntry, NULL);

	// Initialize the device manager here
    Result = UsbManagerInitialize();
    
    // Turn on timeouts
    __GlbTimerEvent = TimerStart(1000, 1, NULL);
    return Result;
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t
OnUnload(void)
{
    // Stop timer
    if (__GlbTimerEvent != UUID_INVALID) {
        TimerStop(__GlbTimerEvent);
    }

    // Stop thread
    if (__GlbFinalizerThreadId != UUID_INVALID) {
        ThreadKill(__GlbFinalizerThreadId);
    }

    // Cleanup semaphore
    if (__GlbFinalizerEvent != NULL) {
        ConditionDestroy(__GlbFinalizerEvent);
    }

	// Cleanup the internal device manager
	return UsbManagerDestroy();
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
OsStatus_t
OnRegister(
    _In_ MCoreDevice_t *Device)
{
	// Variables
	UhciController_t *Controller = NULL;
    
    // Debug
    TRACE("OnRegister()");
	
	// Register the new controller
	Controller = UhciControllerCreate(Device);

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
	UhciController_t *Controller = NULL;
	
	// Lookup controller
	Controller = (UhciController_t*)UsbManagerGetController(Device->Id);

	// Sanitize lookup
	if (Controller == NULL) {
		return OsError;
	}

	// Unregister, then destroy
	UsbManagerDestroyController(&Controller->Base);

	// Destroy it
	return UhciControllerDestroy(Controller);
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
	UhciController_t *Controller = NULL;
	UUId_t Device = UUID_INVALID, Pipe = UUID_INVALID;
    OsStatus_t Result = OsError;
    
    // Debug
    TRACE("OnQuery(Function %i)", QueryFunction);

	// Instantiate some variables
	Device = (UUId_t)Arg0->Data.Value;
	Pipe = (UUId_t)Arg1->Data.Value;
	
	// Lookup controller
	Controller = (UhciController_t*)UsbManagerGetController(Device);

	// Sanitize we have a controller
	if (Controller == NULL) {
		// Null response
		return PipeSend(Queryee, ResponsePort, 
			(void*)&Result, sizeof(OsStatus_t));
	}

	switch (QueryFunction) {
		// Generic Queue
		case __USBHOST_QUEUETRANSFER: {
			// Create and setup new transfer
			Transfer = UsbManagerCreateTransfer(
				(UsbTransfer_t*)Arg2->Data.Buffer,
				Queryee, ResponsePort, Device, Pipe);

			// Queue the generic transfer
			return UsbQueueTransferGeneric(Transfer);
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
			Result = UsbQueueTransferGeneric(Transfer);

			// Get id
			ResPackage.Id = Transfer->Id;
			ResPackage.BytesTransferred = 0;
			if (Result == OsSuccess) {
				ResPackage.Status = TransferNotProcessed;
			}
			else {
				ResPackage.Status = TransferInvalidData;
			}

			// Send back package
			return PipeSend(Queryee, ResponsePort, 
				(void*)&ResPackage, sizeof(UsbTransferResult_t));
		} break;

		// Dequeue Transfer
		case __USBHOST_DEQUEUEPERIODIC: {
			
			// Extract transfer-id
			UsbManagerTransfer_t *Transfer = NULL;
			UUId_t Id = (UUId_t)Arg1->Data.Value;

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
				Result = UsbDequeueTransferGeneric(Transfer);
			}
		} break;

		// Reset port
		case __USBHOST_RESETPORT: {
			// Call reset procedure, then let it fall through
			// to QueryPort
			UhciPortPrepare(Controller, (int)Pipe);
        };
        
		// Query port
		case __USBHOST_QUERYPORT: {
			// Variables
			UsbHcPortDescriptor_t Descriptor;

			// Fill port descriptor
			UhciPortGetStatus(Controller, (int)Pipe, &Descriptor);

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
