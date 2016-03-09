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
#define EHCI_DIAGNOSTICS

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
const char *GlbEhciDriverName = "MollenOS EHCI Driver";
int GlbEhciControllerId = 0;

/* Prototypes */
void EhciSetup(EhciController_t *Controller);
void EhciPortSetup(void *cData, UsbHcPort_t *Port);
void EhciPortScan(void *cData);
int EhciInterruptHandler(void *Args);

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
	mDevice->IrqHandler = EhciInterruptHandler;

	/* Register us for an irq */
#ifndef EHCI_DISABLE
	if (DmRequestResource(mDevice, ResourceIrq)) {

		/* Damnit! */
		LogFatal("EHCI", "Failed to allocate irq for use, bailing out!");
		kfree(Controller);
		return;
	}
#endif

	/* Enable memory io and bus mastering, remove interrupts disabled */
	PciCommand = (uint16_t)PciDeviceRead(mDevice->BusDevice, 0x4, 2);
	PciDeviceWrite(mDevice->BusDevice, 0x4, (PciCommand & ~(0x400)) | 0x2 | 0x4, 2);

	/* Setup driver information */
	mDevice->Driver.Name = (char*)GlbEhciDriverName;
	mDevice->Driver.Data = Controller;
	mDevice->Driver.Version = 1;
	mDevice->Driver.Status = DriverActive;

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
	Controller->OpRegisters->UsbStatus = (0x3F);

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

/* Restart Controller */
void EhciRestart(void *cData)
{
	/* Cast */
	EhciController_t *Controller = (EhciController_t*)cData;
	uint32_t Temp;

	/* Start by stopping */
	EhciHalt(Controller);

	/* Unschedule everything */

	/* Reset Controller */
	EhciReset(Controller);

	/* <reconfigure> */
	Controller->OpRegisters->SegmentSelector = 0;
	Controller->OpRegisters->FrameIndex = 0;

	/* Set desired interrupts */
	Controller->OpRegisters->UsbIntr = (EHCI_INTR_PROCESS | EHCI_INTR_PROCESSERROR
		| EHCI_INTR_PORTCHANGE | EHCI_INTR_HOSTERROR | EHCI_INTR_ASYNC_DOORBELL);

	/* Write addresses */
	Controller->OpRegisters->PeriodicListAddr = 
		(uint32_t)Controller->FrameList;
	Controller->OpRegisters->AsyncListAddress = 
		(uint32_t)Controller->QhPool[EHCI_POOL_QH_ASYNC]->PhysicalAddress;

	/* Build Command
	* Irq Latency = 0 */
	Temp = EHCI_COMMAND_INTR_THRESHOLD(0);

	/* Support for PerPort events? */
	if (Controller->CParameters & EHCI_CPARAM_PERPORT_CHANGE)
		Temp |= EHCI_COMMAND_PERPORT_ENABLE;

	/* Support for Park Mode? */
	if (Controller->CParameters & EHCI_CPARAM_ASYNCPARK) {
		Temp |= EHCI_COMMAND_ASYNC_PARKMODE;
		Temp |= EHCI_COMMAND_PARK_COUNT(3);
	}

	/* Frame list support */
	if (Controller->CParameters & EHCI_CPARAM_VARIABLEFRAMELIST)
		Temp |= EHCI_COMMAND_LISTSIZE(EHCI_LISTSIZE_256);

	/* Start */
	Temp |= EHCI_COMMAND_RUN;
	Controller->OpRegisters->UsbCommand = Temp;

	/* Set configured */
	Controller->OpRegisters->ConfigFlag = 1;
}

/* Setup EHCI */
void EhciSetup(EhciController_t *Controller)
{
	/* Vars */
	UsbHc_t *HcCtrl;
	uint32_t Temp;
	size_t i;

	/* Disable Legacy Support */
	EhciDisableLegacySupport(Controller);

#ifdef EHCI_DISABLE
	/* Silence Controller */
	EhciSilence(Controller);

	/* Now everything is routed to companion controllers */
#else
	/* Save some read-only information */
	Controller->Ports = EHCI_SPARAM_PORTCOUNT(Controller->CapRegisters->SParams);
	Controller->SParameters = Controller->CapRegisters->SParams;
	Controller->CParameters = Controller->CapRegisters->CParams;

	/* Stop Controller */
	EhciHalt(Controller);

	/* Reset Controller */
	EhciReset(Controller);

	/* Initialize Queues */
	EhciInitQueues(Controller);

	/* Reset */
	Controller->OpRegisters->SegmentSelector = 0;
	Controller->OpRegisters->FrameIndex = 0;

	/* Set desired interrupts */
	Controller->OpRegisters->UsbIntr = (EHCI_INTR_PROCESS | EHCI_INTR_PROCESSERROR
		| EHCI_INTR_PORTCHANGE | EHCI_INTR_HOSTERROR | EHCI_INTR_ASYNC_DOORBELL);

	/* Build Command 
	 * Irq Latency = 0 */
	Temp = EHCI_COMMAND_INTR_THRESHOLD(0);

	/* Support for PerPort events? */
	if (Controller->CParameters & EHCI_CPARAM_PERPORT_CHANGE)
		Temp |= EHCI_COMMAND_PERPORT_ENABLE;

	/* Support for Park Mode? */
	if (Controller->CParameters & EHCI_CPARAM_ASYNCPARK) {
		Temp |= EHCI_COMMAND_ASYNC_PARKMODE;
		Temp |= EHCI_COMMAND_PARK_COUNT(3);
	}

	/* Frame list support */
	if (Controller->CParameters & EHCI_CPARAM_VARIABLEFRAMELIST)
		Temp |= EHCI_COMMAND_LISTSIZE(EHCI_LISTSIZE_256);

	/* Start */
	Temp |= EHCI_COMMAND_RUN;
	Controller->OpRegisters->UsbCommand = Temp;

	/* Set configured */
	Controller->OpRegisters->ConfigFlag = 1;
#endif

	/* Initalize Controller (HCD) */
	HcCtrl = UsbInitController(Controller, EhciController, 2);

	/* Port Functions */
	HcCtrl->PortSetup = EhciPortSetup;
	HcCtrl->RootHubCheck = EhciPortScan;

	/* Reset/Resume */
	HcCtrl->Reset = EhciRestart;

	/* Endpoint Functions */


	/* Transaction Functions */


	/* Register Controller */
	Controller->HcdId = UsbRegisterController(HcCtrl);

	/* Setup Ports */
#ifndef EHCI_DISABLE
	/* Now, controller is up and running
	* and we should start doing port setups */

	/* Iterate ports */
	for (i = 0; i < Controller->Ports; i++) {

		/* Is port connected? */
		if (Controller->OpRegisters->Ports[i] & EHCI_PORT_CONNECTED) 
		{
			/* Is the port destined for other controllers?
			 * Port must be in K-State */
			if (EHCI_PORT_LINESTATUS(Controller->OpRegisters->Ports[i])
				== EHCI_LINESTATUS_RELEASE) {
				/* This is a low-speed device */
				if (EHCI_SPARAM_CCCOUNT(Controller->SParameters) != 0)
					Controller->OpRegisters->Ports[i] |= EHCI_PORT_COMPANION_HC;
			}
			else
				UsbEventCreate(HcCtrl, (int)i, HcdConnectedEvent);
		}
	}
#endif
}

/* Port Logic */
int EhciPortReset(EhciController_t *Controller, size_t Port)
{
	/* Vars */
	uint32_t Temp = 0;

	/* Enable port power */
	if (Controller->SParameters & EHCI_SPARAM_PPC) {
		Controller->OpRegisters->Ports[Port] |= EHCI_PORT_POWER;

		/* Wait for power */
		StallMs(5);
	}

	/* We must set the port-reset and keep the signal asserted for atleast 50 ms
	 * now, we are going to keep the signal alive for (much) longer due to 
	 * some devices being slow af */
	Temp = Controller->OpRegisters->Ports[Port];

	/* The EHCI documentation says we should 
	 * disable enabled and assert reset together */
	Temp &= ~(EHCI_PORT_ENABLED);
	Temp |= EHCI_PORT_RESET;

	/* Write */
	Controller->OpRegisters->Ports[Port] = Temp;

	/* Wait */
	StallMs(100);

	/* Deassert signal */
	Temp = Controller->OpRegisters->Ports[Port];
	Temp &= ~(EHCI_PORT_RESET);
	Controller->OpRegisters->Ports[Port] = Temp;

	/* Wait for deassertion: 
	 * The reset process is actually complete when software reads a zero in the PortReset bit */
	Temp = 0;
	WaitForConditionWithFault(Temp, (Controller->OpRegisters->Ports[Port] & EHCI_PORT_RESET) == 0, 250, 10);

	/* Recovery */
	StallMs(30);

	/* Clear RWC */
	Controller->OpRegisters->Ports[Port] |= (EHCI_PORT_CONNECT_EVENT | EHCI_PORT_ENABLE_EVENT | EHCI_PORT_OC_EVENT);

	/* Now, if the port has a high-speed 
	 * device, the enabled port is set */
	if (!(Controller->OpRegisters->Ports[Port] & EHCI_PORT_ENABLED)) {
		if (EHCI_SPARAM_CCCOUNT(Controller->SParameters) != 0)
			Controller->OpRegisters->Ports[Port] |= EHCI_PORT_COMPANION_HC;
		return -1;
	}

	/* Done! */
	return 0;
}

/* Setup Port */
void EhciPortSetup(void *cData, UsbHcPort_t *Port)
{
	/* Cast */
	EhciController_t *Controller = (EhciController_t*)cData;
	int RetVal = 0;

	/* Step 1. Wait for power to stabilize */
	StallMs(100);

#ifdef EHCI_DIAGNOSTICS
	/* Debug */
	LogDebug("EHCI", "Port Pre-Reset: 0x%x", Controller->OpRegisters->Ports[Port->Id]);
#endif

	/* Step 2. Reset Port */
	RetVal = EhciPortReset(Controller, Port->Id);

#ifdef EHCI_DIAGNOSTICS
	/* Debug */
	LogDebug("EHCI", "Port Post-Reset: 0x%x", Controller->OpRegisters->Ports[Port->Id]);
#endif

	/* Evaluate the status of the reset */
	if (RetVal) 
	{
		/* Not for us */
		Port->Connected = 0;
		Port->Enabled = 0;
	}
	else
	{
		/* High Speed */
		uint32_t Status = Controller->OpRegisters->Ports[Port->Id];

		/* Connected? */
		if (Status & EHCI_PORT_CONNECTED)
			Port->Connected = 1;

		/* Enabled? */
		if (Status & EHCI_PORT_ENABLED)
			Port->Enabled = 1;

		/* High Speed */
		Port->Speed = HighSpeed;
	}
}

/* Port Status Check */
void EhciPortCheck(EhciController_t *Controller, size_t Port)
{
	/* Vars */
	uint32_t Status = Controller->OpRegisters->Ports[Port];
	UsbHc_t *HcCtrl;

	/* Connection event? */
	if (!(Status & EHCI_PORT_CONNECT_EVENT))
		return;

	/* Get HCD data */
	HcCtrl = UsbGetHcd(Controller->HcdId);

	/* Sanity */
	if (HcCtrl == NULL)
	{
		LogDebug("EHCI", "Controller %u is zombie and is trying to give events!", Controller->Id);
		return;
	}

	/* Connect or Disconnect? */
	if (Status & EHCI_PORT_CONNECTED)
	{
		/* Ok, something happened
		* but the port might not be for us */
		if (EHCI_PORT_LINESTATUS(Status) == EHCI_LINESTATUS_RELEASE) {
			/* This is a low-speed device */
			if (EHCI_SPARAM_CCCOUNT(Controller->SParameters) != 0)
				Controller->OpRegisters->Ports[Port] |= EHCI_PORT_COMPANION_HC;
		}
		else
			UsbEventCreate(HcCtrl, (int)Port, HcdConnectedEvent);
	}
	else
	{
		/* Disconnect */
		UsbEventCreate(HcCtrl, (int)Port, HcdDisconnectedEvent);
	}
}

/* Root hub scan */
void EhciPortScan(void *cData)
{
	/* Vars & Cast */
	EhciController_t *Controller = (EhciController_t*)cData;
	size_t i;

	/* Go through Ports */
	for (i = 0; i < Controller->Ports; i++)
	{
		/* Check íf Port has connected */
		EhciPortCheck(Controller, i);
	}
}

/* Interrupt Handler */
int EhciInterruptHandler(void *Args)
{
	/* Cast */
	MCoreDevice_t *mDevice = (MCoreDevice_t*)Args;
	EhciController_t *Controller = 
		(EhciController_t*)mDevice->Driver.Data;

	/* Calculate which interrupts we accept */
	uint32_t IntrState =
		(Controller->OpRegisters->UsbStatus & Controller->OpRegisters->UsbIntr);

	/* Sanity */
	if (IntrState == 0)
		return X86_IRQ_NOT_HANDLED;

	/* Debug */
	LogDebug("EHCI", "Controller Interrupt %u: 0x%x",
		Controller->HcdId, IntrState);

	/* Ok, lets see */
	if (IntrState & (EHCI_STATUS_PROCESS | EHCI_STATUS_PROCESSERROR)) {
		/* Scan for completion/error */
	}

	/* Hub Change? */
	if (IntrState & EHCI_STATUS_PORTCHANGE) {
		UsbEventCreate(UsbGetHcd(Controller->HcdId), 0, HcdRootHubEvent);
	}

	/* Fatal Error? */
	if (IntrState & EHCI_STATUS_HOSTERROR) {
		UsbEventCreate(UsbGetHcd(Controller->HcdId), 0, HcdFatalEvent);
	}

	/* Doorbell? */
	if (IntrState & EHCI_STATUS_ASYNC_DOORBELL) {

	}

	/* Acknowledge */
	Controller->OpRegisters->UsbStatus = IntrState;

	/* Done! */
	return X86_IRQ_HANDLED;
}