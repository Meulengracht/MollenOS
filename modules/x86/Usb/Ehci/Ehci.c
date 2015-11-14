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
* MollenOS X86-32 USB EHCI Controller Driver
*/

/* Includes */
#include <Module.h>
#include "Ehci.h"

/* Additional Includes */
#include <UsbCore.h>
#include <Scheduler.h>
#include <Heap.h>
#include <Timers.h>

/* Better abstraction? Woah */
#include <x86\Pci.h>
#include <x86\Memory.h>

/* Globals */
volatile uint32_t GlbEhciId = 0;

/* Entry point of a module */
MODULES_API void ModuleInit(Addr_t *FunctionTable, void *Data)
{
	PciDevice_t *Device = (PciDevice_t*)Data;
	volatile EchiCapabilityRegisters_t *CapRegs;
	volatile EchiOperationalRegisters_t *OpRegs;
	uint32_t Eecp;
	volatile uint32_t cmd;

	/* Save this */
	GlbFunctionTable = FunctionTable;

	/* Enable memory io and bus mastering, remove interrupts disabled */
	uint16_t PciCommand = (uint16_t)PciDeviceRead(Device, 0x4, 2);
	PciDeviceWrite(Device, 0x4, (PciCommand & ~(0x400)) | 0x2 | 0x4, 2);

	/* Pci Registers 
	 * BAR0 - Usb Base Registers 
	 * 0x60 - Revision 
	 * 0x61 - Frame Length Adjustment
	 * 0x62/3 - Port Wake capabilities 
	 * ????? - Usb Legacy Support Extended Capability Register
	 * ???? + 4 - Usb Legacy Support Control And Status Register
	 * The above means ???? = EECP. EECP Offset in PCI space where
	 * we can find the above registers */
	CapRegs = (EchiCapabilityRegisters_t*)MmVirtualMapSysMemory(Device->Header->Bar0, 1);
	cmd = Eecp = ((CapRegs->CParams >> 8) & 0xFF);

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
			CapId = (uint8_t)PciDeviceRead(Device, Eecp, 1);

			/* Legacy Support? */
			if (CapId == 0x01)
				break;

			/* No, get next Eecp */
			NextEecp = (uint8_t)PciDeviceRead(Device, Eecp + 0x1, 1);

			/* Sanity */
			if (NextEecp == 0x00)
				break;
			else
				Eecp = NextEecp;
		}
		
		/* Only continue if Id == 0x01 */
		if (CapId == 0x01)
		{
			Semaphore = (uint8_t)PciDeviceRead(Device, Eecp + 0x2, 1);

			/* Is it BIOS owned? First bit in second byte */
			if (Semaphore & 0x1)
			{
				/* Request for my hat back :/
				* Third byte contains the OS Semaphore */
				PciDeviceWrite(Device, Eecp + 0x3, 0x1, 1);

				/* Now we wait for the bios to release semaphore */
				WaitForCondition((PciDeviceRead(Device, Eecp + 0x2, 1) & 0x1) == 0, 250, 10, "USB_EHCI: Failed to release BIOS Semaphore\n");
				WaitForCondition((PciDeviceRead(Device, Eecp + 0x3, 1) & 0x1) == 1, 250, 10, "USB_EHCI: Failed to set OS Semaphore\n");
			}

			/* Disable SMI by setting all lower 16 bits to 0 of EECP+4 */
			PciDeviceWrite(Device, Eecp + 0x4, 0x0000, 2);
		}
	}

	/* Now we are almost done 
	 * Get operational registers */
	OpRegs = (EchiOperationalRegisters_t*)((Addr_t)CapRegs + CapRegs->Length);
	
	/* Stop scheduler */
	cmd = OpRegs->UsbCommand;
	cmd &= ~(0x10 | 0x20);
	OpRegs->UsbCommand = cmd;

	/* Wait for stop */
	WaitForCondition((OpRegs->UsbStatus & 0xC000) == 0, 250, 10, "USB_EHCI: Failed to stop scheduler\n");
		
	/* Stop controller */
	cmd = OpRegs->UsbCommand;
	cmd &= ~(0x1);
	OpRegs->UsbCommand = cmd;
	OpRegs->UsbIntr = 0;

	/* Wait for stop */
	WaitForCondition((OpRegs->UsbStatus & 0x1000) != 0, 250, 10, "USB_EHCI: Failed to stop controller\n");

	/* Clear Configured Flag */
	OpRegs->ConfigFlag = 0;

	/* Now everything is routed to companion controllers */
}