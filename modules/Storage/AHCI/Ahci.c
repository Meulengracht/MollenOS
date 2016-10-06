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
	//mDevice->IrqHandler = AhciInterruptHandler;

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

/* AHCISetup
 * Initializes memory structures, ports and
 * resets the controller so it's ready for use */
void AhciSetup(AhciController_t *Controller)
{

}