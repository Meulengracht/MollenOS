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
 * MollenOS MCore - Advanced Host Controller Interface Driver
 * TODO:
 *	- Port Multiplier Support
 *	- Power Management
 */

/* Includes
 * - System */
#include <os/mollenos.h>
#include <os/thread.h>
#include <os/utils.h>
#include "ahci.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <stdlib.h>

/* Prototypes */
OsStatus_t
AhciSetup(
	_In_ AhciController_t *Controller);

/* AhciControllerCreate
 * Registers a new controller with the AHCI driver */
AhciController_t*
AhciControllerCreate(
	_In_ MCoreDevice_t *Device)
{
	// Variables
	AhciController_t *Controller = NULL;
	DeviceIoSpace_t *IoBase = NULL;
	MCoreInterrupt_t Interrupt;
	int i;

	// Allocate a new instance of the controller
	Controller = (AhciController_t*)kmalloc(sizeof(AhciController_t));
	memset(Controller, 0, sizeof(AhciController_t));

	// Fill in some basic stuff needed for init
	Controller->Contract.DeviceId = Device->Id;
	SpinlockReset(&Controller->Lock);

	// Get I/O Base, and for AHCI there might be between 1-5
	// IO-spaces filled, so we always, ALWAYS go for the last one
	for (i = __DEVICEMANAGER_MAX_IOSPACES - 1; i >= 0; i--) {
		if (Device->IoSpaces[i].Size != 0
			&& Device->IoSpaces[i].Type == IO_SPACE_MMIO) {
			IoBase = &Device->IoSpaces[i];
			break;
		}
	}

	// Sanitize that we found the io-space
	if (IoBase == NULL) {
		MollenOSSystemLog("No memory space found for ahci-controller");
		free(Controller);
		return NULL;
	}

	// Acquire the io-space
	if (AcquireIoSpace(IoBase) != OsNoError) {
		MollenOSSystemLog("Failed to acquire the io-space for ahci-controller");
		free(Controller);
		return NULL;
	}
	else {
		// Store information
		memcpy(&Controller->IoBase, IoBase, sizeof(DeviceIoSpace_t));
	}

	// Start out by initializing the contract
	InitializeContract(&Controller->Contract, Controller->Contract.DeviceId, 1,
		ContractController, "AHCI Controller Interface");

	// Instantiate the register-access
	Controller->Registers = 
		(AHCIGenericRegisters_t*)IoBase->VirtualBase;

	// Initialize the interrupt settings
	InitializeInterrupt(&Interrupt, Device);
	Interrupt.Data = Controller;

	// Register contract before interrupt
	if (RegisterContract(&Controller->Contract) != OsNoError) {
		MollenOSSystemLog("Failed to register contract for ahci-controller");
		ReleaseIoSpace(&Controller->IoBase);
		free(Controller);
		return NULL;
	}

	// Register interrupt
	Controller->Interrupt = 
		RegisterInterruptSource(&Interrupt, 0);

	// Now that all formalities has been taken care
	// off we can actually setup controller
	return AhciSetup(Controller);
}

/* AhciControllerDestroy
 * Destroys an existing controller instance and cleans up
 * any resources related to it */
OsStatus_t
AhciControllerDestroy(
	_In_ AhciController_t *Controller)
{
	// Variables
	int i;

	// First step is to clear out all ports
	// this releases devices and resources
	for (i = 0; i < AHCI_MAX_PORTS; i++) {
		if (Controller->Ports[i] != NULL) {
			AhciPortCleanup(Controller, Controller->Ports[i]);
		}
	}

	// Free the controller resources
	if (Controller->CmdListBase != NULL) {
		free(Controller->CmdListBase);
	}
	if (Controller->FisBase != NULL) {
		free(Controller->FisBase);
	}
	if (Controller->CmdTableBase != NULL) {
		free(Controller->CmdTableBase);
	}

	// Unregister the interrupt
	UnregisterInterruptSource(Controller->Interrupt);

	// Release the io-space
	ReleaseIoSpace(&Controller->IoBase);

	// Free the controller structure
	free(Controller);
}

/* AHCIReset 
 * Resets the entire HBA Controller and all ports */
OsStatus_t
AhciReset(
	_In_ AhciController_t *Controller)
{
	// Variables
	int Hung = 0;
	int i;

	// Software may reset the entire HBA by setting GHC.HR to ‘1’.
	Controller->Registers->GlobalHostControl |= AHCI_HOSTCONTROL_HR;
	MemoryBarrier();

	// The bit shall be cleared to ‘0’ by the HBA when the reset is complete. 
	// If the HBA has not cleared GHC.HR to ‘0’ within 1 second of 
	// software setting GHC.HR to ‘1’, the HBA is in a hung or locked state.
	WaitForConditionWithFault(Hung, 
		((Controller->Registers->GlobalHostControl & AHCI_HOSTCONTROL_HR) == 0), 10, 200);

	// Sanity 
	// Did the reset succeed?
	if (Hung) {
		return OsError;
	}

	// If the HBA supports staggered spin-up, the PxCMD.SUD bit will be reset to ‘0’; 
	// software is responsible for setting the PxCMD.SUD and PxSCTL.DET fields 
	// appropriately such that communication can be established on the Serial ATA link. 
	// If the HBA does not support staggered spin-up, the HBA reset shall cause 
	// a COMRESET to be sent on the port.

	// Indicate that system software is AHCI aware
	// by setting GHC.AE to ‘1’.
	Controller->Registers->GlobalHostControl |= AHCI_HOSTCONTROL_AE;

	// Ensure that the controller is not in the running state by reading and
	// examining each implemented port’s PxCMD register
	for (i = 0; i < AHCI_MAX_PORTS; i++) {
		if (!(Controller->ValidPorts & AHCI_IMPLEMENTED_PORT(i))) {
			continue;
		}

		// If PxCMD.ST, PxCMD.CR, PxCMD.FRE and PxCMD.FR
		// are all cleared, the port is in an idle state
		if (!(Controller->Ports[i]->Registers->CommandAndStatus &
			(AHCI_PORT_ST | AHCI_PORT_CR | AHCI_PORT_FRE | AHCI_PORT_FR))) {
			continue;
		}

		// System software places a port into the idle state by clearing PxCMD.ST and
		// waiting for PxCMD.CR to return ‘0’ when read 
		Controller->Ports[i]->Registers->CommandAndStatus = 0;
	}

	// Flush writes, just in case
	MemoryBarrier();

	// Software should wait at least 500 milliseconds for port idle to occur
	ThreadSleep(650);

	// Now we iterate through and see what happened
	for (i = 0; i < AHCI_MAX_PORTS; i++) {
		if (Controller->Ports[i] != NULL) {
			if ((Controller->Ports[i]->Registers->CommandAndStatus
				& (AHCI_PORT_CR | AHCI_PORT_FR))) {
				// Port did not go idle
				// Attempt a port reset and if that fails destroy it
				if (AhciPortReset(Controller, Controller->Ports[i]) != OsNoError) {
					AhciPortCleanup(Controller, Controller->Ports[i]);
				}
			}
		}
	}

	// No errors 
	return OsNoError;
}

/* AHCITakeOwnership
 * Takes control of the HBA from BIOS */
OsStatus_t
AhciTakeOwnership(
	_In_ AhciController_t *Controller)
{
	// Variables
	int Hung = 0;

	// Step 1. Sets the OS Ownership (BOHC.OOS) bit to ’1’.
	Controller->Registers->OSControlAndStatus |= AHCI_CONTROLSTATUS_OOS;
	MemoryBarrier();

	// Wait 25 ms, to determine how long time BIOS needs to release
	ThreadSleep(25);

	// If the BIOS Busy (BOHC.BB) has been set to ‘1’ within 25 milliseconds, 
	// then the OS driver shall provide the BIOS a minimum of two seconds 
	// for finishing outstanding commands on the HBA.
	if (Controller->Registers->OSControlAndStatus & AHCI_CONTROLSTATUS_BB) {
		ThreadSleep(2000);
	}

	// Step 2. Spin on the BIOS Ownership (BOHC.BOS) bit, waiting for it to be cleared to ‘0’.
	WaitForConditionWithFault(Hung, 
		((Controller->Registers->OSControlAndStatus & AHCI_CONTROLSTATUS_BOS) == 0), 10, 25);

	// Sanitize if we got the ownership 
	// Hung is set
	if (Hung) {
		return OsError;
	}
	else {
		return OsNoError;
	}
}

/* AHCISetup
 * Initializes memory structures, ports and
 * resets the controller so it's ready for use */
OsStatus_t
AhciSetup(
	_In_ AhciController_t *Controller)
{
	// Variables
	int FullResetRequired = 0, PortItr = 0;
	int i;

	// Take ownership of the controller
	if (AhciTakeOwnership(Controller) != OsNoError) {
		MollenOSSystemLog("Failed to take ownership of the controller.");
		return OsError;
	}

	// Indicate that system software is AHCI aware 
	// by setting GHC.AE to ‘1’.
	Controller->Registers->GlobalHostControl |= AHCI_HOSTCONTROL_AE;

	// Determine which ports are implemented by the HBA, by reading the PI register. 
	// This bit map value will aid software in determining how many ports are 
	// available and which port registers need to be initialized.
	Controller->ValidPorts = Controller->Registers->PortsImplemented;
	Controller->CmdSlotCount = AHCI_CAPABILITIES_NCS(Controller->Registers->Capabilities);

	// Ensure that the controller is not in the running state by reading and 
	// examining each implemented port’s PxCMD register
	for (i = 0; i < AHCI_MAX_PORTS; i++) {
		if (!(Controller->ValidPorts & AHCI_IMPLEMENTED_PORT(i))) {
			continue;
		}

		// Create a new port
		Controller->Ports[i] = AhciPortCreate(Controller, PortItr, i);

		// If PxCMD.ST, PxCMD.CR, PxCMD.FRE and PxCMD.FR 
		// are all cleared, the port is in an idle state
		if (!(Controller->Ports[i]->Registers->CommandAndStatus &
			(AHCI_PORT_ST | AHCI_PORT_CR | AHCI_PORT_FRE | AHCI_PORT_FR))) {
			continue;
		}

		// System software places a port into the idle state by clearing PxCMD.ST and 
		// waiting for PxCMD.CR to return ‘0’ when read
		Controller->Ports[i]->Registers->CommandAndStatus = 0;

		// Next port
		PortItr++;
	}

	// Flush writes, just in case
	MemoryBarrier();
	MollenOSSystemLog("Ports initializing: %i", PortItr);

	// Software should wait at least 500 milliseconds for port idle to occur
	ThreadSleep(650);

	// Now we iterate through and see what happened
	for (i = 0; i < AHCI_MAX_PORTS; i++) {
		if (Controller->Ports[i] != NULL) {
			if ((Controller->Ports[i]->Registers->CommandAndStatus
				& (AHCI_PORT_CR | AHCI_PORT_FR))) {
				// Port did not go idle 
				// Attempt a port reset
				if (AhciPortReset(Controller, Controller->Ports[i]) != OsNoError) {
					FullResetRequired = 1;
					break;
				}
			}
		}
	}

	// If one of the ports reset fail, and ports  
	// still don't clear properly, we should attempt a full reset
	if (FullResetRequired) {
		MollenOSSystemLog("Full reset of controller is required");
		if (AhciReset(Controller) != OsNoError) {
			MollenOSSystemLog("Failed to reset controller, as a full reset was required.");
			return OsError;
		}
	}

	// Allocate some shared resources, especially 
	// command lists as we need 1K * portcount
	Controller->CmdListBase = malloc(1024 * PortItr);
	Controller->CmdTableBase = malloc((AHCI_COMMAND_TABLE_SIZE * 32) * PortItr);
	
	// We have to take into account FIS based switching here, 
	// if it's supported we need 4K
	if (Controller->Registers->Capabilities & AHCI_CAPABILITIES_FBSS) {
		Controller->FisBase = malloc(0x1000 * PortItr);
		memset(Controller->FisBase, 0, 0x1000 * PortItr);
	}
	else {
		Controller->FisBase = malloc(256 * PortItr);
		memset(Controller->FisBase, 0, 256 * PortItr);
	}
	
	/* Zero out memory allocated */
	memset(Controller->CmdListBase, 0, 1024 * PortItr);
	memset(Controller->CmdTableBase, 0, (AHCI_COMMAND_TABLE_SIZE * 32) * PortItr);

	/* For each implemented port, system software shall allocate memory */
	for (i = 0; i < AHCI_MAX_PORTS; i++) {
		if (Controller->Ports[i] != NULL) {
			AhciPortInit(Controller, Controller->Ports[i]);
		}
	}

	// To enable the HBA to generate interrupts, 
	// system software must also set GHC.IE to a ‘1’
	Controller->Registers->InterruptStatus = 0xFFFFFFFF;
	Controller->Registers->GlobalHostControl |= AHCI_HOSTCONTROL_IE;

	// Debug
	MollenOSSystemLog("Controller is up and running, enabling ports");

	// Enumerate ports and devices
	for (i = 0; i < AHCI_MAX_PORTS; i++) {
		if (Controller->Ports[i] != NULL) {
			AhciPortSetupDevice(Controller, Controller->Ports[i]);
		}
	}

	// Done - no errors!
	return OsNoError;
}
