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
 * Todo:
 * Power Management
 * Finish the FSBR implementation, right now there is no guarantee of order ls/fs/bul
 * The isochronous unlink/link needs improvements, it does not support multiple isocs in same frame 
 */
//#define __TRACE

/* Includes
 * - System */
#include <os/thread.h>
#include <os/utils.h>
#include "uhci.h"

/* Includes
 * - Library */
#include <stdlib.h>
#include <string.h>

/* Prototypes 
 * This is to keep the create/destroy at the top of the source file */
OsStatus_t
UhciSetup(
	_In_ UhciController_t *Controller);

/* Externs
 * We need access to the interrupt-handler in main.c */
__EXTERN
InterruptStatus_t
OnFastInterrupt(
    _In_Opt_ void *InterruptData);

/* UhciRead16
 * Reads a 2-byte value from the control-space of the controller */
uint16_t
UhciRead16(
	_In_ UhciController_t *Controller, 
	_In_ uint16_t Register)
{
	// Wrapper for reading the io-space
	return (uint16_t)ReadIoSpace(Controller->Base.IoBase, Register, 2);
}

/* UhciRead32
 * Reads a 4-byte value from the control-space of the controller */
uint32_t
UhciRead32(
	_In_ UhciController_t *Controller, 
	_In_ uint16_t Register)
{
	// Wrapper for reading the io-space
	return (uint32_t)ReadIoSpace(Controller->Base.IoBase, Register, 4);
}

/* UhciWrite8
 * Writes a single byte value to the control-space of the controller */
void
UhciWrite8(
	_In_ UhciController_t *Controller, 
	_In_ uint16_t Register, 
	_In_ uint8_t Value)
{
	// Wrapper for writing to the io-space
	WriteIoSpace(Controller->Base.IoBase, Register, Value, 1);
}

/* UhciWrite16
 * Writes a 2-byte value to the control-space of the controller */
void
UhciWrite16(
	_In_ UhciController_t *Controller, 
	_In_ uint16_t Register, 
	_In_ uint16_t Value)
{ 
	// Wrapper for writing to the io-space
	WriteIoSpace(Controller->Base.IoBase, Register, Value, 2);
}

/* UhciWrite32
 * Writes a 4-byte value to the control-space of the controller */
void 
UhciWrite32(
	_In_ UhciController_t *Controller, 
	_In_ uint16_t Register, 
	_In_ uint32_t Value)
{
	// Wrapper for writing to the io-space
	WriteIoSpace(Controller->Base.IoBase, Register, Value, 4);
}

/* UhciControllerCreate 
 * Initializes and creates a new Uhci Controller instance
 * from a given new system device on the bus. */
UhciController_t*
UhciControllerCreate(
	_In_ MCoreDevice_t *Device)
{
	// Variables
	UhciController_t *Controller = NULL;
	DeviceIoSpace_t *IoBase = NULL;
	int i;

	// Allocate a new instance of the controller
	Controller = (UhciController_t*)malloc(sizeof(UhciController_t));
	memset(Controller, 0, sizeof(UhciController_t));
	memcpy(&Controller->Base.Device, Device, sizeof(MCoreDevice_t));

	// Fill in some basic stuff needed for init
	Controller->Base.Contract.DeviceId = Controller->Base.Device.Id;
	Controller->Base.Type = UsbUHCI;
	SpinlockReset(&Controller->Base.Lock);

	// Get I/O Base, and for UHCI it'll be the first address we encounter
	// of type IO
	for (i = 0; i < __DEVICEMANAGER_MAX_IOSPACES; i++) {
		if (Controller->Base.Device.IoSpaces[i].Size != 0
			&& Controller->Base.Device.IoSpaces[i].Type == IO_SPACE_IO) {
			IoBase = &Controller->Base.Device.IoSpaces[i];
			break;
		}
	}

	// Sanitize that we found the io-space
	if (IoBase == NULL) {
		ERROR("No memory space found for uhci-controller");
		free(Controller);
		return NULL;
	}

	// Trace
	TRACE("Found Io-Space (Type %u, Physical 0x%x, Size 0x%x)",
		IoBase->Type, IoBase->PhysicalBase, IoBase->Size);

	// Acquire the io-space
	if (CreateIoSpace(IoBase) != OsSuccess
		|| AcquireIoSpace(IoBase) != OsSuccess) {
		ERROR("Failed to create and acquire the io-space for uhci-controller");
		free(Controller);
		return NULL;
	}
	else {
		// Store information
		Controller->Base.IoBase = IoBase;
	}

	// Start out by initializing the contract
	InitializeContract(&Controller->Base.Contract, Controller->Base.Contract.DeviceId, 1,
		ContractController, "UHCI Controller Interface");

	// Initialize the interrupt settings
	Controller->Base.Device.Interrupt.FastHandler = OnFastInterrupt;
	Controller->Base.Device.Interrupt.Data = Controller;

	// Register contract before interrupt
	if (RegisterContract(&Controller->Base.Contract) != OsSuccess) {
		ERROR("Failed to register contract for uhci-controller");
		ReleaseIoSpace(Controller->Base.IoBase);
		DestroyIoSpace(Controller->Base.IoBase->Id);
		free(Controller);
		return NULL;
	}

	// Register interrupt
	Controller->Base.Interrupt =
		RegisterInterruptSource(&Controller->Base.Device.Interrupt, INTERRUPT_FAST);

	// Enable device
	if (IoctlDevice(Controller->Base.Device.Id, __DEVICEMANAGER_IOCTL_BUS,
		(__DEVICEMANAGER_IOCTL_ENABLE | __DEVICEMANAGER_IOCTL_IO_ENABLE
			| __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE)) != OsSuccess) {
		ERROR("Failed to enable the ohci-controller");
		UnregisterInterruptSource(Controller->Base.Interrupt);
		ReleaseIoSpace(Controller->Base.IoBase);
		DestroyIoSpace(Controller->Base.IoBase->Id);
		free(Controller);
		return NULL;
	}

	// Allocate a list of endpoints
	Controller->Base.Endpoints = ListCreate(KeyInteger, LIST_SAFE);

	// Now that all formalities has been taken care
	// off we can actually setup controller
	if (UhciSetup(Controller) == OsSuccess) {
		return Controller;
	}
	else {
		UhciControllerDestroy(Controller);
		return NULL;
	}
}

/* UhciControllerDestroy
 * Destroys an existing controller instance and cleans up
 * any resources related to it */
OsStatus_t
UhciControllerDestroy(
	_In_ UhciController_t *Controller)
{
	// Cleanup scheduler
	UhciQueueDestroy(Controller);

	// Unregister the interrupt
	UnregisterInterruptSource(Controller->Base.Interrupt);

	// Release the io-space
	ReleaseIoSpace(Controller->Base.IoBase);
	DestroyIoSpace(Controller->Base.IoBase->Id);

	// Free the list of endpoints
	ListDestroy(Controller->Base.Endpoints);

	// Free the controller structure
	free(Controller);

	// Cleanup done
	return OsSuccess;
}

/* UhciStart
 * Boots the controller, if it succeeds OsSuccess is returned. */
OsStatus_t
UhciStart(
	_In_ UhciController_t *Controller,
	_In_ int Wait)
{
	// Variables
	uint16_t OldCmd = 0;

	// Read current command register
	// to preserve information, then assert some flags
	OldCmd = UhciRead16(Controller, UHCI_REGISTER_COMMAND);
	OldCmd |= (UHCI_COMMAND_CONFIGFLAG 
		| UHCI_COMMAND_RUN | UHCI_COMMAND_MAXPACKET64);

	// Update
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, OldCmd);

	// Break here?
	if (Wait == 0) {
		return OsSuccess;
	}

	// Wait for controller to start
	OldCmd = 0;
	WaitForConditionWithFault(OldCmd, 
		(UhciRead16(Controller, UHCI_REGISTER_STATUS) & UHCI_STATUS_HALTED) == 0, 100, 10);

	return (OldCmd == 0) ? OsSuccess : OsError;
}

/* UhciStop
 * Stops the controller, if it succeeds OsSuccess is returned. */
OsStatus_t
UhciStop(
	_In_ UhciController_t *Controller)
{
	// Variables
	uint16_t OldCmd = 0;
	
	// Read current command register
	// to preserve information, then deassert run flag
	OldCmd = UhciRead16(Controller, UHCI_REGISTER_COMMAND);
	OldCmd &= ~(UHCI_COMMAND_RUN);

	// Update
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, OldCmd);

	// We don't wait..
	return OsSuccess;
}

/* UhciReset
 * Resets the controller back to usable state, does not restart the controller. */
OsStatus_t
UhciReset(
	_In_ UhciController_t *Controller)
{
	// Variables
	uint16_t Temp = 0;

	// Assert the host-controller reset bit
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, UHCI_COMMAND_HCRESET);

	// Wait for it to stop being asserted
	WaitForConditionWithFault(Temp, 
		(UhciRead16(Controller, UHCI_REGISTER_COMMAND) & UHCI_COMMAND_HCRESET) == 0, 100, 10);

	// Make sure it actually stopped
	if (Temp == 1) {
		ERROR("UHCI: Reset signal is still active..");
	}

	// Clear out command and interrupt register
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, 0x0000);
	UhciWrite16(Controller, UHCI_REGISTER_INTR, 0x0000);

	// Now reconfigure the controller
	UhciWrite8(Controller, UHCI_REGISTER_SOFMOD, 64); // Frame length 1 ms
	UhciWrite32(Controller, UHCI_REGISTER_FRBASEADDR, 
		Controller->QueueControl.FrameListPhysical);
	UhciWrite16(Controller, UHCI_REGISTER_FRNUM, 
		(Controller->QueueControl.Frame & UHCI_FRAME_MASK));

	// Enable the interrupts that are relevant
	UhciWrite16(Controller, UHCI_REGISTER_INTR,
		(UHCI_INTR_TIMEOUT | UHCI_INTR_SHORT_PACKET
		| UHCI_INTR_RESUME | UHCI_INTR_COMPLETION));

	// Done
	return OsSuccess;
}

/* UhciSetup
 * Initializes the controller state and resources */
OsStatus_t
UhciSetup(
	_In_ UhciController_t *Controller)
{
	// Variables
	Flags_t IoctlValue = 0;
	uint16_t Temp = 0, i = 0;

	// Disable interrupts while configuring (and stop controller)
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, 0x0000);
	UhciWrite16(Controller, UHCI_REGISTER_INTR, 0x0000);

	// Perform a global reset, we must wait 100 ms for this complete
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, UHCI_COMMAND_GRESET);
	ThreadSleep(100);
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, 0x0000);

	// Initialize queues
	UhciQueueInitialize(Controller);

	// Reset controller
	UhciReset(Controller);

	// Enumerate all available ports
	for (i = 0; i <= UHCI_MAX_PORTS; i++) {
		Temp = UhciRead16(Controller, (UHCI_REGISTER_PORT_BASE + (i * 2)));

		// Is port valid?
		if (!(Temp & UHCI_PORT_RESERVED)
			|| Temp == 0xFFFF) {
			// This reserved bit must be 1
			// And we must have 2 ports atleast
			break;
		}
	}

	// Store the number of available ports
	Controller->Base.PortCount = i;

	// Enable PCI Interrupts
	IoctlValue = 0x2000;
	if (IoctlDeviceEx(Controller->Base.Device.Id, __DEVICEMANAGER_IOCTL_EXT_WRITE, 
			UHCI_USBLEGEACY, &IoctlValue, 2) != OsSuccess) {
		return OsError;
	}

	// If vendor is Intel we null out the intel register
	if (Controller->Base.Device.VendorId == 0x8086) {
		IoctlValue = 0x00;
		if (IoctlDeviceEx(Controller->Base.Device.Id, __DEVICEMANAGER_IOCTL_EXT_WRITE, 
				UHCI_USBRES_INTEL, &IoctlValue, 1) != OsSuccess) {
			return OsError;
		}
	}

	// Start the controller and return result from that
	return UhciStart(Controller, 1);
}
