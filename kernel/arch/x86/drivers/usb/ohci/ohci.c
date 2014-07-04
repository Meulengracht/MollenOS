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
* MollenOS X86-32 USB OHCI Controller Driver
*/

/* Includes */
#include <drivers\usb\ohci\ohci.h>
#include <arch.h>
#include <memory.h>
#include <heap.h>
#include <list.h>
#include <stdio.h>

/* Globals */
list_t *glb_ohci_controllers = NULL;
volatile uint32_t glb_ohci_id = 0;

/* Externs */

/* Prototypes */
void ohci_interrupt_handler(void *data);

/* Error Codes */
const char *ohci_err_msgs[] = 
{
	"No Error",
	"CRC Error",
	"Bit Stuffing Violation",
	"Data Toggle Mismatch",
	"Stall PID recieved",
	"Device Not Responding",
	"PID Check Failure",
	"Unexpected PID",
	"Data Overrun",
	"Data Underrun",
	"Reserved",
	"Reserved",
	"Buffer Overrun",
	"Buffer Underrun",
	"Not Accessed",
	"Not Accessed"
};

void ohci_init(pci_driver_t *device, int irq_override)
{
	ohci_controller_t *controller;

	/* Sanity */
	if (glb_ohci_controllers == NULL)
		glb_ohci_controllers = list_create(LIST_NORMAL);
	
	/* Allocate Resources for this controller */
	controller = (ohci_controller_t*)kmalloc(sizeof(ohci_controller_t));
	controller->pci_info = device;
	controller->id = glb_ohci_id;

	/* Determine Irq */
	if (irq_override != -1)
		controller->irq = (uint32_t)irq_override;
	else
		controller->irq = device->header->interrupt_line;

	/* Enable memory and bus mastering */
	pci_write_word((const uint16_t)device->bus, (const uint16_t)device->device, (const uint16_t)device->function, 0x4, 0x6);

	/* Get location of registers */
	controller->control_space = device->header->bar0;

	/* Sanity */
	if (controller->control_space == 0 || (controller->control_space & 0x1))
	{
		/* Yea, give me my hat back */
		kfree(controller);
		return;
	}

	/* Now we initialise */
	glb_ohci_id++;

	/* Memory map needed space */
	controller->registers = (volatile ohci_registers_t*)memory_map_system_memory(controller->control_space, 1);
	controller->hcca_space = (uint32_t)physmem_alloc_block_dma();
	controller->hcca = (volatile ohci_hcca_t*)controller->hcca_space;
	controller->int_table = (ohci_int_table_t*)(controller->hcca_space + 256);	/* Hcca is 256 bytes in size */

	/* Install IRQ Handler */
	interrupt_install(controller->irq, ohci_interrupt_handler, controller);

	/* Debug */
	printf("OHCI - Id %u, Irq %u, bar0: 0x%x (0x%x), dma: 0x%x\n", 
		controller->id, controller->irq, controller->control_space,
		(addr_t)controller->registers, controller->hcca_space);

	/* Do we have control of this controller? */
	if (controller->registers->HcControl & (1 << 8))
	{
		printf("SMM Has control, returning hat\n");
		/* Ok, SMM has control, now give me my hat back */
		controller->registers->HcCommandStatus |= 0x8;		/* Set bit 3 */

		/* Wait for InterruptRouting to clear TODO timeout */
		while (controller->registers->HcControl & (1 << 8));
		printf("got hat!\n");
	}
	else if (controller->registers->HcControl & 0xC0)
	{
		printf("BIOS Has control, returning hat\n");

		if ((controller->registers->HcControl & 0xC0) != 0x80)
		{
			/* Resume Usb Operations */
			controller->registers->HcControl &= ~0xC0;
			controller->registers->HcControl |= 0x40;
		}

		printf("got hat!\n");
	}
}

/* Interrupt Handler */
void ohci_interrupt_handler(void *data)
{
	data = data;
}