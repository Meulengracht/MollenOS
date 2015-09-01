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
#include <Arch.h>
#include <LApic.h>
#include <assert.h>
#include <Memory.h>
#include <Scheduler.h>
#include <Heap.h>
#include <List.h>
#include <stdio.h>
#include <string.h>
#include <SysTimers.h>

#include <Drivers\Usb\Usb.h>
#include <Drivers\Usb\Ehci\Ehci.h>

/* Globals */
volatile uint32_t GlbEhciId = 0;

/* Initialise Controller from PCI */
void EhciInit(PciDevice_t *PciDevice)
{
	volatile EchiCapabilityRegisters_t *CapRegs;
	volatile EchiOperationalRegisters_t *OpRegs;
	uint32_t Eecp;
	uint32_t cmd;
	uint32_t To;

	/* Enable memory io and bus mastering, keep interrupts disabled */
	PciWriteWord((const uint16_t)PciDevice->Bus, (const uint16_t)PciDevice->Device, (const uint16_t)PciDevice->Function, 0x4, 0x0406);

	/* Pci Registers 
	 * BAR0 - Usb Base Registers 
	 * 0x60 - Revision 
	 * 0x61 - Frame Length Adjustment
	 * 0x62/3 - Port Wake capabilities 
	 * ????? - Usb Legacy Support Extended Capability Register
	 * ???? + 4 - Usb Legacy Support Control And Status Register
	 * The above means ???? = EECP. EECP Offset in PCI space where
	 * we can find the above registers */
	CapRegs = (EchiCapabilityRegisters_t*)MmVirtualMapSysMemory(PciDevice->Header->Bar0, 1);
	cmd = Eecp = ((CapRegs->CParams >> 8) & 0xFF);

	/* Two cases, if EECP is valid we do additional steps */
	if (Eecp >= 0x40)
	{
		uint8_t Semaphore = 0;
		uint8_t CapId = 0;
		uint8_t Failed = 0;
		uint32_t Timeout = 0;

		/* Get the extended capability register 
		 * We read the second byte, because it contains 
		 * the BIOS Semaphore */
		Failed = 0;
		CapId = PciReadByte((uint16_t)PciDevice->Bus, (uint16_t)PciDevice->Device, (uint16_t)PciDevice->Function, Eecp);
		Semaphore = PciReadByte((uint16_t)PciDevice->Bus, (uint16_t)PciDevice->Device, (uint16_t)PciDevice->Function, Eecp + 0x2);

		/* Is it BIOS owned? First bit in second byte */
		if (Semaphore & 0x1)
		{
			/* Request for my hat back :/ 
			 * Third byte contains the OS Semaphore */
			PciWriteByte((uint16_t)PciDevice->Bus, (uint16_t)PciDevice->Device, (uint16_t)PciDevice->Function, Eecp + 0x3, 0x1);

			/* Now we wait for the hat to return */
			Timeout = 0;
			Semaphore = PciReadByte((uint16_t)PciDevice->Bus, (uint16_t)PciDevice->Device, (uint16_t)PciDevice->Function, Eecp + 0x2);
			while ((Timeout < 1000) && (Semaphore & 0x1))
			{
				StallMs(1);
				Semaphore = PciReadByte((uint16_t)PciDevice->Bus, (uint16_t)PciDevice->Device, (uint16_t)PciDevice->Function, Eecp + 0x2);
				Timeout++;
			}

			/* Sanity */
			if (Timeout == 1000)
				Failed = 1;

			/* Now, we wait for OS semaphore to be 1 */
			if (!Failed)
			{
				Timeout = 0;
				Semaphore = PciReadByte((uint16_t)PciDevice->Bus, (uint16_t)PciDevice->Device, (uint16_t)PciDevice->Function, Eecp + 0x3);
				while ((Timeout < 1000) && (!(Semaphore & 0x1)))
				{
					StallMs(1);
					Semaphore = PciReadByte((uint16_t)PciDevice->Bus, (uint16_t)PciDevice->Device, (uint16_t)PciDevice->Function, Eecp + 0x3);
					Timeout++;
				}

				/* Sanity */
				if (Timeout == 1000)
					Failed = 1;
			}
			
			/* Disable SMI by setting all lower 16 bits to 0 of EECP+4 */
			if (CapId & 1)
				PciWriteDword((uint16_t)PciDevice->Bus, (uint16_t)PciDevice->Device, (uint16_t)PciDevice->Function, Eecp + 0x4, 0x0000);
		}
	}

	/* Now we are almost done 
	 * Get operational registers */
	OpRegs = (EchiOperationalRegisters_t*)((Addr_t)CapRegs + CapRegs->Length);
	
	/* Stop scheduler */
	cmd = OpRegs->UsbCommand;
	cmd &= ~(0x30);
	OpRegs->UsbCommand = cmd;

	/* Wait for stop */
	To = 0;
	while (OpRegs->UsbStatus & 0xC000 
		&& To < 100)
	{
		StallMs(5);
		To++;
	}
		
	/* Stop controller */
	cmd = OpRegs->UsbCommand;
	cmd &= ~(0x1);
	OpRegs->UsbCommand = cmd;
	OpRegs->UsbIntr = 0;

	/* Wait for stop */
	To = 0;
	while (!(OpRegs->UsbStatus & 0x1000)
		&& To < 100)
	{
		StallMs(5);
		To++;
	}

	/* Clear Configured Flag */
	OpRegs->ConfigFlag = 0;

	/* Now everything is routed to companion controllers */
}