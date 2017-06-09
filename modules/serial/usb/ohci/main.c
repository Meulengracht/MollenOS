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

/* OnInterrupt
 * Is called when one of the registered devices
 * produces an interrupt. On successful handled
 * interrupt return OsSuccess, otherwise the interrupt
 * won't be acknowledged */
InterruptStatus_t OnInterrupt(void *InterruptData)
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

	// Disable interrupts
	Controller->Registers->HcInterruptDisable = (reg32_t)OHCI_INTR_MASTER;

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
	return OsSuccess;
}

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
OsStatus_t OnLoad(void)
{
	// Initialize the device manager here
	return UsbManagerInitialize();
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t OnUnload(void)
{
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
	return UsbManagerCreateController(Controller);
}

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
OsStatus_t OnUnregister(MCoreDevice_t *Device)
{
	// Variables
	OhciController_t *Controller = NULL;
	
	// Lookup controller
	Controller = UsbManagerGetController(Device->Id);

	// Sanitize lookup
	if (Controller == NULL) {
		return OsError;
	}

	// Unregister, then destroy
	UsbManagerDestroyController(Controller);

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
	OhciController_t *Controller = NULL;
	UUId_t Device = UUID_INVALID;

	// Instantiate some variables
	Device = (UUId_t)Arg0->Data.Value;
	
	// Lookup controller
	Controller = UsbManagerGetController(Device);

	// Sanitize we have a controller
	if (Controller == NULL) {
		// Null response

		// Return
		return OsError;
	}

	// Unused params
	_CRT_UNUSED(Arg1);
	_CRT_UNUSED(Arg2);

	switch (QueryFunction) {
		// Generic Queue
		case __USBHOST_QUEUETRANSFER: {
			// Create and setup new transfer

		} break;

		// Periodic Queue
		case __USBHOST_QUEUEPERIODIC: {

		} break;

		// Dequeue Transfer
		case __USBHOST_DEQUEUEPERIODIC: {

		} break;

		// Fall-through, error
		default:
			break;
	}

	// Dunno, fall-through case
	// Return null response

	return OsError;
}
