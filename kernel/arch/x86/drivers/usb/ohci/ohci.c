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
#include <string.h>

/* Globals */
list_t *glb_ohci_controllers = NULL;
volatile uint32_t glb_ohci_id = 0;

/* Externs */

/* Prototypes */
void ohci_interrupt_handler(void *data);
void ohci_reset(ohci_controller_t *controller);

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


/* Helpers */
void ohci_set_mode(ohci_controller_t *controller, uint32_t mode)
{
	/* First we clear the current Operation Mode */
	controller->registers->HcControl &= ~X86_OHCI_CTRL_FSTATE_BITS;
	controller->registers->HcControl |= mode;
}

void ohci_toggle_frt(ohci_controller_t *controller)
{
	if (controller->registers->HcFmInterval & X86_OHCI_FRMV_FRT)
		controller->registers->HcFmInterval &= ~X86_OHCI_FRMV_FRT;
	else
		controller->registers->HcFmInterval |= X86_OHCI_FRMV_FRT;
}

/* Function Allocates Resources 
 * and starts a init thread */
void ohci_init(pci_driver_t *device, int irq_override)
{
	ohci_controller_t *controller = NULL;

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
	interrupt_install(controller->irq, INTERRUPT_USB_OHCI, ohci_interrupt_handler, controller);

	/* Debug */
	printf("OHCI - Id %u, Irq %u, bar0: 0x%x (0x%x), dma: 0x%x\n", 
		controller->id, controller->irq, controller->control_space,
		(addr_t)controller->registers, controller->hcca_space);

	
	/* Reset Controller */
	ohci_reset(controller);
}

/* Resets the controllre to a working state from initial */
void ohci_reset(ohci_controller_t *controller)
{
	uint32_t temp_value = 0;
	int i;

	/* Step 1. Verify the Revision */
	temp_value = (controller->registers->HcRevision & 0xFF);
	if (temp_value != X86_OHCI_REVISION)
	{
		printf("OHCI Revision is wrong (0x%x), exiting :(\n", temp_value);
		physmem_free_block(controller->hcca_space);
		kfree(controller);
		return;
	}

	/* Disable All Interrupts */
	controller->registers->HcInterruptDisable = (uint32_t)X86_OHCI_INTR_MASTER_INTR;

	/* Step 2. Gain control of controller */

	/* Is SMM the bitch? */
	if (controller->registers->HcControl & X86_OHCI_CTRL_INT_ROUTING)
	{
		/* Ok, SMM has control, now give me my hat back */
		controller->registers->HcCommandStatus |= X86_OHCI_CMD_OWNERSHIP;

		/* Wait for InterruptRouting to clear */
		i = 0;
		while ((i < 500) && (controller->registers->HcControl & X86_OHCI_CTRL_INT_ROUTING))
		{
			/* Idle idle idle */
			clock_stall(1);

			/* Increase I */
			i++;
		}

		if (i == 500)
		{
			/* Did not work, reset bit, try that */
			controller->registers->HcControl &= ~X86_OHCI_CTRL_INT_ROUTING;
			clock_stall(200);

			if (controller->registers->HcControl & X86_OHCI_CTRL_INT_ROUTING)
			{
				printf("OHCI: SMM Won't give us the controller, we're backing down >(\n");
				physmem_free_block(controller->hcca_space);
				kfree(controller);
				return;
			}
		}
	}
	/* Is BIOS the bitch?? */
	else if (controller->registers->HcControl & X86_OHCI_CTRL_FSTATE_BITS)
	{
		if ((controller->registers->HcControl & X86_OHCI_CTRL_FSTATE_BITS) != X86_OHCI_CTRL_USB_WORKING)
		{
			/* Resume Usb Operations */
			ohci_set_mode(controller, X86_OHCI_CTRL_USB_RESUME);

			/* Wait 10 ms */
			clock_stall(10);
		}
	}
	else
	{
		/* Cold Boot */

		/* Wait 10 ms */
		clock_stall(10);
	}

	/* Okiiii, reset controller, we need to save FmInterval */
	temp_value = controller->registers->HcFmInterval;

	/* Set bit 0 to request reboot */
	controller->registers->HcCommandStatus |= X86_OHCI_CMD_RESETCTRL;

	/* Wait for reboot (takes maximum of 10 ms) */
	clock_stall(20);

	/* Now restore FmInterval */
	controller->registers->HcFmInterval = temp_value;
	ohci_toggle_frt(controller);

	/* Controller is now in usb_suspend state (84 page)
	* if we stay in this state for more than 2 ms
	* usb resume state must be entered to resume usb operations */
	if ((controller->registers->HcControl & X86_OHCI_CTRL_FSTATE_BITS) == X86_OHCI_CTRL_USB_SUSPEND)
	{
		ohci_set_mode(controller, X86_OHCI_CTRL_USB_RESUME);
		clock_stall(100);
	}

	/* Initialise Virtual Queues */
	memset(controller->int_table, 0, sizeof(ohci_int_table_t));
	temp_value = controller->hcca_space + 256;

	/* Setup ms16 poll list */
	temp_value += 16 * sizeof(ohci_endpoint_desc_t);
	for (i = 0; i < 16; i++)
		controller->int_table->ms16[i].next_ed = temp_value + i / 2 * sizeof(ohci_endpoint_desc_t);

	/* Setup ms8 poll list */
	temp_value += 8 * sizeof(ohci_endpoint_desc_t);
	for (i = 0; i < 8; i++)
		controller->int_table->ms8[i].next_ed = temp_value + i / 2 * sizeof(ohci_endpoint_desc_t);

	/* Setup ms4 poll list */
	temp_value += 4 * sizeof(ohci_endpoint_desc_t);
	for (i = 0; i < 4; i++)
		controller->int_table->ms4[i].next_ed = temp_value + i / 2 * sizeof(ohci_endpoint_desc_t);

	/* Setup ms2 poll list */
	temp_value += 2 * sizeof(ohci_endpoint_desc_t);
	for (i = 0; i < 2; i++)
		controller->int_table->ms2[i].next_ed = temp_value + sizeof(ohci_endpoint_desc_t);

	/* Setup ms1 poll list */
	temp_value += 1 * sizeof(ohci_endpoint_desc_t);
	controller->int_table->ms1[0].next_ed = temp_value;

	/* Mark all skippable */
	for (i = 0; i < 32; i++)
	{
		ohci_endpoint_desc_t *ep = &controller->int_table->ms16[i];
		ep->flags |= X86_OHCI_EP_SKIP;
	}

	/* Setup HCCA IntTable */
	for (i = 0; i < 32; i++)
	{
		/* List from OHCI Specs */
		static const int tree_balance[] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };

		controller->hcca->interrupt_table[i] =
			controller->hcca_space + 256 + tree_balance[i & 15] * sizeof(ohci_endpoint_desc_t);
	}

	/* Initial values for frame */
	controller->hcca->current_frame = 0;
	controller->hcca->head_done = 0;

	/* Set HcHCCA to phys address of HCCA */
	controller->registers->HcHCCA = controller->hcca_space;

	/* Set TD Heads (Bulk & Control) */
	controller->registers->HcBulkHeadED = (uint32_t)&controller->int_table->stop_ed;
	controller->registers->HcControlHeadED = (uint32_t)&controller->int_table->stop_ed;

	/* Set HcEnableInterrupt to all except SOF */
	controller->registers->HcInterruptDisable = (uint32_t)(X86_OHCI_INTR_DISABLE_SOF | X86_OHCI_INTR_MASTER_INTR);
	controller->registers->HcInterruptStatus = ~(uint32_t)0;
	controller->registers->HcInterruptEnable = X86_OHCI_INTR_ENABLE_ALL;

	/* Disable queues for now */
	controller->registers->HcControl &= ~X86_OHCI_CTRL_DISABLE_QUEUES;
	controller->registers->HcControl |= X86_OHCI_CTRL_REMOTE_WAKE;

	/* Set HcPeriodicStart to a value that is 90% of FrameInterval in HcFmInterval */
	temp_value = (controller->registers->HcFmInterval & 0x3FFF);
	controller->registers->HcPeriodicStart = (temp_value / 10) * 9;

	/* The counter value indicates max value transfered 
	 * initially we want this cleared, and for some reason we need 
	 * to reset bit 30 afterwards :s */
	controller->registers->HcFmInterval &= ~X86_OHCI_MAX_PACKET_SIZE_BITS;
	controller->registers->HcFmInterval |= (1 << 30);
	ohci_toggle_frt(controller);

	/*
	LSThreshold contains a value which is compared to the FrameRemaining field prior to initiating a Low Speed transaction.
	The transaction is started only if FrameRemaining >= this field.
	The value is calculated by HCD with the consideration of transmission and setup overhead.
	*/
	controller->registers->HcLSThreshold = 0;

	printf("HcFrameInterval: %u", controller->registers->HcFmInterval & 0x3FFF);
	printf("  HcPeriodicStart: %u", controller->registers->HcPeriodicStart);
	printf("  FSMPS: %u bits", (controller->registers->HcFmInterval >> 16) & 0x7FFF);
	printf("  LSThreshhold: %u\n", controller->registers->HcLSThreshold & 0xFFF);

	/* Set Control Bulk Ratio */
	controller->registers->HcControl |= X86_OHCI_CTRL_SRATIO_BITS;

	/* Start controller by setting it to UsbOperational
	* and get port count from (DescriptorA & 0x7F) */
	controller->ports = controller->registers->HcRhDescriptorA & 0x7F;
	ohci_set_mode(controller, X86_OHCI_CTRL_USB_WORKING);

	/* Sanity */
	if (controller->ports > 15)
		controller->ports = 15;

	/* Turn on power for ports! */
	controller->registers->HcRhStatus |= X86_OHCI_STATUS_POWER_ON;

	/* Make sure root hub is not set as compound device */
	controller->registers->HcRhDescriptorA &= ~X86_OHCI_DESCA_DEVICE_TYPE;
	controller->registers->HcRhDescriptorB = 0;

	/* Get Power On Delay 
	 * PowerOnToPowerGoodTime (24 - 31)
	 * This byte specifies the duration HCD has to wait before 
	 * accessing a powered-on port of the Root Hub. 
	 * It is implementation-specific.  The unit of time is 2 ms.  
	 * The duration is calculated as POTPGT * 2 ms.
	 */
	temp_value = controller->registers->HcRhDescriptorA;
	temp_value >>= 24;
	temp_value *= 2;

	/* Sanity */
	if (temp_value > 20)
		temp_value = 20;

	controller->power_on_delay_ms = temp_value;

	/* Set HcControl to all queues enabled */
	controller->registers->HcControl = X86_OHCI_CTRL_ENABLE_QUEUES;

	printf("OHCI: Controller %u Started, ports %u\n", controller->id, controller->ports);

	for (i = 0; i < (int)controller->ports; i++)
		printf("Port Status: 0x%x\n", controller->registers->HcRhPortStatus[i]);

	/* Check us in with the USB Handler */

	/* Setup Ports , set bits 0, 1 and 4*/

	/* Start the TD handler thread */
}

/* Interrupt Handler 
 * Make sure that this controller actually made the interrupt 
 * as this interrupt will be shared with other OHCI's */
void ohci_interrupt_handler(void *data)
{
	uint32_t temp_value = 0;
	ohci_controller_t *controller = (ohci_controller_t*)data;

	/* Was it this controller that made the interrupt? */
	temp_value = controller->registers->HcInterruptStatus;

	if (temp_value == 0)
		return;

	/* Why yes, yes it was, wake up the TD handler thread 
	 * if it was head_done_writeback */
	if (temp_value & X86_OHCI_INTR_HEAD_DONE)
	{
		/* Wuhu, handle this! */
	}

	/* Reset Interrupt Status */
	controller->registers->HcInterruptStatus = temp_value;

	/* Check interrupt error states */

	/* Scheduling Overrun? */
	if (temp_value & X86_OHCI_INTR_SCHEDULING_OVRRN)
		printf("OHCI %u: Scheduling Overrun\n", controller->id);
	
	/* Resume Detection? */
	if (temp_value & X86_OHCI_INTR_RESUME_DETECT)
		printf("OHCI %u: Resume Detected\n", controller->id);

	/* Fatal Error? */
	if (temp_value & X86_OHCI_INTR_FATAL_ERROR)
	{
		printf("OHCI %u: Fatal Error, resetting...\n", controller->id);
		controller->registers->HcCommandStatus |= X86_OHCI_CMD_RESETCTRL;
	}

	/* Frame Overflow */
	if (temp_value & X86_OHCI_INTR_FRAME_OVERFLOW)
		printf("OHCI %u: Frame Overflow\n", controller->id);

	/* Root Hub Status Change 
	 * Do a port status check */
	if (temp_value & X86_OHCI_INTR_ROOT_HUB_EVENT)
		printf("OHCI %u: Root Hub Status Event\n", controller->id);

	/* Ownership Event */
	if (temp_value & X86_OHCI_INTR_OWNERSHIP_EVENT)
		printf("OHCI %u: Ownership Event\n", controller->id);
}