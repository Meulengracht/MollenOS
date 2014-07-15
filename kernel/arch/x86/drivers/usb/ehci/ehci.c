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
#include <arch.h>
#include <lapic.h>
#include <assert.h>
#include <memory.h>
#include <scheduler.h>
#include <heap.h>
#include <list.h>
#include <stdio.h>
#include <string.h>

#include <drivers\usb\usb.h>
#include <drivers\usb\ehci\ehci.h>

/* Globals */
list_t *glb_ehci_controllers = NULL;
volatile uint32_t glb_ehci_id = 0;

/* Initialise Controller from PCI */
void ehci_init(pci_driver_t *device)
{
	//uint32_t pin = 0xFF;
	ehci_capability_registers_t *cap_registers;
	ehci_operational_registers_t *op_registers;
	uint32_t eecp;

	/* Sanity */
	if (glb_ehci_controllers == NULL)
		glb_ehci_controllers = list_create(LIST_NORMAL);

	/* Pci Registers 
	 * BAR0 - Usb Base Registers 
	 * 0x60 - Revision 
	 * 0x61 - Frame Length Adjustment
	 * 0x62/3 - Port Wake capabilities 
	 * ????? - Usb Legacy Support Extended Capability Register
	 * ???? + 4 - Usb Legacy Support Control And Status Register
	 * The above means ???? = EECP. EECP Offset in PCI space where
	 * we can find the above registers */
	cap_registers = (ehci_capability_registers_t*)memory_map_system_memory(device->header->bar0, 1);
	eecp = (cap_registers->cparams & 0x0000FF00) >> 8;

	/* Two cases, if EECP is valid we do additional steps */
	if (eecp >= 0x40)
	{
		uint8_t semaphore;
		uint8_t cap_id;
		uint8_t failed;
		uint32_t timeout;

		/* Get the extended capability register 
		 * We read the second byte, because it contains 
		 * the BIOS Semaphore */
		failed = 0;
		semaphore = pci_read_byte((uint16_t)device->bus, (uint16_t)device->device, (uint16_t)device->function, eecp + 0x2);

		/* Is it BIOS owned? First bit in second byte */
		if (semaphore & (1 << 0))
		{
			/* Request for my hat back :/ 
			 * Third byte contains the OS Semaphore */
			pci_write_byte((uint16_t)device->bus, (uint16_t)device->device, (uint16_t)device->function, eecp + 0x3, 0x1);

			/* Now we wait for the hat to return */
			timeout = 0;
			while ((timeout < 1000) && (semaphore & (1 << 0)))
			{
				clock_stall(1);
				semaphore = pci_read_byte((uint16_t)device->bus, (uint16_t)device->device, (uint16_t)device->function, eecp + 0x2);
				timeout++;
			}

			/* Sanity */
			if (timeout == 1000)
				failed = 1;

			/* Now, we wait for OS semaphore to be 1 */
			if (!failed)
			{
				semaphore = pci_read_byte((uint16_t)device->bus, (uint16_t)device->device, (uint16_t)device->function, eecp + 0x3);
				timeout = 0;
				while ((timeout < 1000) && (!(semaphore & (1 << 0))))
				{
					clock_stall(1);
					semaphore = pci_read_byte((uint16_t)device->bus, (uint16_t)device->device, (uint16_t)device->function, eecp + 0x3);
					timeout++;
				}

				/* Sanity */
				if (timeout == 1000)
					failed = 1;
			}
			
			/* Wuhuu, hat returned?? */
			if (failed)
			{
				/* You evul bios :( 
				 * now we disable the controller manually :( */
				cap_id = pci_read_byte((uint16_t)device->bus, (uint16_t)device->device, (uint16_t)device->function, eecp);

				/* Legacy Support? */
				if (cap_id == 0x01)
				{
					/* Disable SMI by setting all lower 16 bits to 0 of EECP+4 */
					pci_write_word((uint16_t)device->bus, (uint16_t)device->device, (uint16_t)device->function, eecp + 0x4, 0x0000);
				}
			}
		}
	}

	/* Now we are almost done 
	 * Get operational registers */
	op_registers = (ehci_operational_registers_t*)((addr_t)cap_registers + cap_registers->length);

	/* Clear Configured Flag */
	op_registers->config_flag = 0;

	/* Now everything is routed to companion controllers */
}