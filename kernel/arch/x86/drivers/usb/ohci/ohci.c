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
#include <drivers\usb\ohci\ohci.h>

/* Globals */
list_t *glb_ohci_controllers = NULL;
volatile uint32_t glb_ohci_id = 0;

/* Externs */
extern void clock_stall_noint(time_t ms);
extern void _yield(void);

/* Prototypes */
void ohci_interrupt_handler(void *data);
void ohci_reset(ohci_controller_t *controller);
void ohci_setup(ohci_controller_t *controller);

void ohci_transaction_init(void *controller, usb_hc_request_t *request);
usb_hc_transaction_t *ohci_transaction_setup(void *controller, usb_hc_request_t *request);
usb_hc_transaction_t *ohci_transaction_in(void *controller, usb_hc_request_t *request);
usb_hc_transaction_t *ohci_transaction_out(void *controller, usb_hc_request_t *request);
void ohci_transaction_send(void *controller, usb_hc_request_t *request);

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

void ohci_reset_indices(ohci_controller_t *controller, uint32_t type)
{
	if (type & X86_OHCI_INDEX_TYPE_CONTROL)
	{
		controller->ed_index_control = X86_OHCI_POOL_ED_CONTROL_START;
		controller->td_index_control = X86_OHCI_POOL_TD_CONTROL_START;
	}
	
	if (type & X86_OHCI_INDEX_TYPE_BULK)
	{
		controller->ed_index_bulk = X86_OHCI_POOL_ED_BULK_START;
		controller->td_index_bulk = X86_OHCI_POOL_TD_BULK_START;
	}
}

void ohci_validate_indices(ohci_controller_t *controller)
{
	/* Check Control ED/TD */
	if ((controller->ed_index_control == (X86_OHCI_POOL_ED_BULK_START - 2))
		|| (controller->ed_index_control == (X86_OHCI_POOL_ED_BULK_START - 1))
		|| (controller->td_index_control == (X86_OHCI_POOL_TD_BULK_START - 2))
		|| (controller->td_index_control == (X86_OHCI_POOL_TD_BULK_START - 1)))
		ohci_reset_indices(controller, X86_OHCI_INDEX_TYPE_CONTROL);

	/* Check Bulk ED/TD */
	if ((controller->ed_index_bulk >= (X86_OHCI_POOL_NUM_ED) - 2)
		|| (controller->td_index_bulk >= (X86_OHCI_POOL_NUM_TD) - 2))
		ohci_reset_indices(controller, X86_OHCI_INDEX_TYPE_BULK);
}

addr_t ohci_align(addr_t addr, addr_t alignment_bits, addr_t alignment)
{
	addr_t aligned_addr = addr;

	if (aligned_addr & alignment_bits)
	{
		aligned_addr &= ~alignment_bits;
		aligned_addr += alignment;
	}

	return aligned_addr;
}

/* Callbacks */
void ohci_port_status(void *ctrl_data, usb_hc_port_t *port)
{
	ohci_controller_t *controller = (ohci_controller_t*)ctrl_data;
	uint32_t status = controller->registers->HcRhPortStatus[port->id];

	/* Update information in port */

	/* Is it connected? */
	if (status & X86_OHCI_PORT_CONNECTED)
		port->connected = 1;
	else
		port->connected = 0;

	/* Is it enabled? */
	if (status & X86_OHCI_PORT_ENABLED)
		port->enabled = 1;
	else
		port->enabled = 0;

	/* Is it full-speed? */
	if (status & X86_OHCI_PORT_LOW_SPEED)
		port->full_speed = 0;
	else
		port->full_speed = 1;

	printf("OHCI: Port Status %u: 0x%x\n", port->id, status);
}

/* Port Functions */

/* This resets a port, this is only ever
 * called from an interrupt and thus we can't use clock_stall :/ */
void ohci_port_reset(ohci_controller_t *controller, uint32_t port)
{
	int i = 0;

	/* Set reset */
	controller->registers->HcRhPortStatus[port] = X86_OHCI_PORT_RESET;

	/* Wait with timeout */
	while ((controller->registers->HcRhPortStatus[port] & X86_OHCI_PORT_RESET)
		&& (i < 1000))
	{
		/* Increase timeout */
		i++;

		/* Stall */
		clock_stall_noint(20000);
	}

	/* Set Enable */
	if (controller->power_mode == X86_OHCI_POWER_PORT_CONTROLLED)
		controller->registers->HcRhPortStatus[port] = X86_OHCI_PORT_ENABLED | X86_OHCI_PORT_POWER_ENABLE;
	else
		controller->registers->HcRhPortStatus[port] = X86_OHCI_PORT_ENABLED;
	
	/* Stall */
	clock_stall_noint(100000);
}

void ohci_port_check(ohci_controller_t *controller, uint32_t port)
{
	usb_hc_t *hc;

	/* Was it connect event and not disconnect ? */
	if (controller->registers->HcRhPortStatus[port] & X86_OHCI_PORT_CONNECT_EVENT)
	{
		if (controller->registers->HcRhPortStatus[port] & X86_OHCI_PORT_CONNECTED)
		{
			/* Reset on Attach */
			ohci_port_reset(controller, port);
		}
		else
		{
			/* Nah, disconnect event */

			/* Get HCD data */
			hc = usb_get_hcd(controller->hcd_id);

			/* Sanity */
			if (hc == NULL)
				return;

			/* Disconnect */
			usb_event_create(hc, port, X86_USB_EVENT_DISCONNECTED);

		}

		/* If device is enabled, and powered, set it up */
		if ((controller->registers->HcRhPortStatus[port] & X86_OHCI_PORT_ENABLED)
			&& (controller->registers->HcRhPortStatus[port] & X86_OHCI_PORT_POWER_ENABLE))
		{
			/* Get HCD data */
			hc = usb_get_hcd(controller->hcd_id);

			/* Sanity */
			if (hc == NULL)
			{
				printf("OHCI: Controller %u is zombie and is trying to register ports!!\n", controller->id);
				return;
			}

			/* Register Device */
			usb_event_create(hc, port, X86_USB_EVENT_CONNECTED);
		}
	}

	/* Clear Connect Event */
	if (controller->registers->HcRhPortStatus[port] & X86_OHCI_PORT_CONNECT_EVENT)
		controller->registers->HcRhPortStatus[port] = X86_OHCI_PORT_CONNECT_EVENT;

	/* If Enable Event bit is set, clear it */
	if (controller->registers->HcRhPortStatus[port] & X86_OHCI_PORT_ENABLE_EVENT)
		controller->registers->HcRhPortStatus[port] = X86_OHCI_PORT_ENABLE_EVENT;

	/* If Suspend Event is set, clear it */
	if (controller->registers->HcRhPortStatus[port] & X86_OHCI_PORT_SUSPEND_EVENT)
		controller->registers->HcRhPortStatus[port] = X86_OHCI_PORT_SUSPEND_EVENT;

	/* If Over Current Event is set, clear it */
	if (controller->registers->HcRhPortStatus[port] & X86_OHCI_PORT_OVR_CURRENT_EVENT)
		controller->registers->HcRhPortStatus[port] = X86_OHCI_PORT_OVR_CURRENT_EVENT;

	/* If reset bit is set, clear it */
	if (controller->registers->HcRhPortStatus[port] & X86_OHCI_PORT_RESET_EVENT)
		controller->registers->HcRhPortStatus[port] = X86_OHCI_PORT_RESET_EVENT;
}

void ohci_ports_check(ohci_controller_t *controller)
{
	int i;

	/* Go through ports */
	for (i = 0; i < (int)controller->ports; i++)
	{
		/* Check íf port has connected */
		ohci_port_check(controller, i);
	}
}

/* Function Allocates Resources 
 * and starts a init thread */
void ohci_init(pci_driver_t *device, int irq_override)
{
	ohci_controller_t *controller = NULL;
	uint32_t pin = 0xFF;

	/* Sanity */
	if (glb_ohci_controllers == NULL)
		glb_ohci_controllers = list_create(LIST_NORMAL);
	
	/* Allocate Resources for this controller */
	controller = (ohci_controller_t*)kmalloc(sizeof(ohci_controller_t));
	controller->pci_info = device;
	controller->id = glb_ohci_id;

	/* Determine Irq */
	if (irq_override != -1)
	{
		controller->irq = (uint32_t)irq_override;
		pin = device->header->interrupt_pin;
	}
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

	/* Memset HCCA Space */
	memset((void*)controller->hcca, 0, 0x1000);

	/* Install IRQ Handler */
	interrupt_install_pci(controller->irq, pin, ohci_interrupt_handler, controller);

	/* Debug */
	printf("OHCI - Id %u, Irq %u, bar0: 0x%x (0x%x), dma: 0x%x\n", 
		controller->id, controller->irq, controller->control_space,
		(addr_t)controller->registers, controller->hcca_space);

	
	/* Reset Controller */
	ohci_setup(controller);
}

/* Resets the controllre to a working state from initial */
void ohci_setup(ohci_controller_t *controller)
{
	usb_hc_t *hc;
	uint32_t temp_value = 0;
	addr_t buffer_address = 0, buffer_address_max = 0;
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

	/* Initialise ED Pool */
	for (i = 0; i < X86_OHCI_POOL_NUM_ED; i++)
	{
		addr_t a_space = (addr_t)kmalloc(sizeof(ohci_endpoint_desc_t) + X86_OHCI_STRUCT_ALIGN);
		controller->ed_pool[i] = (ohci_endpoint_desc_t*)ohci_align(a_space, X86_OHCI_STRUCT_ALIGN_BITS, X86_OHCI_STRUCT_ALIGN);
		memset((void*)controller->ed_pool[i], 0, sizeof(ohci_endpoint_desc_t));
	}

	/* Setup ED List
	 * We use the first 20 ED's for Control Transfers 
	 * And the next 30 for Bulk Transfers 
	 * This means we have two seperate lists, however under one "roof" */
	for (i = 0; i < X86_OHCI_POOL_NUM_ED; i++)
	{
		/* Is it end of Control List ? */
		if (i == X86_OHCI_POOL_ED_BULK_START - 1)
			controller->ed_pool[i]->next_ed = 0;
		/* Is it end of bulk list ? */
		else if (i == X86_OHCI_POOL_NUM_ED - 1)
			controller->ed_pool[i]->next_ed = 0;
		else
		{
			/* Otherwise, link this ED to the next in list
			* and set it as SKIP initially since we have no transactions yet */
			controller->ed_pool[i]->flags |= X86_OHCI_EP_SKIP;
			controller->ed_pool[i]->next_ed = memory_getmap(NULL, (virtaddr_t)controller->ed_pool[i + 1]);
		}
	}

	/* Setup initial ED points */
	controller->registers->HcControlHeadED =
		controller->registers->HcControlCurrentED = memory_getmap(NULL, (virtaddr_t)controller->ed_pool[X86_OHCI_POOL_ED_CONTROL_START]);
	controller->registers->HcBulkHeadED =
		controller->registers->HcBulkCurrentED = memory_getmap(NULL, (virtaddr_t)controller->ed_pool[X86_OHCI_POOL_ED_BULK_START]);

	/* Initialise TD Pool */
	for (i = 0; i < X86_OHCI_POOL_NUM_TD; i++)
	{
		addr_t a_space = (addr_t)kmalloc(sizeof(ohci_gtransfer_desc_t) + X86_OHCI_STRUCT_ALIGN);
		controller->td_pool[i] = (ohci_gtransfer_desc_t*)ohci_align(a_space, X86_OHCI_STRUCT_ALIGN_BITS, X86_OHCI_STRUCT_ALIGN);
		controller->td_pool_phys[i] = memory_getmap(NULL, (virtaddr_t)controller->td_pool[i]);
		memset((void*)controller->td_pool[i], 0, sizeof(ohci_gtransfer_desc_t));
	}

	/* Initialise TD Pool Buffers */
	buffer_address = (addr_t)kmalloc_a(0x1000);
	buffer_address_max = buffer_address + 0x1000 - 1;
	for (i = 0; i < X86_OHCI_POOL_NUM_TD; i++)
	{
		/* Allocate another page? */
		if (buffer_address > buffer_address_max)
		{
			buffer_address = (addr_t)kmalloc_a(0x1000);
			buffer_address_max = buffer_address + 0x1000 - 1;
		}

		/* Setup buffer */
		controller->td_pool_buffers[i] = (addr_t*)buffer_address;
		controller->td_pool[i]->cbp = memory_getmap(NULL, buffer_address);
		buffer_address += 0x200;
	}

	/* Reset Indices */
	ohci_reset_indices(controller, X86_OHCI_INDEX_TYPE_CONTROL | X86_OHCI_INDEX_TYPE_BULK);

	/* Initial values for frame */
	controller->hcca->current_frame = 0;
	controller->hcca->head_done = 0;

	/* Set HcHCCA to phys address of HCCA */
	controller->registers->HcHCCA = controller->hcca_space;

	/* Set HcEnableInterrupt to all except SOF */
	controller->registers->HcInterruptDisable = (uint32_t)(X86_OHCI_INTR_DISABLE_SOF | X86_OHCI_INTR_MASTER_INTR);
	controller->registers->HcInterruptStatus = ~(uint32_t)0;
	controller->registers->HcInterruptEnable = X86_OHCI_INTR_ENABLE_ALL;

	/* Disable queues for now */
	controller->registers->HcControl &= ~X86_OHCI_CTRL_ALL_LISTS;
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
	printf("HcFrameInterval: %u", controller->registers->HcFmInterval & 0x3FFF);
	printf("  HcPeriodicStart: %u", controller->registers->HcPeriodicStart);
	printf("  FSMPS: %u bytes", (controller->registers->HcFmInterval >> 16) & 0x7FFF);
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

	/* Check Power Mode */
	if (controller->registers->HcRhDescriptorA & (1 << 9))
		controller->power_mode = X86_OHCI_POWER_ALWAYS_ON;
	else
	{
		/* Ports are power-switched 
		 * Check Mode */
		if (controller->registers->HcRhDescriptorA & (1 << 8))
		{
			/* This is favorable mode 
			 * (If this is supported we set power-mask so that all ports control their own power) */
			controller->power_mode = X86_OHCI_POWER_PORT_CONTROLLED;
			controller->registers->HcRhDescriptorB = 0xFFFF0000;
		}
		else
		{
			/* Global Power Switch */
			controller->registers->HcRhDescriptorB = 0;
			controller->registers->HcRhStatus |= X86_OHCI_STATUS_POWER_ON;
			controller->power_mode = X86_OHCI_POWER_PORT_GLOBAL;
		}
	}

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

	printf("OHCI: Controller %u Started, ports %u (power mode %u)\n", 
		controller->id, controller->ports, controller->power_mode);

	/* Setup HCD */
	hc = usb_init_controller((void*)controller, X86_USB_TYPE_OHCI, controller->ports);

	hc->port_status = ohci_port_status;

	hc->transaction_init = ohci_transaction_init;
	hc->transaction_setup = ohci_transaction_setup;
	hc->transaction_in = ohci_transaction_in;
	hc->transaction_out = ohci_transaction_out;
	hc->transaction_send = ohci_transaction_send;

	controller->hcd_id = usb_register_controller(hc);

	/* Setup Ports */
	for (i = 0; i < (int)controller->ports; i++)
	{
		/* Check ports in with usb controller */
		if (controller->power_mode == X86_OHCI_POWER_PORT_CONTROLLED)
			controller->registers->HcRhPortStatus[i] = X86_OHCI_PORT_CONNECTED | X86_OHCI_PORT_ENABLED | X86_OHCI_PORT_RESET | X86_OHCI_PORT_POWER_ENABLE;
		else
			controller->registers->HcRhPortStatus[i] = X86_OHCI_PORT_CONNECTED | X86_OHCI_PORT_ENABLED | X86_OHCI_PORT_RESET;

		clock_stall(50);
	}
}

/* Reset Controller */
void ohci_reset(ohci_controller_t *controller)
{
	uint32_t temp_value;

	/* Disable All Interrupts */
	controller->registers->HcInterruptDisable = (uint32_t)X86_OHCI_INTR_MASTER_INTR;

	/* Okiiii, reset controller, we need to save FmInterval */
	temp_value = controller->registers->HcFmInterval;

	/* Set bit 0 to request reboot */
	controller->registers->HcCommandStatus |= X86_OHCI_CMD_RESETCTRL;

	/* Wait for reboot (takes maximum of 10 ms) */
	clock_stall_noint(100);

	/* Now restore FmInterval */
	controller->registers->HcFmInterval = temp_value;
	ohci_toggle_frt(controller);

	/* Controller is now in usb_suspend state (84 page)
	* if we stay in this state for more than 2 ms
	* usb resume state must be entered to resume usb operations */
	if ((controller->registers->HcControl & X86_OHCI_CTRL_FSTATE_BITS) == X86_OHCI_CTRL_USB_SUSPEND)
	{
		ohci_set_mode(controller, X86_OHCI_CTRL_USB_RESUME);
		clock_stall(500);
	}

	/* Setup initial ED points */
	controller->registers->HcControlHeadED =
		controller->registers->HcControlCurrentED = memory_getmap(NULL, (virtaddr_t)controller->ed_pool[X86_OHCI_POOL_ED_CONTROL_START]);
	controller->registers->HcBulkHeadED =
		controller->registers->HcBulkCurrentED = memory_getmap(NULL, (virtaddr_t)controller->ed_pool[X86_OHCI_POOL_ED_BULK_START]);

	/* Initial values for frame */
	controller->hcca->current_frame = 0;
	controller->hcca->head_done = 0;

	/* Set HcHCCA to phys address of HCCA */
	controller->registers->HcHCCA = controller->hcca_space;

	/* Set HcEnableInterrupt to all except SOF */
	controller->registers->HcInterruptDisable = (uint32_t)(X86_OHCI_INTR_DISABLE_SOF | X86_OHCI_INTR_MASTER_INTR);
	controller->registers->HcInterruptStatus = ~(uint32_t)0;
	controller->registers->HcInterruptEnable = X86_OHCI_INTR_ENABLE_ALL;

	/* Disable queues for now */
	controller->registers->HcControl &= ~X86_OHCI_CTRL_ALL_LISTS;
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

	/* Set Control Bulk Ratio */
	controller->registers->HcControl |= X86_OHCI_CTRL_SRATIO_BITS;

	/* Start controller by setting it to UsbOperational
	* and get port count from (DescriptorA & 0x7F) */
	ohci_set_mode(controller, X86_OHCI_CTRL_USB_WORKING);

	/* Turn on power for ports! */
	controller->registers->HcRhStatus |= X86_OHCI_STATUS_POWER_ON;

	/* Make sure root hub is not set as compound device */
	controller->registers->HcRhDescriptorA &= ~X86_OHCI_DESCA_DEVICE_TYPE;
	controller->registers->HcRhDescriptorB = 0;
}

/* Interrupt Handler 
 * Make sure that this controller actually made the interrupt 
 * as this interrupt will be shared with other OHCI's */
void ohci_interrupt_handler(void *data)
{
	uint32_t temp_value = 0;
	ohci_controller_t *controller = (ohci_controller_t*)data;

	/* Was it this controller that made the interrupt? 
	 * We only want the interrupts we have set as enabled */
	temp_value = controller->registers->HcInterruptStatus & controller->registers->HcInterruptEnable;

	if (temp_value == 0)
		return;

	/* Disable Interrupts */
	controller->registers->HcInterruptDisable = (uint32_t)X86_OHCI_INTR_MASTER_INTR;

	/* Fatal Error? */
	if (temp_value & X86_OHCI_INTR_FATAL_ERROR)
	{
		printf("OHCI %u: Fatal Error, resetting...\n", controller->id);
		ohci_reset(controller);
		return;
	}

	/* Flag for end of frame type interrupts */
	if (temp_value & (X86_OHCI_INTR_SCHEDULING_OVRRN | X86_OHCI_INTR_HEAD_DONE | X86_OHCI_INTR_SOF | X86_OHCI_INTR_FRAME_OVERFLOW))
		temp_value |= X86_OHCI_INTR_MASTER_INTR;

	/* Scheduling Overrun? */
	if (temp_value & X86_OHCI_INTR_SCHEDULING_OVRRN)
	{
		printf("OHCI %u: Scheduling Overrun\n", controller->id);

		/* Acknowledge Interrupt */
		controller->registers->HcInterruptStatus = X86_OHCI_INTR_SCHEDULING_OVRRN;
		temp_value &= ~X86_OHCI_INTR_SCHEDULING_OVRRN;
	}	
	
	/* Resume Detection? */
	if (temp_value & X86_OHCI_INTR_RESUME_DETECT)
	{
		printf("OHCI %u: Resume Detected\n", controller->id);

		/* We must wait 20 ms before putting controller to Operational */
		clock_stall_noint(200);
		ohci_set_mode(controller, X86_OHCI_CTRL_USB_WORKING);

		/* Acknowledge Interrupt */
		controller->registers->HcInterruptStatus = X86_OHCI_INTR_RESUME_DETECT;
		temp_value &= ~X86_OHCI_INTR_RESUME_DETECT;
	}
		
	/* Frame Overflow 
	 * Happens when it rolls over from 0xFFFF to 0 */
	if (temp_value & X86_OHCI_INTR_FRAME_OVERFLOW)
	{
		//printf("OHCI %u: Frame Overflow (%u)\n", controller->id, controller->registers->HcFmNumber);

		/* Acknowledge Interrupt */
		controller->registers->HcInterruptStatus = X86_OHCI_INTR_FRAME_OVERFLOW;
		temp_value &= ~X86_OHCI_INTR_FRAME_OVERFLOW;
	}

	/* Why yes, yes it was, wake up the TD handler thread
	* if it was head_done_writeback */
	if (temp_value & X86_OHCI_INTR_HEAD_DONE)
	{
		/* Wuhu, handle this! */
		uint32_t ed_address = controller->hcca->head_done;
		scheduler_wakeup_one((addr_t*)ed_address);

		/* Acknowledge Interrupt */
		controller->hcca->head_done = 0;
		controller->registers->HcInterruptStatus = X86_OHCI_INTR_HEAD_DONE;
		temp_value &= ~X86_OHCI_INTR_HEAD_DONE;
	}

	/* Root Hub Status Change 
	 * Do a port status check */
	if (temp_value & X86_OHCI_INTR_ROOT_HUB_EVENT)
	{
		ohci_ports_check(controller);

		/* Acknowledge Interrupt */
		controller->registers->HcInterruptStatus = X86_OHCI_INTR_ROOT_HUB_EVENT;
		temp_value &= ~X86_OHCI_INTR_ROOT_HUB_EVENT;
	}
	
	/* Mask out remaining interrupts, we dont use them */
	if (temp_value & ~X86_OHCI_INTR_MASTER_INTR)
		controller->registers->HcInterruptDisable = temp_value;

	/* Enable Interrupts */
	controller->registers->HcInterruptEnable = (uint32_t)X86_OHCI_INTR_MASTER_INTR;

	/* Send EOI */
	apic_send_eoi();
}

/* ED Functions */
void ohci_ep_init(ohci_endpoint_desc_t *ep, addr_t first_td, 
	uint32_t address, uint32_t endpoint, uint32_t packet_size, uint32_t lowspeed)
{
	/* Setup Flags 
	 * HighSpeed Bulk/Control/Interrupt */
	ep->flags = 0;
	ep->flags |= (address & X86_OHCI_EP_ADDR_BITS);
	ep->flags |= X86_OHCI_EP_EP_NUM((endpoint & X86_OHCI_EP_EP_NUM_BITS));
	ep->flags |= X86_OHCI_EP_LOWSPEED(lowspeed);
	ep->flags |= X86_OHCI_EP_PID_TD; /* Get PID from TD */
	ep->flags |= X86_OHCI_EP_PACKET_SIZE((packet_size & X86_OHCI_EP_PACKET_BITS));

	/* Set TD */
	if (first_td == X86_OHCI_TRANSFER_END_OF_LIST)
		ep->head_ptr = X86_OHCI_TRANSFER_END_OF_LIST;
	else
		ep->head_ptr = memory_getmap(NULL, (first_td & ~0xD));
}

/* TD Functions */
ohci_gtransfer_desc_t *ohci_td_setup(ohci_controller_t *controller, uint32_t type, 
	ohci_endpoint_desc_t *ed, addr_t next_td, uint32_t toggle, uint8_t request_direction,
	uint8_t request_type, uint8_t request_value_low, uint8_t request_value_high, uint16_t request_index,
	uint16_t request_length, void **td_buffer)
{
	usb_packet_t *packet;
	ohci_gtransfer_desc_t *td;
	addr_t td_phys;
	void *buffer;

	/* Validate Indices */
	ohci_validate_indices(controller);

	/* Grab a TD and a buffer */
	if (type == X86_USB_REQUEST_TYPE_CONTROL)
	{
		td = controller->td_pool[controller->td_index_control];
		buffer = controller->td_pool_buffers[controller->td_index_control];
		td_phys = controller->td_pool_phys[controller->td_index_control];
		controller->td_index_control++;
	}
	else
	{
		td = controller->td_pool[controller->td_index_bulk];
		buffer = controller->td_pool_buffers[controller->td_index_bulk];
		td_phys = controller->td_pool_phys[controller->td_index_bulk];
		controller->td_index_bulk++;
	}
	
	/* EOL ? */
	if (next_td == X86_OHCI_TRANSFER_END_OF_LIST)
		td->next_td = X86_OHCI_TRANSFER_END_OF_LIST;
	else	/* Get physical address of next_td and set next_td to that */
		td->next_td = memory_getmap(NULL, (virtaddr_t)next_td); 

	/* Setup the TD for a SETUP TD */
	td->flags = 0;
	td->flags |= X86_OHCI_TRANSFER_BUF_ROUNDING;
	td->flags |= X86_OHCI_TRANSFER_BUF_PID_SETUP;
	td->flags |= X86_OHCI_TRANSFER_BUF_NO_INTERRUPT;	/* We don't want interrupt */
	td->flags |= (toggle << 24);
	td->flags |= X86_OHCI_TRANSFER_BUF_TD_TOGGLE;
	td->flags |= X86_OHCI_TRANSFER_BUF_NOCC;

	/* Setup the SETUP Request */
	*td_buffer = buffer;
	packet = (usb_packet_t*)buffer;
	packet->direction = request_direction;
	packet->type = request_type;
	packet->value_low = request_value_low;
	packet->value_high = request_value_high;
	packet->index = request_index;
	packet->length = request_length;

	/* Set TD buffer */
	td->buffer_end = td->cbp + sizeof(usb_packet_t) - 1;

	/* Make Queue Tail point to this */
	ed->tail_ptr = td_phys;

	return td;
}

ohci_gtransfer_desc_t *ohci_td_io(ohci_controller_t *controller, uint32_t type,
	ohci_endpoint_desc_t *ed, addr_t next_td, uint32_t toggle, uint32_t pid, 
	uint32_t length, void **td_buffer)
{
	ohci_gtransfer_desc_t *td;
	addr_t td_phys;
	void *buffer;

	/* Validate Indices */
	ohci_validate_indices(controller);

	/* Grab a TD and a buffer */
	if (type == X86_USB_REQUEST_TYPE_CONTROL)
	{
		td = controller->td_pool[controller->td_index_control];
		buffer = controller->td_pool_buffers[controller->td_index_control];
		td_phys = controller->td_pool_phys[controller->td_index_control];
		controller->td_index_control++;
	}
	else
	{
		td = controller->td_pool[controller->td_index_bulk];
		buffer = controller->td_pool_buffers[controller->td_index_bulk];
		td_phys = controller->td_pool_phys[controller->td_index_bulk];
		controller->td_index_bulk++;
	}

	/* EOL ? */
	if (next_td == X86_OHCI_TRANSFER_END_OF_LIST)
		td->next_td = X86_OHCI_TRANSFER_END_OF_LIST;
	else	/* Get physical address of next_td and set next_td to that */
		td->next_td = memory_getmap(NULL, (virtaddr_t)next_td);

	/* Setup the TD for a IO TD */
	td->flags = 0;
	td->flags |= X86_OHCI_TRANSFER_BUF_ROUNDING;
	td->flags |= pid;
	td->flags |= X86_OHCI_TRANSFER_BUF_NO_INTERRUPT;	/* We don't want interrupt */
	td->flags |= X86_OHCI_TRANSFER_BUF_TD_TOGGLE;
	td->flags |= X86_OHCI_TRANSFER_BUF_NOCC;
	td->flags |= (toggle << 24);

	*td_buffer = buffer;
	
	/* Bytes to transfer?? */
	if (length > 0)
	{
		td->cbp = memory_getmap(NULL, (virtaddr_t)buffer);
		td->buffer_end = td->cbp + length - 1;
	}
	else
	{
		td->cbp = 0;
		td->buffer_end = td->cbp;
	}
		
	
	/* Make Queue Tail point to this */
	ed->tail_ptr = td_phys;

	return td;
}

/* Transaction Functions */

/* This one prepaires an ED */
void ohci_transaction_init(void *controller, usb_hc_request_t *request)
{
	ohci_controller_t *ctrl = (ohci_controller_t*)controller;

	/* Disable BULK and CONTROL queues */
	ctrl->registers->HcControl &= ~(X86_OHCI_CTRL_CONTROL_LIST | X86_OHCI_CTRL_BULK_LIST); 

	/* Tell Command Status we dont have the list filled */
	ctrl->registers->HcCommandStatus &= ~(X86_OHCI_CMD_TDACTIVE_CTRL | X86_OHCI_CMD_TDACTIVE_BULK);
		
	/* Validate TD/ED Indices */
	ohci_validate_indices(ctrl);

	/* Grab an ED and increase index */
	if (request->type == X86_USB_REQUEST_TYPE_CONTROL)
	{
		request->data = (void*)ctrl->ed_pool[ctrl->ed_index_control];
		ctrl->ed_index_control++;
	}
	else
	{
		request->data = (void*)ctrl->ed_pool[ctrl->ed_index_bulk];
		ctrl->ed_index_bulk++;
	}

	/* Set as not completed for start */
	request->completed = 0;
}

/* This one prepaires an setup TD */
usb_hc_transaction_t *ohci_transaction_setup(void *controller, usb_hc_request_t *request)
{
	ohci_controller_t *ctrl = (ohci_controller_t*)controller;
	usb_hc_transaction_t *transaction;

	/* Allocate transaction */
	transaction = (usb_hc_transaction_t*)kmalloc(sizeof(usb_hc_transaction_t));
	transaction->io_buffer = 0;
	transaction->io_length = 0;
	transaction->link = NULL;

	/* Create the td */
	transaction->transfer_descriptor = (void*)ohci_td_setup(ctrl, request->type, 
		(ohci_endpoint_desc_t*)request->data, X86_OHCI_TRANSFER_END_OF_LIST, 
		request->toggle, request->packet.direction, request->packet.type, 
		request->packet.value_low, request->packet.value_high, request->packet.index, 
		request->packet.length, &transaction->transfer_buffer);

	/* If previous transaction */
	if (request->transactions != NULL)
	{
		ohci_gtransfer_desc_t *prev_td;
		usb_hc_transaction_t *ctrans = request->transactions;

		while (ctrans->link)
			ctrans = ctrans->link;
		
		prev_td = (ohci_gtransfer_desc_t*)ctrans->transfer_descriptor;
		prev_td->next_td = memory_getmap(NULL, (virtaddr_t)transaction->transfer_descriptor);
	}

	return transaction;
}

/* This one prepaires an in TD */
usb_hc_transaction_t *ohci_transaction_in(void *controller, usb_hc_request_t *request)
{
	ohci_controller_t *ctrl = (ohci_controller_t*)controller;
	usb_hc_transaction_t *transaction;

	/* Allocate transaction */
	transaction = (usb_hc_transaction_t*)kmalloc(sizeof(usb_hc_transaction_t));
	transaction->io_buffer = request->io_buffer;
	transaction->io_length = request->io_length;
	transaction->link = NULL;

	/* Setup TD */
	transaction->transfer_descriptor = (void*)ohci_td_io(ctrl, request->type,
		(ohci_endpoint_desc_t*)request->data, X86_OHCI_TRANSFER_END_OF_LIST, 
		request->toggle, X86_OHCI_TRANSFER_BUF_PID_IN, request->io_length, 
		&transaction->transfer_buffer);

	/* If previous transaction */
	if (request->transactions != NULL)
	{
		ohci_gtransfer_desc_t *prev_td;
		usb_hc_transaction_t *ctrans = request->transactions;

		while (ctrans->link)
			ctrans = ctrans->link;

		prev_td = (ohci_gtransfer_desc_t*)ctrans->transfer_descriptor;
		prev_td->next_td = memory_getmap(NULL, (virtaddr_t)transaction->transfer_descriptor);
	}

	return transaction;
}

/* This one prepaires an out TD */
usb_hc_transaction_t *ohci_transaction_out(void *controller, usb_hc_request_t *request)
{
	ohci_controller_t *ctrl = (ohci_controller_t*)controller;
	usb_hc_transaction_t *transaction;

	/* Allocate transaction */
	transaction = (usb_hc_transaction_t*)kmalloc(sizeof(usb_hc_transaction_t));
	transaction->io_buffer = 0;
	transaction->io_length = 0;
	transaction->link = NULL;

	/* Setup TD */
	transaction->transfer_descriptor = (void*)ohci_td_io(ctrl, request->type,
		(ohci_endpoint_desc_t*)request->data, X86_OHCI_TRANSFER_END_OF_LIST,
		request->toggle, X86_OHCI_TRANSFER_BUF_PID_OUT, request->io_length,
		&transaction->transfer_buffer);

	/* Copy Data */
	if (request->io_buffer != NULL && request->io_length != 0)
		memcpy(transaction->transfer_buffer, request->io_buffer, request->io_length);

	/* If previous transaction */
	if (request->transactions != NULL)
	{
		ohci_gtransfer_desc_t *prev_td;
		usb_hc_transaction_t *ctrans = request->transactions;

		while (ctrans->link)
			ctrans = ctrans->link;

		prev_td = (ohci_gtransfer_desc_t*)ctrans->transfer_descriptor;
		prev_td->next_td = memory_getmap(NULL, (virtaddr_t)transaction->transfer_descriptor);
	}

	return transaction;
}

/* This one queues the transaction up for processing */
void ohci_transaction_send(void *controller, usb_hc_request_t *request)
{
	/* Debug Time */
	usb_hc_transaction_t *transaction = request->transactions;
	ohci_controller_t *ctrl = (ohci_controller_t*)controller;
	int i, completed = 1;
	ohci_gtransfer_desc_t *td = NULL;
	uint32_t condition_code;
	addr_t ed_address, ed_interrupt = 0;

	/* Get physical */
	ed_address = memory_getmap(NULL, (virtaddr_t)request->data);

	/* Set as not completed for start */
	request->completed = 0;

	/* Add dummy TD to end */
	usb_transaction_out(usb_get_hcd(ctrl->hcd_id), request, 1, 0, 0);

	/* Setup an ED for this */
	ohci_ep_init(request->data, (addr_t)request->transactions->transfer_descriptor,
		request->device->address, request->endpoint, request->length, request->lowspeed);

	/* Set last TD to produce an interrupt (not dummy) */
	transaction = request->transactions;
	while (transaction->link)
	{
		/* Check if last before dummy */
		if (transaction->link->link == NULL)
		{
			/* Set TD to produce interrupt */
			td = (ohci_gtransfer_desc_t*)transaction->transfer_descriptor;
			td->flags &= ~X86_OHCI_TRANSFER_BUF_NO_INTERRUPT;
			ed_interrupt = memory_getmap(NULL, (virtaddr_t)td);
			break;
		}

		transaction = transaction->link;
	}

	/* Add it HcControl/BulkCurrentED */
	if (request->type == X86_USB_REQUEST_TYPE_CONTROL)
		ctrl->registers->HcControlCurrentED = ed_address;
	else
		ctrl->registers->HcBulkCurrentED = ed_address;

	/* Now lets try the transaction */
	for (i = 0; i < 3; i++)
	{
		/* Set false */
		completed = 1;

		/* Set Lists Filled (Enable Them) */
		ctrl->registers->HcCommandStatus |= (X86_OHCI_CMD_TDACTIVE_CTRL | X86_OHCI_CMD_TDACTIVE_BULK);
		((ohci_endpoint_desc_t*)request->data)->head_ptr &= ~0x1;
		ctrl->registers->HcControl |= (X86_OHCI_CTRL_CONTROL_LIST | X86_OHCI_CTRL_BULK_LIST);

		/* Wait for interrupt */
		scheduler_sleep_thread((addr_t*)ed_interrupt);
		_yield();

		/* Check Conditions (WithOUT dummy) */
		transaction = request->transactions;
		while (transaction->link)
		{
			td = (ohci_gtransfer_desc_t*)transaction->transfer_descriptor;
			condition_code = (td->flags & 0xF0000000) >> 28;
			//printf("TD Flags 0x%x, TD Condition Code %u (%s)\n", td->flags, condition_code, ohci_err_msgs[condition_code]);

			if (condition_code == 0 && completed == 1)
				completed = 1;
			else
				completed = 0;

			transaction = transaction->link;
		}

		/* Did we do it?! */
		if (completed == 1)
			break;
	}

	/* Lets see... */
	if (completed)
	{
		/* Build Buffer */
		transaction = request->transactions;

		while (transaction->link)
		{
			/* Copy Data? */
			if (transaction->io_buffer != NULL && transaction->io_length != 0)
			{
				//printf("Buffer Copy 0x%x, Length 0x%x\n", transaction->io_buffer, transaction->io_length);
				memcpy(transaction->io_buffer, transaction->transfer_buffer, transaction->io_length);
			}
			
			/* Next Link */
			transaction = transaction->link;
		}

		/* Set as completed */
		request->completed = 1;
	}
		
}