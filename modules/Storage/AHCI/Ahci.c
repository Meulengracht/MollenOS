/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
*/

/* Includes */
#include <Module.h>
#include "Ahci.h"

/* Additional Includes */
#include <DeviceManager.h>
#include <Scheduler.h>
#include <Heap.h>
#include <Timers.h>
#include <x86/Pci.h>

/* CLib */
#include <string.h>

/* Globals */
const char *GlbAhciDriverName = "MollenOS AHCI Driver";
int GlbAhciControllerId = 0;

/* Prototypes */
void AhciSetup(AhciController_t *Controller);
int AhciInterruptHandler(void *Args);

/* Entry point of a module */
MODULES_API void ModuleInit(void *Data)
{
	/* Cast & Vars */
	MCoreDevice_t *mDevice = (MCoreDevice_t*)Data;
	AhciController_t *Controller = NULL;
	DeviceIoSpace_t *IoBase = NULL;
	uint16_t PciCommand;

	/* Allocate Resources for this Controller */
	Controller = (AhciController_t*)kmalloc(sizeof(AhciController_t));
	memset(Controller, 0, sizeof(AhciController_t));

	/* Get I/O Base, and for AHCI there might be between 1-5
	 * IO-spaces filled, so we always, ALWAYS go for the last one */
	for (PciCommand = 0; PciCommand < DEVICEMANAGER_MAX_IOSPACES; PciCommand++) {
		if (mDevice->IoSpaces[PciCommand] != NULL
			&& mDevice->IoSpaces[PciCommand]->Type == DEVICE_IO_SPACE_MMIO) {
			IoBase = mDevice->IoSpaces[PciCommand];
		}
	}

	/* Sanity */
	if (IoBase == NULL)
	{
		/* Yea, give me my hat back */
		LogFatal("AHCI", "No memory space found for controller!");
		kfree(Controller);
		return;
	}

	/* Now we initialise */
	Controller->Id = GlbAhciControllerId;
	Controller->Device = mDevice;
	GlbAhciControllerId++;

	/* Get registers */
	Controller->Registers = (volatile AHCIGenericRegisters_t*)IoBase->VirtualBase;

	/* Reset Lock */
	SpinlockReset(&Controller->Lock);

	/* Allocate Irq */
	mDevice->IrqAvailable[0] = -1;
	mDevice->IrqHandler = AhciInterruptHandler;

	/* Register us for an irq */
	if (DmRequestResource(mDevice, ResourceIrq)) {

		/* Damnit! */
		LogFatal("AHCI", "Failed to allocate irq for use, bailing out!");
		kfree(Controller);
		return;
	}

	/* Enable memory io and bus mastering, remove interrupts disabled */
	PciCommand = (uint16_t)PciDeviceRead(mDevice->BusDevice, 0x4, 2);
	PciDeviceWrite(mDevice->BusDevice, 0x4, (PciCommand & ~(0x400)) | 0x2 | 0x4, 2);

	/* Setup driver information */
	mDevice->Driver.Name = (char*)GlbAhciDriverName;
	mDevice->Driver.Data = Controller;
	mDevice->Driver.Version = 1;
	mDevice->Driver.Status = DriverActive;

	/* Setup Controller */
	AhciSetup(Controller);
}

/* AHCIReset 
 * Resets the entire HBA Controller and all ports */
OsStatus_t AhciReset(AhciController_t *Controller)
{
	/* Variables */
	int Hung = 0;
	int i;

	/* Software may reset the entire HBA by setting GHC.HR to ‘1’. */
	Controller->Registers->GlobalHostControl |= AHCI_HOSTCONTROL_HR;
	MemoryBarrier();

	/* The bit shall be cleared to ‘0’ by the HBA when the reset is complete. 
	 * If the HBA has not cleared GHC.HR to ‘0’ within 1 second of 
	 * software setting GHC.HR to ‘1’, the HBA is in a hung or locked state. */
	WaitForConditionWithFault(Hung, 
		((Controller->Registers->GlobalHostControl & AHCI_HOSTCONTROL_HR) == 0), 10, 200);

	/* Sanity 
	 * Did the reset succeed? */
	if (Hung) {
		return OsError;
	}

	/* If the HBA supports staggered spin-up, the PxCMD.SUD bit will be reset to ‘0’; 
	 * software is responsible for setting the PxCMD.SUD and PxSCTL.DET fields 
	 * appropriately such that communication can be established on the Serial ATA link. 
	 * If the HBA does not support staggered spin-up, the HBA reset shall cause 
	 * a COMRESET to be sent on the port. */

	/* Indicate that system software is AHCI aware
	 * by setting GHC.AE to ‘1’. */
	Controller->Registers->GlobalHostControl |= AHCI_HOSTCONTROL_AE;

	/* Ensure that the controller is not in the running state by reading and
	 * examining each implemented port’s PxCMD register */
	for (i = 0; i < AHCI_MAX_PORTS; i++)
	{
		/* Sanitize */
		if (!(Controller->ValidPorts & AHCI_IMPLEMENTED_PORT(i))) {
			continue;
		}

		/* If PxCMD.ST, PxCMD.CR, PxCMD.FRE and PxCMD.FR
		 * are all cleared, the port is in an idle state */
		if (!(Controller->Ports[i]->Registers->CommandAndStatus &
			(AHCI_PORT_ST | AHCI_PORT_CR | AHCI_PORT_FRE | AHCI_PORT_FR))) {
			continue;
		}

		/* System software places a port into the idle state by clearing PxCMD.ST and
		 * waiting for PxCMD.CR to return ‘0’ when read */
		Controller->Ports[i]->Registers->CommandAndStatus = 0;
	}

	/* Flush writes, just in case */
	MemoryBarrier();

	/* Software should wait at least 500 milliseconds for port idle to occur */
	SleepMs(650);

	/* Now we iterate through and see what happened */
	for (i = 0; i < AHCI_MAX_PORTS; i++) {
		if (Controller->Ports[i] != NULL) {
			if ((Controller->Ports[i]->Registers->CommandAndStatus
				& (AHCI_PORT_CR | AHCI_PORT_FR))) {
				/* Port did not go idle
				 * Attempt a port reset */
				if (AhciPortReset(Controller, Controller->Ports[i]) != OsNoError) {
					/* Destroy the port */
					AhciPortCleanup(Controller, Controller->Ports[i]);
				}
			}
		}
	}

	/* Done! */
	return OsNoError;
}

/* AHCIDestroy
 * Cleans up controller, ports and memory structures allocated */
void AhciDestroy(AhciController_t *Controller)
{
	/* Variables */
	int i;

	/* Cleanup all ports */
	for (i = 0; i < AHCI_MAX_PORTS; i++) {
		if (Controller->Ports[i] != NULL) {
			AhciPortCleanup(Controller, Controller->Ports[i]);
		}
	}

	/* Free the shared resources */
	if (Controller->CmdListBase != NULL)
		kfree(Controller->CmdListBase);
	if (Controller->FisBase != NULL)
		kfree(Controller->FisBase);
	if (Controller->CmdTableBase != NULL)
		kfree(Controller->CmdTableBase);
	
	/* Free controller */
	kfree(Controller);
}

/* AHCITakeOwnership
 * Takes control of the HBA from BIOS */
OsStatus_t AhciTakeOwnership(AhciController_t *Controller)
{
	/* Variables */
	int Hung = 0;

	/* Step 1. Sets the OS Ownership (BOHC.OOS) bit to ’1’. */
	Controller->Registers->OSControlAndStatus |= AHCI_CONTROLSTATUS_OOS;
	MemoryBarrier();

	/* Wait 25 ms, to determine how long time BIOS needs to release */
	SleepMs(25);

	/* If the BIOS Busy (BOHC.BB) has been set to ‘1’ within 25 milliseconds, 
	 * then the OS driver shall provide the BIOS a minimum of two seconds 
	 * for finishing outstanding commands on the HBA. */
	if (Controller->Registers->OSControlAndStatus & AHCI_CONTROLSTATUS_BB) {
		SleepMs(2000);
	}

	/* Step 2. Spin on the BIOS Ownership (BOHC.BOS) bit, waiting for it to be cleared to ‘0’. */
	WaitForConditionWithFault(Hung, 
		((Controller->Registers->OSControlAndStatus & AHCI_CONTROLSTATUS_BOS) == 0), 10, 25);

	/* Sanitize if we got the ownership 
	 * Hung is set */
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
void AhciSetup(AhciController_t *Controller)
{
	/* Variables */
	int FullResetRequired = 0, PortItr = 0;
	int i;

	/* Declare ownership */
	if (AhciTakeOwnership(Controller) != OsNoError) {
		LogFatal("AHCI", "Failed to take ownership of the controller.");
		AhciDestroy(Controller);
		return;
	}

	/* Indicate that system software is AHCI aware 
	 * by setting GHC.AE to ‘1’. */
	Controller->Registers->GlobalHostControl |= AHCI_HOSTCONTROL_AE;

	/* Determine which ports are implemented by the HBA, by reading the PI register. 
	 * This bit map value will aid software in determining how many ports are 
	 * available and which port registers need to be initialized. */
	Controller->ValidPorts = Controller->Registers->PortsImplemented;
	Controller->CmdSlotCount = AHCI_CAPABILITIES_NCS(Controller->Registers->Capabilities);

	/* Ensure that the controller is not in the running state by reading and 
	 * examining each implemented port’s PxCMD register */
	for (i = 0; i < AHCI_MAX_PORTS; i++)
	{
		/* Sanitize */
		if (!(Controller->ValidPorts & AHCI_IMPLEMENTED_PORT(i))) {
			continue;
		}

		/* Create a new port */
		Controller->Ports[i] = AhciPortCreate(Controller, PortItr, i);

		/* If PxCMD.ST, PxCMD.CR, PxCMD.FRE and PxCMD.FR 
		 * are all cleared, the port is in an idle state */
		if (!(Controller->Ports[i]->Registers->CommandAndStatus &
			(AHCI_PORT_ST | AHCI_PORT_CR | AHCI_PORT_FRE | AHCI_PORT_FR))) {
			continue;
		}

		/* System software places a port into the idle state by clearing PxCMD.ST and 
		 * waiting for PxCMD.CR to return ‘0’ when read */
		Controller->Ports[i]->Registers->CommandAndStatus = 0;

		/* Increament port counter */
		PortItr++;
	}

	/* Flush writes, just in case */
	MemoryBarrier();

	/* Log */
	LogInformation("AHCI", "Ports initializing: %i", PortItr);

	/* Software should wait at least 500 milliseconds for port idle to occur */
	SleepMs(650);

	/* Now we iterate through and see what happened */
	for (i = 0; i < AHCI_MAX_PORTS; i++) {
		if (Controller->Ports[i] != NULL) {
			if ((Controller->Ports[i]->Registers->CommandAndStatus
				& (AHCI_PORT_CR | AHCI_PORT_FR))) {
				/* Port did not go idle 
				 * Attempt a port reset */
				if (AhciPortReset(Controller, Controller->Ports[i]) != OsNoError) {
					FullResetRequired = 1;
					break;
				}
			}
		}
	}

	/* If one of the ports reset fail, and ports  
	 * still don't clear properly, we should attempt a full reset */
	if (FullResetRequired) {
		LogDebug("AHCI", "Full reset of controller is required");
		if (AhciReset(Controller) != OsNoError) {
			LogFatal("AHCI", "Failed to reset controller, as a full reset was required.");
			AhciDestroy(Controller);
			return;
		}
	}

	/* Allocate some shared resources, especially 
	 * command lists as we need 1K * portcount */
	Controller->CmdListBase = kmalloc_a(1024 * PortItr);
	Controller->FisBase = kmalloc_a(256 * PortItr);
	Controller->CmdTableBase = kmalloc_a(((AHCI_COMMAND_TABLE_SIZE * Controller->CmdSlotCount) * 32) * PortItr);

	/* Zero out memory allocated */
	memset(Controller->CmdListBase, 0, 1024 * PortItr);
	memset(Controller->FisBase, 0, 256 * PortItr);
	memset(Controller->CmdTableBase, 0, ((AHCI_COMMAND_TABLE_SIZE * Controller->CmdSlotCount) * 32) * PortItr);

	/* For each implemented port, system software shall allocate memory */
	for (i = 0; i < AHCI_MAX_PORTS; i++) {
		if (Controller->Ports[i] != NULL) {
			AhciPortInit(Controller, Controller->Ports[i]);
		}
	}

	/* To enable the HBA to generate interrupts, system software must also set GHC.IE to a ‘1’ */
	Controller->Registers->InterruptStatus = 0xFFFFFFFF;
	Controller->Registers->GlobalHostControl |= AHCI_HOSTCONTROL_IE;

	/* Debug */
	LogInformation("AHCI", "Controller is up and running, enabling ports");

	/* Enumerate ports and devices */
	for (i = 0; i < AHCI_MAX_PORTS; i++) {
		if (Controller->Ports[i] != NULL) {
			AhciPortSetupDevice(Controller, Controller->Ports[i]);
		}
	}
}

/* Controller Interrupt Handler 
 * Does basic interrupt handling */
int AhciInterruptHandler(void *Args)
{
	/* Variables */
	MCoreDevice_t *mDevice = (MCoreDevice_t*)Args;
	AhciController_t *Controller = (AhciController_t*)mDevice->Driver.Data;
	int i;
	
	/* Store status locally */
	reg32_t InterruptStatus = Controller->Registers->InterruptStatus;

	/* Was this interrupt even from this controller?? */
	if (!InterruptStatus) {
		return X86_IRQ_NOT_HANDLED;
	}

	/* Iterate port interrupt status */
	for (i = 0; i < 32; i++) {
		if (Controller->Ports[i] != NULL
			&& ((InterruptStatus & (1 << i)) != 0)) {
			AhciPortInterruptHandler(Controller, Controller->Ports[i]);
		}
	}

	/* Clear out interrupts */
	Controller->Registers->InterruptStatus = InterruptStatus;

	/* Done! */
	return X86_IRQ_HANDLED;
}
