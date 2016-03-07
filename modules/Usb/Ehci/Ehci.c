/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS USB EHCI Controller Driver
*/

/* Definitions */
//#define EHCI_DISABLE

/* Includes */
#include <Module.h>
#include "Ehci.h"

/* Additional Includes */
#include <DeviceManager.h>
#include <UsbCore.h>
#include <Scheduler.h>
#include <Heap.h>
#include <Timers.h>
#include <Pci.h>

/* CLib */
#include <string.h>

/* Globals */
int GlbEhciControllerId = 0;

/* Prototypes */
void EhciSetup(EhciController_t *Controller);

/* Entry point of a module */
MODULES_API void ModuleInit(void *Data)
{
	/* Cast & Vars */
	MCoreDevice_t *mDevice = (MCoreDevice_t*)Data;
	EhciController_t *Controller = NULL;
	DeviceIoSpace_t *IoBase = NULL;
	uint16_t PciCommand;

	/* Allocate Resources for this Controller */
	Controller = (EhciController_t*)kmalloc(sizeof(EhciController_t));
	memset(Controller, 0, sizeof(EhciController_t));

	Controller->Device = mDevice;
	Controller->Id = GlbEhciControllerId;

	/* Get I/O Base */
	for (PciCommand = 0; PciCommand < DEVICEMANAGER_MAX_IOSPACES; PciCommand++) {
		if (mDevice->IoSpaces[PciCommand] != NULL
			&& mDevice->IoSpaces[PciCommand]->Type == DEVICE_IO_SPACE_MMIO) {
			IoBase = mDevice->IoSpaces[PciCommand];
			break;
		}
	}

	/* Sanity */
	if (IoBase == NULL)
	{
		/* Yea, give me my hat back */
		LogFatal("EHCI", "No memory space found for controller!");
		kfree(Controller);
		return;
	}

	/* Now we initialise */
	GlbEhciControllerId++;

	/* Get registers */
	Controller->CapRegisters = (EchiCapabilityRegisters_t*)IoBase->VirtualBase;
	Controller->OpRegisters = (EchiOperationalRegisters_t*)
		(IoBase->VirtualBase + Controller->CapRegisters->Length);

	/* Reset Lock */
	SpinlockReset(&Controller->Lock);

	/* Allocate Irq */
	mDevice->IrqAvailable[0] = -1;
	mDevice->IrqHandler = NULL;

	/* Register us for an irq */
	//if (DmRequestResource(mDevice, ResourceIrq)) {

		/* Damnit! */
	//	LogFatal("OHCI", "Failed to allocate irq for use, bailing out!");
	//	kfree(Controller);
	//	return;
	//}

	/* Enable memory io and bus mastering, remove interrupts disabled */
	uint16_t PciCommand = (uint16_t)PciDeviceRead(mDevice->BusDevice, 0x4, 2);
	PciDeviceWrite(mDevice->BusDevice, 0x4, (PciCommand & ~(0x400)) | 0x2 | 0x4, 2);

	/* Setup Controller */
	EhciSetup(Controller);
}

/* Disable Legecy Support */
void EhciDisableLegacySupport(EhciController_t *Controller)
{
	/* Vars */
	uint32_t Eecp;

	/* Pci Registers
	* BAR0 - Usb Base Registers
	* 0x60 - Revision
	* 0x61 - Frame Length Adjustment
	* 0x62/3 - Port Wake capabilities
	* ????? - Usb Legacy Support Extended Capability Register
	* ???? + 4 - Usb Legacy Support Control And Status Register
	* The above means ???? = EECP. EECP Offset in PCI space where
	* we can find the above registers */
	Eecp = EHCI_CPARAM_EECP(Controller->CapRegisters->CParams);

	/* Two cases, if EECP is valid we do additional steps */
	if (Eecp >= 0x40)
	{
		volatile uint8_t Semaphore = 0;
		volatile uint8_t CapId = 0;
		uint8_t Failed = 0;
		volatile uint8_t NextEecp = 0;

		/* Get the extended capability register
		* We read the second byte, because it contains
		* the BIOS Semaphore */
		Failed = 0;
		while (1)
		{
			/* Get Id */
			CapId = (uint8_t)PciDeviceRead(Controller->Device->BusDevice, Eecp, 1);

			/* Legacy Support? */
			if (CapId == 0x01)
				break;

			/* No, get next Eecp */
			NextEecp = (uint8_t)PciDeviceRead(Controller->Device->BusDevice, Eecp + 0x1, 1);

			/* Sanity */
			if (NextEecp == 0x00)
				break;
			else
				Eecp = NextEecp;
		}

		/* Only continue if Id == 0x01 */
		if (CapId == 0x01)
		{
			Semaphore = (uint8_t)PciDeviceRead(Controller->Device->BusDevice, Eecp + 0x2, 1);

			/* Is it BIOS owned? First bit in second byte */
			if (Semaphore & 0x1)
			{
				/* Request for my hat back :/
				* Third byte contains the OS Semaphore */
				PciDeviceWrite(Controller->Device->BusDevice, Eecp + 0x3, 0x1, 1);

				/* Now we wait for the bios to release semaphore */
				WaitForCondition((PciDeviceRead(Controller->Device->BusDevice, Eecp + 0x2, 1) & 0x1) == 0, 250, 10, "USB_EHCI: Failed to release BIOS Semaphore");
				WaitForCondition((PciDeviceRead(Controller->Device->BusDevice, Eecp + 0x3, 1) & 0x1) == 1, 250, 10, "USB_EHCI: Failed to set OS Semaphore");
			}

			/* Disable SMI by setting all lower 16 bits to 0 of EECP+4 */
			PciDeviceWrite(Controller->Device->BusDevice, Eecp + 0x4, 0x0000, 2);
		}
	}
}

/* Stop Controller */
void EhciHalt(EhciController_t *Controller)
{
	/* Vars */
	uint32_t Temp;
	int Fault = 0;

	/* Stop scheduler */
	Temp = Controller->OpRegisters->UsbCommand;
	Temp &= ~(EHCI_COMMAND_PERIODIC_ENABLE | EHCI_COMMAND_ASYNC_ENABLE);
	Controller->OpRegisters->UsbCommand = Temp;

	/* Wait for stop */
	WaitForConditionWithFault(Fault, (Controller->OpRegisters->UsbStatus & 0xC000) == 0, 250, 10);

	/* Sanity */
	if (Fault) {
		LogFatal("EHCI", "Failed to stop scheduler, Cmd Register: 0x%x - Status: 0x%x",
			Controller->OpRegisters->UsbCommand, Controller->OpRegisters->UsbStatus);
	}

	/* Clear remaining interrupts */
	Controller->OpRegisters->UsbIntr = 0;

	/* Stop controller */
	Temp = Controller->OpRegisters->UsbCommand;
	Temp &= ~(EHCI_COMMAND_RUN);
	Controller->OpRegisters->UsbCommand = Temp;

	/* Wait for stop */
	Fault = 0;
	WaitForConditionWithFault(Fault, (Controller->OpRegisters->UsbStatus & EHCI_STATUS_HALTED) != 0, 250, 10);

	if (Fault) {
		LogFatal("EHCI", "Failed to stop controller, Cmd Register: 0x%x - Status: 0x%x",
			Controller->OpRegisters->UsbCommand, Controller->OpRegisters->UsbStatus);
	}
}

/* Silence Controller */
void EhciSilence(EhciController_t *Controller)
{
	/* Halt Controller */
	EhciHalt(Controller);

	/* Clear Configured Flag */
	Controller->OpRegisters->ConfigFlag = 0;
}

/* Reset Controller */
void EhciReset(EhciController_t *Controller)
{
	/* Vars */
	uint32_t Temp;
	int Fault = 0;

	/* Reset Controller */
	Temp = Controller->OpRegisters->UsbCommand;
	Temp |= EHCI_COMMAND_HCRESET;
	Controller->OpRegisters->UsbCommand = Temp;

	/* Wait for reset signal to deassert */
	WaitForConditionWithFault(Fault, 
		(Controller->OpRegisters->UsbCommand & EHCI_COMMAND_HCRESET) == 0, 250, 10);

	if (Fault) {
		LogDebug("EHCI", "Reset signal won't deassert, waiting one last long wait",
			Controller->OpRegisters->UsbCommand, Controller->OpRegisters->UsbStatus);
		StallMs(250);
	}
}

/* Initialize */

/* Setup EHCI */
void EhciSetup(EhciController_t *Controller)
{
	/* Vars */
	UsbHc_t *HcCtrl;

	/* Disable Legacy Support */
	EhciDisableLegacySupport(Controller);

#ifdef EHCI_DISABLE
	/* Silence Controller */
	EhciSilence(Controller);
#else
	/* Save some read-only information */
	Controller->Ports = EHCI_SPARAM_PORTCOUNT(Controller->CapRegisters->SParams);
	Controller->SParameters = Controller->CapRegisters->SParams;
	Controller->CParameters = Controller->CapRegisters->CParams;

	/* Stop Controller */
	EhciHalt(Controller);

	/* Reset Controller */
	EhciReset(Controller);

	/* Instantiate some registers */
	Controller->OpRegisters->SegmentSelector = 0;

	/* Initialize Periodic Scheduler */
	EhciInitializePeriodicScheduler(Controller);

	/* Initialize Async Scheduler */
	EhciInitializeAsyncScheduler(Controller);
#endif

	/* Now everything is routed to companion controllers */
	HcCtrl = UsbInitController(Controller, EhciController, 2);
}