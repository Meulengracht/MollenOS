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
* Todo:
* Isochronous Support
* Stability (Only tested on emulators and one real hardware pc).
* Multiple Transfers per interrupt (should be easy)
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
volatile uint32_t glb_ohci_id = 0;

/* Externs */
extern void clock_stall_noint(uint32_t ms);
extern void _yield(void);

/* Prototypes */
void ohci_interrupt_handler(void *data);
void ohci_reset(ohci_controller_t *controller);
void ohci_setup(ohci_controller_t *controller);

uint32_t ohci_allocate_td(ohci_controller_t *controller);

void ohci_transaction_init(void *controller, usb_hc_request_t *request);
usb_hc_transaction_t *ohci_transaction_setup(void *controller, usb_hc_request_t *request);
usb_hc_transaction_t *ohci_transaction_in(void *controller, usb_hc_request_t *request);
usb_hc_transaction_t *ohci_transaction_out(void *controller, usb_hc_request_t *request);
void ohci_transaction_send(void *controller, usb_hc_request_t *request);

void ohci_install_interrupt(void *controller, usb_hc_device_t *device, usb_hc_endpoint_t *endpoint,
	void *in_buffer, size_t in_bytes, void(*callback)(void*, size_t), void *arg);

/* Error Codes */
static const int _balance[] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
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
	uint32_t val = controller->registers->HcControl;
	val = (val & ~X86_OHCI_CTRL_USB_SUSPEND);
	val |= mode;
	controller->registers->HcControl = val;
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

/* Stop/Start */
void ohci_stop(ohci_controller_t *controller)
{
	uint32_t temp;

	/* Disable BULK and CONTROL queues */
	temp = controller->registers->HcControl;
	temp = (temp & ~0x00000030);
	controller->registers->HcControl = temp;

	/* Tell Command Status we dont have the list filled */
	temp = controller->registers->HcCommandStatus;
	temp = (temp & ~0x00000006);
	controller->registers->HcCommandStatus = temp;
}

/* This resets a port, this is only ever
* called from an interrupt and thus we can't use clock_stall :/ */
void ohci_port_reset(ohci_controller_t *controller, uint32_t port, int noint)
{
	int i = 0;
	uint32_t temp;

	/* Set reset */
	if (noint)
		controller->registers->HcRhPortStatus[port] = (X86_OHCI_PORT_RESET | X86_OHCI_PORT_CONNECT_EVENT);
	else
		controller->registers->HcRhPortStatus[port] = (X86_OHCI_PORT_RESET);

	/* Wait with timeout */
	temp = controller->registers->HcRhPortStatus[port];
	while ((temp & X86_OHCI_PORT_RESET)
		&& (i < 1000))
	{
		/* Increase timeout */
		i++;

		/* Stall */
		clock_stall(5);

		/* Update */
		temp = controller->registers->HcRhPortStatus[port];
	}

	/* Clear Reset Event */
	if (noint)
		controller->registers->HcRhPortStatus[port] = X86_OHCI_PORT_RESET_EVENT;

	/* Set Enable */
	if (!(controller->registers->HcRhPortStatus[port] & X86_OHCI_PORT_ENABLED))
	{
		if (controller->power_mode == X86_OHCI_POWER_PORT_CONTROLLED)
			controller->registers->HcRhPortStatus[port] = X86_OHCI_PORT_ENABLED | X86_OHCI_PORT_POWER_ENABLE;
		else
			controller->registers->HcRhPortStatus[port] = X86_OHCI_PORT_ENABLED;
	}

	/* Stall */
	clock_stall(100);
}

/* Callbacks */
void ohci_port_status(void *ctrl_data, usb_hc_port_t *port)
{
	ohci_controller_t *controller = (ohci_controller_t*)ctrl_data;
	uint32_t status;

	/* Reset Port */
	ohci_port_reset(controller, port->id, 1);

	/* Update information in port */
	status = controller->registers->HcRhPortStatus[port->id];

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

	/* Clear Connect Event */
	if (controller->registers->HcRhPortStatus[port->id] & X86_OHCI_PORT_CONNECT_EVENT)
		controller->registers->HcRhPortStatus[port->id] = X86_OHCI_PORT_CONNECT_EVENT;

	/* If Enable Event bit is set, clear it */
	if (controller->registers->HcRhPortStatus[port->id] & X86_OHCI_PORT_ENABLE_EVENT)
		controller->registers->HcRhPortStatus[port->id] = X86_OHCI_PORT_ENABLE_EVENT;

	/* If Suspend Event is set, clear it */
	if (controller->registers->HcRhPortStatus[port->id] & X86_OHCI_PORT_SUSPEND_EVENT)
		controller->registers->HcRhPortStatus[port->id] = X86_OHCI_PORT_SUSPEND_EVENT;

	/* If Over Current Event is set, clear it */
	if (controller->registers->HcRhPortStatus[port->id] & X86_OHCI_PORT_OVR_CURRENT_EVENT)
		controller->registers->HcRhPortStatus[port->id] = X86_OHCI_PORT_OVR_CURRENT_EVENT;

	/* If reset bit is set, clear it */
	if (controller->registers->HcRhPortStatus[port->id] & X86_OHCI_PORT_RESET_EVENT)
		controller->registers->HcRhPortStatus[port->id] = X86_OHCI_PORT_RESET_EVENT;

	printf("OHCI: Port Status %u: 0x%x\n", port->id, status);
}

/* Port Functions */
void ohci_port_check(ohci_controller_t *controller, uint32_t port)
{
	usb_hc_t *hc;

	/* Was it connect event and not disconnect ? */
	if (controller->registers->HcRhPortStatus[port] & X86_OHCI_PORT_CONNECT_EVENT)
	{
		if (controller->registers->HcRhPortStatus[port] & X86_OHCI_PORT_CONNECTED)
		{
			/* Reset on Attach */
			ohci_port_reset(controller, port, 0);
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
void ohci_init(pci_driver_t *device)
{
	uint16_t pci_command;
	ohci_controller_t *controller = NULL;
	
	/* Allocate Resources for this controller */
	controller = (ohci_controller_t*)kmalloc(sizeof(ohci_controller_t));
	controller->pci_info = device;
	controller->id = glb_ohci_id;

	/* Enable memory and bus mastering */
	pci_command = pci_read_word((const uint16_t)device->bus, (const uint16_t)device->device, (const uint16_t)device->function, 0x4);
	pci_write_word((const uint16_t)device->bus, (const uint16_t)device->device, (const uint16_t)device->function, 0x4, pci_command | 0x6);

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
	spinlock_reset(&controller->lock);

	/* Memset HCCA Space */
	memset((void*)controller->hcca, 0, 0x1000);

	/* Install IRQ Handler */
	interrupt_install_pci(device, ohci_interrupt_handler, controller);

	/* Debug */
	printf("OHCI - Id %u, bar0: 0x%x (0x%x), dma: 0x%x\n", 
		controller->id, controller->control_space,
		(addr_t)controller->registers, controller->hcca_space);

	/* Reset Controller */
	ohci_setup(controller);
}

/* Initializes Controller Queues */
void ohci_init_queues(ohci_controller_t *controller)
{
	addr_t buffer_address = 0, buffer_address_max = 0;
	addr_t pool = 0, pool_phys = 0;
	addr_t ed_level;
	int i;

	/* Initialise ED Pool */
	controller->ed_index = 0;
	for (i = 0; i < X86_OHCI_POOL_NUM_ED; i++)
	{
		addr_t a_space = (addr_t)kmalloc(sizeof(ohci_endpoint_desc_t) + X86_OHCI_STRUCT_ALIGN);
		controller->ed_pool[i] = (ohci_endpoint_desc_t*)ohci_align(a_space, X86_OHCI_STRUCT_ALIGN_BITS, X86_OHCI_STRUCT_ALIGN);
		memset((void*)controller->ed_pool[i], 0, sizeof(ohci_endpoint_desc_t));
		controller->ed_pool[i]->next_ed = 0;
		controller->ed_pool[i]->flags = X86_OHCI_EP_SKIP;
	}

	/* Initialise Bulk/Control TD Pool & Buffers */
	controller->td_index = 0;
	buffer_address = (addr_t)kmalloc_a(0x1000);
	buffer_address_max = buffer_address + 0x1000 - 1;

	pool = (addr_t)kmalloc((sizeof(ohci_gtransfer_desc_t) * X86_OHCI_POOL_NUM_TD) + X86_OHCI_STRUCT_ALIGN);
	pool = ohci_align(pool, X86_OHCI_STRUCT_ALIGN_BITS, X86_OHCI_STRUCT_ALIGN);
	pool_phys = memory_getmap(NULL, pool);
	memset((void*)pool, 0, sizeof(ohci_gtransfer_desc_t) * X86_OHCI_POOL_NUM_TD);
	for (i = 0; i < X86_OHCI_POOL_NUM_TD; i++)
	{
		/* Set */
		controller->td_pool[i] = (ohci_gtransfer_desc_t*)pool;
		controller->td_pool_phys[i] = pool_phys;

		/* Allocate another page? */
		if (buffer_address > buffer_address_max)
		{
			buffer_address = (addr_t)kmalloc_a(0x1000);
			buffer_address_max = buffer_address + 0x1000 - 1;
		}

		/* Setup buffer */
		controller->td_pool_buffers[i] = (addr_t*)buffer_address;
		controller->td_pool[i]->cbp = memory_getmap(NULL, buffer_address);

		/* Increase */
		pool += sizeof(ohci_gtransfer_desc_t);
		pool_phys += sizeof(ohci_gtransfer_desc_t);
		buffer_address += 0x200;
	}

	/* Setup Interrupt Table 
	 * We simply use the DMA
	 * allocation */
	controller->itable = (ohci_interrupt_table_t*)(controller->hcca_space + 512);

	/* Setup first level */
	ed_level = controller->hcca_space + 512;
	ed_level += 16 * sizeof(ohci_endpoint_desc_t);
	for (i = 0; i < 16; i++)
	{
		controller->itable->ms16[i].next_ed = ed_level + ((i / 2) * sizeof(ohci_endpoint_desc_t));
		controller->itable->ms16[i].next_ed_virt = ed_level + ((i / 2) * sizeof(ohci_endpoint_desc_t));
		controller->itable->ms16[i].flags = X86_OHCI_EP_SKIP;
	}

	/* Second level (8 ms) */
	ed_level += 8 * sizeof(ohci_endpoint_desc_t);
	for (i = 0; i < 8; i++)
	{
		controller->itable->ms8[i].next_ed = ed_level + ((i / 2) * sizeof(ohci_endpoint_desc_t));
		controller->itable->ms8[i].next_ed_virt = ed_level + ((i / 2) * sizeof(ohci_endpoint_desc_t));
		controller->itable->ms8[i].flags = X86_OHCI_EP_SKIP;
	}

	/* Third level (4 ms) */
	ed_level += 4 * sizeof(ohci_endpoint_desc_t);
	for (i = 0; i < 4; i++)
	{
		controller->itable->ms4[i].next_ed = ed_level + ((i / 2) * sizeof(ohci_endpoint_desc_t));
		controller->itable->ms4[i].next_ed_virt = ed_level + ((i / 2) * sizeof(ohci_endpoint_desc_t));
		controller->itable->ms4[i].flags = X86_OHCI_EP_SKIP;
	}

	/* Fourth level (2 ms) */
	ed_level += 2 * sizeof(ohci_endpoint_desc_t);
	for (i = 0; i < 2; i++)
	{
		controller->itable->ms2[i].next_ed = ed_level + sizeof(ohci_endpoint_desc_t);
		controller->itable->ms2[i].next_ed_virt = ed_level + sizeof(ohci_endpoint_desc_t);
		controller->itable->ms2[i].flags = X86_OHCI_EP_SKIP;
	}

	/* Last level (1 ms) */
	controller->itable->ms1[0].next_ed = 0;
	controller->itable->ms1[0].next_ed_virt = 0;
	controller->itable->ms1[0].flags = X86_OHCI_EP_SKIP;

	/* Setup HCCA */
	for (i = 0; i < 32; i++)
	{
		/* 0 -> 0     16 -> 0
		 * 1 -> 8     17 -> 8
		 * 2 -> 4     18 -> 4
		 * 3 -> 12    19 -> 12
		 * 4 -> 2     20 -> 2
		 * 5 -> 10    21 -> 10
		 * 6 -> 6     22 -> 6
		 * 7 -> 14    23 -> 14
		 * 8 -> 1     24 -> 1
		 * 9 -> 9     25 -> 9
		 * 10 -> 5    26 -> 5
		 * 11 -> 13   27 -> 13
		 * 12 -> 3    28 -> 3
		 * 13 -> 11   29 -> 11
		 * 14 -> 7    30 -> 7
		 * 15 -> 15   31 -> 15 */
		controller->ed32[i] = (ohci_endpoint_desc_t*)
			((controller->hcca_space + 512) + (_balance[i & 0xF] * sizeof(ohci_endpoint_desc_t)));
		controller->hcca->interrupt_table[i] = 
			((controller->hcca_space + 512) + (_balance[i & 0xF] * sizeof(ohci_endpoint_desc_t)));
		
		/* This gives us the tree 
		 * This means our 16 first ED's in the itable are the buttom of the tree 
		 *   0          1         2         3        4         5         6        7        8        9        10        11        12        13        15       
		 *  / \        / \       / \       / \      / \       / \       / \      / \      / \      / \       / \       / \       / \       / \       / \ 
		 * 0  16      8  24     4  20     12 28    2  18     10 26     6  22    14 30    1  17    9  25     5  21     13 29     3  19     7  23     15 31
		 */

	}

	/* Load Balancing */
	controller->i32 = 0;
	controller->i16 = 0;
	controller->i8 = 0;
	controller->i4 = 0;
	controller->i2 = 0;

	/* Allocate a transaction list */
	controller->transactions_waiting_bulk = 0;
	controller->transactions_waiting_control = 0;
	controller->transaction_queue_bulk = 0;
	controller->transaction_queue_control = 0;
	controller->transactions_list = list_create(LIST_SAFE);
}

/* Resets the controllre to a working state from initial */
void ohci_setup(ohci_controller_t *controller)
{
	usb_hc_t *hc;
	uint32_t temp_value = 0, temp = 0, fmint = 0;
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

	/* Step 2. Init Virtual Queues */
	ohci_init_queues(controller);

	/* Step 3. Gain control of controller */

	/* Is SMM the bitch? */
	if (controller->registers->HcControl & X86_OHCI_CTRL_INT_ROUTING)
	{
		/* Ok, SMM has control, now give me my hat back */
		temp = controller->registers->HcCommandStatus;
		temp |= X86_OHCI_CMD_OWNERSHIP;
		controller->registers->HcCommandStatus = temp;

		/* Wait for InterruptRouting to clear */
		i = 0;
		while ((i < 500) 
			&& (controller->registers->HcControl & X86_OHCI_CTRL_INT_ROUTING))
		{
			/* Idle idle idle */
			clock_stall(10);

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
			ohci_set_mode(controller, X86_OHCI_CTRL_USB_WORKING);

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

	/* Disable All Interrupts */
	controller->registers->HcInterruptDisable = (uint32_t)X86_OHCI_INTR_MASTER_INTR;

	/* Perform a reset of HC Controller */
	ohci_set_mode(controller, X86_OHCI_CTRL_USB_SUSPEND);
	clock_stall(200);

	/* Okiiii, reset controller, we need to save FmInterval */
	fmint = controller->registers->HcFmInterval;

	/* Set bit 0 to request reboot */
	temp = controller->registers->HcCommandStatus;
	temp |= X86_OHCI_CMD_RESETCTRL;
	controller->registers->HcCommandStatus = temp;

	/* Wait for reboot (takes maximum of 10 ms) */
	i = 0;
	while ((i < 500) && controller->registers->HcCommandStatus & X86_OHCI_CMD_RESETCTRL)
	{
		clock_stall(1);
		i++;
	}

	/* Sanity */
	if (i == 500)
	{
		printf("OHCI: Reset Timeout :(\n");
		return;
	}

	/**************************************/
	/* We now have 2 ms to complete setup */
	/**************************************/

	/* Set HcHCCA to phys address of HCCA */
	controller->registers->HcHCCA = controller->hcca_space;

	/* Initial values for frame */
	controller->hcca->current_frame = 0;
	controller->hcca->head_done = 0;

	/* Setup initial ED points */
	controller->registers->HcControlHeadED =
		controller->registers->HcControlCurrentED = 0;
	controller->registers->HcBulkHeadED =
		controller->registers->HcBulkCurrentED = 0;

	/* Set HcEnableInterrupt to all except SOF and OC */
	controller->registers->HcInterruptDisable = (X86_OHCI_INTR_SOF | X86_OHCI_INTR_ROOT_HUB_EVENT | X86_OHCI_INTR_OWNERSHIP_EVENT);
	controller->registers->HcInterruptStatus = ~(uint32_t)0;
	controller->registers->HcInterruptEnable = (X86_OHCI_INTR_SCHEDULING_OVRRN | X86_OHCI_INTR_HEAD_DONE |
		X86_OHCI_INTR_RESUME_DETECT | X86_OHCI_INTR_FATAL_ERROR | X86_OHCI_INTR_FRAME_OVERFLOW | X86_OHCI_INTR_MASTER_INTR);

	/* Set HcPeriodicStart to a value that is 90% of FrameInterval in HcFmInterval */
	temp_value = (controller->registers->HcFmInterval & 0x3FFF);
	controller->registers->HcPeriodicStart = (temp_value / 10) * 9;

	/* Setup Control */
	temp = controller->registers->HcControl;
	if (temp & X86_OHCI_CTRL_REMOTE_WAKE)
		temp |= X86_OHCI_CTRL_REMOTE_WAKE;

	/* Clear Lists, Mode, Ratio and IR */
	temp = (temp & ~(0x0000003C | X86_OHCI_CTRL_USB_SUSPEND | 0x3 | 0x100));

	/* Set Ratio (4:1) and Mode (Operational) */
	temp |= (0x3 | X86_OHCI_CTRL_USB_WORKING);
	controller->registers->HcControl = temp;

	/* Now restore FmInterval */
	controller->registers->HcFmInterval = fmint;

	/* Controller is now running! */
	printf("OHCI: Controller %u Started, Control 0x%x\n",
		controller->id, controller->registers->HcControl);

	/* Check Power Mode */
	if (controller->registers->HcRhDescriptorA & (1 << 9))
	{
		controller->power_mode = X86_OHCI_POWER_ALWAYS_ON;
		controller->registers->HcRhStatus = X86_OHCI_STATUS_POWER_ON;
		controller->registers->HcRhDescriptorB = 0;
	}
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
			controller->registers->HcRhStatus = X86_OHCI_STATUS_POWER_ON;
			controller->power_mode = X86_OHCI_POWER_PORT_GLOBAL;
		}
	}

	/* Get port count from (DescriptorA & 0x7F) */
	controller->ports = controller->registers->HcRhDescriptorA & 0x7F;

	/* Sanity */
	if (controller->ports > 15)
		controller->ports = 15;

	/* Set RhA */
	controller->registers->HcRhDescriptorA &= ~(0x00000000 | X86_OHCI_DESCA_DEVICE_TYPE);

	/* Get Power On Delay 
	 * PowerOnToPowerGoodTime (24 - 31)
	 * This byte specifies the duration HCD has to wait before 
	 * accessing a powered-on port of the Root Hub. 
	 * It is implementation-specific.  The unit of time is 2 ms.  
	 * The duration is calculated as POTPGT * 2 ms.
	 */
	temp_value = controller->registers->HcRhDescriptorA;
	temp_value >>= 24;
	temp_value &= 0x000000FF;
	temp_value *= 2;

	/* Give it atleast 100 ms :p */
	if (temp_value < 100)
		temp_value = 100;

	controller->power_on_delay_ms = temp_value;

	printf("OHCI: Ports %u (power mode %u, power delay %u)\n", 
		controller->ports, controller->power_mode, temp_value);

	/* Setup HCD */
	hc = usb_init_controller((void*)controller, X86_USB_TYPE_OHCI, controller->ports);

	/* Port Functions */
	hc->root_hub_check = ohci_ports_check;
	hc->port_status = ohci_port_status;

	/* Transaction Functions */
	hc->transaction_init = ohci_transaction_init;
	hc->transaction_setup = ohci_transaction_setup;
	hc->transaction_in = ohci_transaction_in;
	hc->transaction_out = ohci_transaction_out;
	hc->transaction_send = ohci_transaction_send;
	hc->install_interrupt = ohci_install_interrupt;

	controller->hcd_id = usb_register_controller(hc);

	/* Setup Ports */
	for (i = 0; i < (int)controller->ports; i++)
	{
		int p = i;

		/* Make sure power is on */
		if (!(controller->registers->HcRhPortStatus[i] & X86_OHCI_PORT_POWER_ENABLE))
		{
			/* Powerup! */
			controller->registers->HcRhPortStatus[i] = X86_OHCI_PORT_POWER_ENABLE;

			/* Wait for power to stabilize */
			clock_stall(controller->power_on_delay_ms);
		}

		/* Check if port is connected */
		if (controller->registers->HcRhPortStatus[i] & X86_OHCI_PORT_CONNECTED)
			usb_event_create(usb_get_hcd(controller->hcd_id), p, X86_USB_EVENT_CONNECTED);
	}

	/* Now we can enable hub events (and clear interrupts) */
	controller->registers->HcInterruptStatus &= ~(uint32_t)0;
	controller->registers->HcInterruptEnable = X86_OHCI_INTR_ROOT_HUB_EVENT;
}

/* Reset Controller */
void ohci_reset(ohci_controller_t *controller)
{
	uint32_t temp_value, temp, fmint;
	int i;

	/* Disable All Interrupts */
	controller->registers->HcInterruptDisable = (uint32_t)X86_OHCI_INTR_MASTER_INTR;

	/* Perform a reset of HC Controller */
	ohci_set_mode(controller, X86_OHCI_CTRL_USB_SUSPEND);
	clock_stall(200);

	/* Okiiii, reset controller, we need to save FmInterval */
	fmint = controller->registers->HcFmInterval;

	/* Set bit 0 to request reboot */
	temp = controller->registers->HcCommandStatus;
	temp |= X86_OHCI_CMD_RESETCTRL;
	controller->registers->HcCommandStatus = temp;

	/* Wait for reboot (takes maximum of 10 ms) */
	i = 0;
	while ((i < 500) && controller->registers->HcCommandStatus & X86_OHCI_CMD_RESETCTRL)
	{
		clock_stall(1);
		i++;
	}

	/* Sanity */
	if (i == 500)
	{
		printf("OHCI: Reset Timeout :(\n");
		return;
	}

	/**************************************/
	/* We now have 2 ms to complete setup */
	/**************************************/

	/* Set HcHCCA to phys address of HCCA */
	controller->registers->HcHCCA = controller->hcca_space;

	/* Initial values for frame */
	controller->hcca->current_frame = 0;
	controller->hcca->head_done = 0;

	/* Setup initial ED points */
	controller->registers->HcControlHeadED =
		controller->registers->HcControlCurrentED = 0;
	controller->registers->HcBulkHeadED =
		controller->registers->HcBulkCurrentED = 0;

	/* Set HcEnableInterrupt to all except SOF and OC */
	controller->registers->HcInterruptDisable = (X86_OHCI_INTR_SOF | X86_OHCI_INTR_ROOT_HUB_EVENT | X86_OHCI_INTR_OWNERSHIP_EVENT);
	controller->registers->HcInterruptStatus = ~(uint32_t)0;
	controller->registers->HcInterruptEnable = (X86_OHCI_INTR_SCHEDULING_OVRRN | X86_OHCI_INTR_HEAD_DONE |
		X86_OHCI_INTR_RESUME_DETECT | X86_OHCI_INTR_FATAL_ERROR | X86_OHCI_INTR_FRAME_OVERFLOW | X86_OHCI_INTR_MASTER_INTR);

	/* Set HcPeriodicStart to a value that is 90% of FrameInterval in HcFmInterval */
	temp_value = (controller->registers->HcFmInterval & 0x3FFF);
	controller->registers->HcPeriodicStart = (temp_value / 10) * 9;

	/* Setup Control */
	temp = controller->registers->HcControl;
	if (temp & X86_OHCI_CTRL_REMOTE_WAKE)
		temp |= X86_OHCI_CTRL_REMOTE_WAKE;

	/* Clear Lists, Mode, Ratio and IR */
	temp = (temp & ~(0x0000003C | X86_OHCI_CTRL_USB_SUSPEND | 0x3 | 0x100));

	/* Set Ratio (4:1) and Mode (Operational) */
	temp |= (0x3 | X86_OHCI_CTRL_USB_WORKING);
	controller->registers->HcControl = temp;

	/* Now restore FmInterval */
	controller->registers->HcFmInterval = fmint;

	/* Controller is now running! */
	printf("OHCI: Controller %u Started, Control 0x%x\n",
		controller->id, controller->registers->HcControl);

	/* Check Power Mode */
	if (controller->registers->HcRhDescriptorA & (1 << 9))
	{
		controller->power_mode = X86_OHCI_POWER_ALWAYS_ON;
		controller->registers->HcRhStatus = X86_OHCI_STATUS_POWER_ON;
		controller->registers->HcRhDescriptorB = 0;
	}
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
			controller->registers->HcRhStatus = X86_OHCI_STATUS_POWER_ON;
			controller->power_mode = X86_OHCI_POWER_PORT_GLOBAL;
		}
	}

	/* Now we can enable hub events (and clear interrupts) */
	controller->registers->HcInterruptStatus &= ~(uint32_t)0;
	controller->registers->HcInterruptEnable = X86_OHCI_INTR_ROOT_HUB_EVENT;
}

/* ED Functions */
uint32_t ohci_allocate_ep(ohci_controller_t *controller)
{
	interrupt_status_t int_state;
	int32_t index = -1;
	ohci_endpoint_desc_t *ed;

	/* Pick a QH */
	int_state = interrupt_disable();
	spinlock_acquire(&controller->lock);

	/* Grap it, locked operation */
	while (index == -1)
	{
		ed = controller->ed_pool[controller->ed_index];

		if (ed->flags & X86_OHCI_EP_SKIP)
		{
			/* Done! */
			index = controller->ed_index;
			ed->flags = 0;
		}

		controller->ed_index++;

		/* Index Sanity */
		if (controller->ed_index == X86_OHCI_POOL_NUM_ED)
			controller->ed_index = 0;
	}

	/* Release lock */
	spinlock_release(&controller->lock);
	interrupt_set_state(int_state);

	return (uint32_t)index;
}

void ohci_ep_init(ohci_endpoint_desc_t *ep, addr_t first_td, uint32_t type, 
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
	ep->flags |= X86_OHCI_EP_TYPE((type & 0xF));

	/* Set TD */
	if (first_td == X86_OHCI_TRANSFER_END_OF_LIST)
		ep->head_ptr = X86_OHCI_TRANSFER_END_OF_LIST;
	else
		ep->head_ptr = memory_getmap(NULL, (first_td & ~0xD));

	/* Set Tail */
	ep->next_ed = 0;
	ep->next_ed_virt = 0;
}

/* TD Functions */
uint32_t ohci_allocate_td(ohci_controller_t *controller)
{
	interrupt_status_t int_state;
	int32_t index = -1;
	ohci_gtransfer_desc_t *td;

	/* Pick a QH */
	int_state = interrupt_disable();
	spinlock_acquire(&controller->lock);

	/* Grap it, locked operation */
	while (index == -1)
	{
		td = controller->td_pool[controller->td_index];

		if (!(td->flags & X86_OHCI_TRANSFER_BUF_NOCC))
		{
			/* Done! */
			index = controller->td_index;
			td->flags |= X86_OHCI_TRANSFER_BUF_NOCC;
		}

		controller->td_index++;

		/* Index Sanity */
		if (controller->td_index == X86_OHCI_POOL_NUM_TD)
			controller->td_index = 0;
	}

	/* Release lock */
	spinlock_release(&controller->lock);
	interrupt_set_state(int_state);

	return (uint32_t)index;
}

ohci_gtransfer_desc_t *ohci_td_setup(ohci_controller_t *controller, 
	ohci_endpoint_desc_t *ed, addr_t next_td, uint32_t toggle, uint8_t request_direction,
	uint8_t request_type, uint8_t request_value_low, uint8_t request_value_high, uint16_t request_index,
	uint16_t request_length, void **td_buffer)
{
	usb_packet_t *packet;
	ohci_gtransfer_desc_t *td;
	addr_t td_phys;
	void *buffer;
	uint32_t td_index;

	/* Allocate a TD */
	td_index = ohci_allocate_td(controller);

	/* Grab a TD and a buffer */
	td = controller->td_pool[td_index];
	buffer = controller->td_pool_buffers[td_index];
	td_phys = controller->td_pool_phys[td_index];
	
	/* EOL ? */
	if (next_td == X86_OHCI_TRANSFER_END_OF_LIST)
		td->next_td = X86_OHCI_TRANSFER_END_OF_LIST;
	else	/* Get physical address of next_td and set next_td to that */
		td->next_td = memory_getmap(NULL, (virtaddr_t)next_td); 

	/* Setup the TD for a SETUP TD */
	td->flags = 0;
	td->flags |= X86_OHCI_TRANSFER_BUF_ROUNDING;
	td->flags |= X86_OHCI_TRANSFER_BUF_PID_SETUP;
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
	td->cbp = memory_getmap(NULL, (virtaddr_t)buffer);
	td->buffer_end = td->cbp + sizeof(usb_packet_t) - 1;

	/* Make Queue Tail point to this */
	ed->tail_ptr = td_phys;

	return td;
}

ohci_gtransfer_desc_t *ohci_td_io(ohci_controller_t *controller,
	ohci_endpoint_desc_t *ed, addr_t next_td, uint32_t toggle, uint32_t pid, 
	uint32_t length, void **td_buffer)
{
	ohci_gtransfer_desc_t *td;
	addr_t td_phys;
	void *buffer;
	uint32_t td_index;

	/* Allocate a TD */
	td_index = ohci_allocate_td(controller);

	/* Grab a TD and a buffer */
	td = controller->td_pool[td_index];
	buffer = controller->td_pool_buffers[td_index];
	td_phys = controller->td_pool_phys[td_index];

	/* EOL ? */
	if (next_td == X86_OHCI_TRANSFER_END_OF_LIST)
		td->next_td = X86_OHCI_TRANSFER_END_OF_LIST;
	else	/* Get physical address of next_td and set next_td to that */
		td->next_td = memory_getmap(NULL, (virtaddr_t)next_td);

	/* Setup the TD for a IO TD */
	td->flags = 0;
	td->flags |= X86_OHCI_TRANSFER_BUF_ROUNDING;
	td->flags |= pid;
	//td->flags |= X86_OHCI_TRANSFER_BUF_NO_INTERRUPT;	/* We don't want interrupt */
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
	uint32_t temp;

	temp = ohci_allocate_ep(ctrl);
	request->data = ctrl->ed_pool[temp];

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
	transaction->transfer_descriptor = (void*)ohci_td_setup(ctrl, 
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
	transaction->transfer_descriptor = (void*)ohci_td_io(ctrl,
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
	transaction->transfer_descriptor = (void*)ohci_td_io(ctrl,
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
	/* Wuhu */
	usb_hc_transaction_t *transaction = request->transactions;
	ohci_controller_t *ctrl = (ohci_controller_t*)controller;
	int completed = 1;
	interrupt_status_t int_state = 0;
	ohci_gtransfer_desc_t *td = NULL;
	uint32_t condition_code;
	addr_t ed_address;

	/* Get physical */
	ed_address = memory_getmap(NULL, (virtaddr_t)request->data);

	/* Set as not completed for start */
	request->completed = 0;

	/* Add dummy TD to end */
	usb_transaction_out(usb_get_hcd(ctrl->hcd_id), request, 1, 0, 0);

	/* Setup an ED for this */
	((ohci_endpoint_desc_t*)request->data)->hcd_data = (uint32_t)transaction;
	ohci_ep_init(request->data, (addr_t)request->transactions->transfer_descriptor, request->type,
		request->device->address, request->endpoint, request->length, request->lowspeed);

	/* Now lets try the transaction */
	int_state = interrupt_disable();
	spinlock_acquire(&ctrl->lock);

	/* Set true */
	completed = 1;

	/* Add this transaction to list */
	((ohci_endpoint_desc_t*)request->data)->head_ptr &= ~(0x00000001);
	list_append(ctrl->transactions_list, list_create_node(0, request->data));

	/* Is this the "first" control transfer? */
	if (request->type == X86_USB_REQUEST_TYPE_CONTROL)
	{
		if (ctrl->transactions_waiting_control > 0)
		{
			/* Insert it */
			if (ctrl->transaction_queue_control == 0)
				ctrl->transaction_queue_control = (uint32_t)request->data;
			else
			{
				ohci_endpoint_desc_t *ep = (ohci_endpoint_desc_t*)ctrl->transaction_queue_control;

				/* Find tail */
				while (ep->next_ed)
					ep = (ohci_endpoint_desc_t*)ep->next_ed_virt;

				/* Insert it */
				ep->next_ed = ed_address;
				ep->next_ed_virt = (uint32_t)request->data;
			}

			/* Increase */
			ctrl->transactions_waiting_control++;

			/* Release spinlock */
			spinlock_release(&ctrl->lock);
			interrupt_set_state(int_state);
		}
		else
		{
			/* Add it HcControl/BulkCurrentED */
			ctrl->registers->HcControlHeadED = 
				ctrl->registers->HcControlCurrentED = ed_address;

			/* Increase */
			ctrl->transactions_waiting_control++;

			/* Release spinlock */
			spinlock_release(&ctrl->lock);
			interrupt_set_state(int_state);

			/* Set Lists Filled (Enable Them) */
			ctrl->registers->HcCommandStatus |= X86_OHCI_CMD_TDACTIVE_CTRL;
			ctrl->registers->HcControl |= X86_OHCI_CTRL_CONTROL_LIST;
		}
	}
	else if (request->type == X86_USB_REQUEST_TYPE_BULK)
	{
		if (ctrl->transactions_waiting_bulk > 0)
		{
			/* Insert it */
			if (ctrl->transaction_queue_bulk == 0)
				ctrl->transaction_queue_bulk = (addr_t)request->data;
			else
			{
				ohci_endpoint_desc_t *ep = (ohci_endpoint_desc_t*)ctrl->transaction_queue_bulk;

				/* Find tail */
				while (ep->next_ed)
					ep = (ohci_endpoint_desc_t*)ep->next_ed_virt;

				/* Insert it */
				ep->next_ed = ed_address;
				ep->next_ed_virt = (uint32_t)request->data;
			}

			/* Increase */
			ctrl->transactions_waiting_bulk++;
			
			/* Release spinlock */
			spinlock_release(&ctrl->lock);
			interrupt_set_state(int_state);
		}
		else
		{
			/* Add it HcControl/BulkCurrentED */
			ctrl->registers->HcBulkHeadED = 
				ctrl->registers->HcBulkCurrentED = ed_address;

			/* Increase */
			ctrl->transactions_waiting_bulk++;

			/* Release spinlock */
			spinlock_release(&ctrl->lock);
			interrupt_set_state(int_state);

			/* Set Lists Filled (Enable Them) */
			ctrl->registers->HcCommandStatus |= X86_OHCI_CMD_TDACTIVE_BULK;
			ctrl->registers->HcControl |= X86_OHCI_CTRL_BULK_LIST;
		}
	}
	
	/* Wait for interrupt */
	scheduler_sleep_thread((addr_t*)request->data);
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

/* Install an Interrupt Endpoint */
void ohci_install_interrupt(void *controller, usb_hc_device_t *device, usb_hc_endpoint_t *endpoint,
	void *in_buffer, size_t in_bytes, void(*callback)(void*, size_t), void *arg)
{
	usb_hc_t *hcd = device->hcd;
	ohci_controller_t *ctrl = (ohci_controller_t*)controller;
	ohci_endpoint_desc_t *ep = NULL, *iep = NULL;
	interrupt_status_t int_state;
	ohci_gtransfer_desc_t *td = NULL;
	void *td_buffer = NULL;
	uint32_t period = 32;
	uint32_t i;
	uint32_t lowspeed = (hcd->ports[device->port]->full_speed == 1) ? 0 : 1;
	ohci_periodic_callback_t *cb_info = (ohci_periodic_callback_t*)kmalloc(sizeof(ohci_periodic_callback_t));

	/* Calculate period */
	for (; (period >= endpoint->interval) && (period > 0);)
		period >>= 1;

	/* Grab an EP */
	i = ohci_allocate_ep(ctrl);
	ep = ctrl->ed_pool[i];

	/* Get TD(s) */
	i = ohci_allocate_td(ctrl);
	td = ctrl->td_pool[i];
	td_buffer = ctrl->td_pool_buffers[i];

	/* Setup CB Information */
	cb_info->buffer = in_buffer;
	cb_info->bytes = in_bytes;
	cb_info->callback = callback;
	cb_info->args = arg;
	cb_info->td_index = i;


	if (endpoint->bandwidth > 1)
	{
		/* Oh god support this :( */
	}

	/* Setup TD */
	td->flags = 0;
	td->flags |= X86_OHCI_TRANSFER_BUF_ROUNDING;
	td->flags |= X86_OHCI_TRANSFER_BUF_PID_IN;
	td->flags |= ((endpoint->toggle & 0x1) << 24);
	td->flags |= X86_OHCI_TRANSFER_BUF_TD_TOGGLE;
	td->flags |= X86_OHCI_TRANSFER_BUF_NOCC;

	td->next_td = 0x1;

	td->cbp = memory_getmap(NULL, (virtaddr_t)td_buffer);
	td->buffer_end = td->cbp + 0x200 - 1;

	/* Setup EP */
	ep->head_ptr = memory_getmap(NULL, (virtaddr_t)td);
	ep->tail_ptr = 0;
	ep->hcd_data = (uint32_t)cb_info;

	ep->flags = (device->address & X86_OHCI_EP_ADDR_BITS); /* Device Address */
	ep->flags |= X86_OHCI_EP_EP_NUM((endpoint->address & X86_OHCI_EP_EP_NUM_BITS));
	ep->flags |= X86_OHCI_EP_LOWSPEED(lowspeed); /* Device Speed */
	ep->flags |= X86_OHCI_EP_PACKET_SIZE((endpoint->max_packet_size & X86_OHCI_EP_PACKET_BITS));
	ep->flags |= X86_OHCI_EP_TYPE(2);

	/* Add transfer */
	list_append(ctrl->transactions_list, list_create_node(0, ep));

	/* Ok, queue it
	 * Lock & stop ints */
	int_state = interrupt_disable();
	spinlock_acquire(&ctrl->lock);

	if (period == 1)
	{
		iep = &ctrl->itable->ms1[0];

		/* Insert it */
		ep->next_ed_virt = iep->next_ed_virt;
		ep->next_ed = iep->next_ed;
		iep->next_ed = memory_getmap(NULL, (virtaddr_t)ep);
		iep->next_ed_virt = (uint32_t)ep;
	}
	else if (period == 2)
	{
		iep = &ctrl->itable->ms2[ctrl->i2];

		/* Insert it */
		ep->next_ed_virt = iep->next_ed_virt;
		ep->next_ed = iep->next_ed;
		iep->next_ed = memory_getmap(NULL, (virtaddr_t)ep);
		iep->next_ed_virt = (uint32_t)ep;

		/* Increase i2 */
		ctrl->i2++;
		if (ctrl->i2 == 2)
			ctrl->i2 = 0;
	}
	else if (period == 4)
	{
		iep = &ctrl->itable->ms4[ctrl->i4];

		/* Insert it */
		ep->next_ed_virt = iep->next_ed_virt;
		ep->next_ed = iep->next_ed;
		iep->next_ed = memory_getmap(NULL, (virtaddr_t)ep);
		iep->next_ed_virt = (uint32_t)ep;

		/* Increase i4 */
		ctrl->i4++;
		if (ctrl->i4 == 4)
			ctrl->i4 = 0;
	}
	else if (period == 8)
	{
		iep = &ctrl->itable->ms8[ctrl->i8];

		/* Insert it */
		ep->next_ed_virt = iep->next_ed_virt;
		ep->next_ed = iep->next_ed;
		iep->next_ed = memory_getmap(NULL, (virtaddr_t)ep);
		iep->next_ed_virt = (uint32_t)ep;

		/* Increase i8 */
		ctrl->i8++;
		if (ctrl->i8 == 8)
			ctrl->i8 = 0;
	}
	else if (period == 16)
	{
		iep = &ctrl->itable->ms16[ctrl->i16];

		/* Insert it */
		ep->next_ed_virt = iep->next_ed_virt;
		ep->next_ed = iep->next_ed;
		iep->next_ed = memory_getmap(NULL, (virtaddr_t)ep);
		iep->next_ed_virt = (uint32_t)ep;

		/* Increase i16 */
		ctrl->i16++;
		if (ctrl->i16 == 16)
			ctrl->i16 = 0;
	}
	else
	{
		/* 32 */
		iep = ctrl->ed32[ctrl->i32];

		/* Insert it */
		ep->next_ed_virt = (addr_t)iep;
		ep->next_ed = memory_getmap(NULL, (virtaddr_t)iep);

		/* Make int-table point to this */
		ctrl->hcca->interrupt_table[ctrl->i32] = memory_getmap(NULL, (virtaddr_t)ep);
		ctrl->ed32[ctrl->i32] = ep;

		/* Increase i32 */
		ctrl->i32++;
		if (ctrl->i32 == 32)
			ctrl->i32 = 0;
	}

	/* Done */
	spinlock_release(&ctrl->lock);
	interrupt_set_state(int_state);

	/* Enable Queue in case it was disabled */
	ctrl->registers->HcControl |= X86_OCHI_CTRL_PERIODIC_LIST;
}

/* Process Done Queue */
void ohci_process_done_queue(ohci_controller_t *controller, addr_t done_head)
{
	list_t *transactions = controller->transactions_list;
	list_node_t *ta = NULL;
	addr_t td_physical;
	uint32_t transfer_type = 0;
	int n;

	/* Find it */
	n = 0;
	ta = list_get_node_by_id(transactions, 0, n);
	while (ta != NULL)
	{
		ohci_endpoint_desc_t *ep = (ohci_endpoint_desc_t*)ta->data;
		transfer_type = (ep->flags >> 27);

		/* Special Case */
		if (transfer_type == 2)
		{
			/* Interrupt */
			ohci_periodic_callback_t *cb_info = (ohci_periodic_callback_t*)ep->hcd_data;
			
			/* Is it this? */
			if (controller->td_pool_phys[cb_info->td_index] == done_head)
			{
				/* Yeps */
				void *td_buffer = controller->td_pool_buffers[cb_info->td_index];
				ohci_gtransfer_desc_t *interrupt_td = controller->td_pool[cb_info->td_index];
				uint32_t condition_code = (interrupt_td->flags & 0xF0000000) >> 28;

				/* Sanity */
				if (condition_code == 0)
				{
					/* Get data */
					memcpy(cb_info->buffer, td_buffer, cb_info->bytes);

					/* Inform Callback */
					cb_info->callback(cb_info->args, cb_info->bytes);
				}

				/* Restart TD */
				interrupt_td->cbp = interrupt_td->buffer_end - 0x200 + 1;
				interrupt_td->flags |= X86_OHCI_TRANSFER_BUF_NOCC;

				/* Reset EP */
				ep->head_ptr = controller->td_pool_phys[cb_info->td_index];

				/* We are done, return */
				return;
			}
		}
		else
		{
			usb_hc_transaction_t *t_list = (usb_hc_transaction_t*)ep->hcd_data;

			/* Process TD's and see if all transfers are done */
			while (t_list)
			{
				/* Get physical of TD */
				td_physical = memory_getmap(NULL, (virtaddr_t)t_list->transfer_descriptor);

				if (td_physical == done_head)
				{
					/* Is this the last? :> */
					if (t_list->link == NULL || t_list->link->link == NULL)
					{
						n = 0xDEADBEEF;
						break;
					}
					else
					{
						/* Error :/ */
						ohci_gtransfer_desc_t *td = (ohci_gtransfer_desc_t*)t_list->transfer_descriptor;
						uint32_t condition_code = (td->flags & 0xF0000000) >> 28;
						printf("FAILURE: TD Flags 0x%x, TD Condition Code %u (%s)\n", td->flags, condition_code, ohci_err_msgs[condition_code]);
						n = 0xBEEFDEAD;
						break;
					}
				}

				/* Next */
				t_list = t_list->link;
			}

			if (n == 0xDEADBEEF || n == 0xBEEFDEAD)
				break;
		}

		n++;
		ta = list_get_node_by_id(transactions, 0, n);	
	}

	if (ta != NULL && (n == 0xDEADBEEF || n == 0xBEEFDEAD))
	{
		/* Either it failed, or it succeded */

		/* So now, before waking up a sleeper we see if transactions are pending
		 * if they are, we simply copy the queue over to the current */
		spinlock_acquire(&controller->lock);

		/* Any Controls waiting? */
		if (transfer_type == 0)
		{
			if (controller->transactions_waiting_control > 0)
			{
				/* Get physical of EP */
				addr_t ep_physical = memory_getmap(NULL, controller->transaction_queue_control);

				/* Set it */
				controller->registers->HcControlHeadED =
					controller->registers->HcControlCurrentED = ep_physical;

				/* Start queue */
				controller->registers->HcCommandStatus |= X86_OHCI_CMD_TDACTIVE_CTRL;
				controller->registers->HcControl |= X86_OHCI_CTRL_CONTROL_LIST;
			}

			/* Reset control queue */
			controller->transaction_queue_control = 0;
			controller->transactions_waiting_control = 0;
		}
		else if (transfer_type == 1)
		{
			/* Bulk */
			if (controller->transactions_waiting_bulk > 0)
			{
				/* Get physical of EP */
				addr_t ep_physical = memory_getmap(NULL, controller->transaction_queue_bulk);

				/* Add it to queue */
				controller->registers->HcBulkHeadED = 
					controller->registers->HcBulkCurrentED = ep_physical;

				/* Start queue */
				controller->registers->HcCommandStatus |= X86_OHCI_CMD_TDACTIVE_BULK;
				controller->registers->HcControl |= X86_OHCI_CTRL_BULK_LIST;
			}

			/* Reset control queue */
			controller->transaction_queue_bulk = 0;
			controller->transactions_waiting_bulk = 0;
		}

		/* Done */
		spinlock_release(&controller->lock);

		/* Mark EP Descriptor as sKip SEHR IMPORTANTE */
		((ohci_endpoint_desc_t*)ta->data)->flags = X86_OHCI_EP_SKIP;

		/* Wake a node */
		scheduler_wakeup_one((addr_t*)ta->data);

		/* Remove from list */
		list_remove_by_node(transactions, ta);

		/* Cleanup node */
		kfree(ta);
	}
}

/* Interrupt Handler
* Make sure that this controller actually made the interrupt
* as this interrupt will be shared with other OHCI's */
void ohci_interrupt_handler(void *data)
{
	uint32_t intr_state = 0;
	ohci_controller_t *controller = (ohci_controller_t*)data;

	/* Is this our interrupt ? */
	if (controller->hcca->head_done != 0)
	{
		/* Acknowledge */
		intr_state = X86_OHCI_INTR_HEAD_DONE;

		if (controller->hcca->head_done & 0x1)
		{
			/* Get rest of interrupts, since head_done has halted */
			intr_state |= (controller->registers->HcInterruptStatus & controller->registers->HcInterruptEnable);
		}
	}
	else
	{
		/* Was it this controller that made the interrupt?
		* We only want the interrupts we have set as enabled */
		intr_state = (controller->registers->HcInterruptStatus & controller->registers->HcInterruptEnable);

		if (intr_state == 0)
			return;
	}

	/* Debug */
	//printf("OHCI: Controller %u Interrupt: 0x%x\n", controller->hcd_id, intr_state);

	/* Disable Interrupts */
	controller->registers->HcInterruptDisable = (uint32_t)X86_OHCI_INTR_MASTER_INTR;

	/* Fatal Error? */
	if (intr_state & X86_OHCI_INTR_FATAL_ERROR)
	{
		printf("OHCI %u: Fatal Error, resetting...\n", controller->id);
		ohci_reset(controller);
		return;
	}

	/* Flag for end of frame type interrupts */
	if (intr_state & (X86_OHCI_INTR_SCHEDULING_OVRRN | X86_OHCI_INTR_HEAD_DONE | X86_OHCI_INTR_SOF | X86_OHCI_INTR_FRAME_OVERFLOW))
		intr_state |= X86_OHCI_INTR_MASTER_INTR;

	/* Scheduling Overrun? */
	if (intr_state & X86_OHCI_INTR_SCHEDULING_OVRRN)
	{
		printf("OHCI %u: Scheduling Overrun\n", controller->id);

		/* Acknowledge Interrupt */
		controller->registers->HcInterruptStatus = X86_OHCI_INTR_SCHEDULING_OVRRN;
		intr_state = intr_state & ~(X86_OHCI_INTR_SCHEDULING_OVRRN);
	}

	/* Resume Detection? */
	if (intr_state & X86_OHCI_INTR_RESUME_DETECT)
	{
		printf("OHCI %u: Resume Detected\n", controller->id);

		/* We must wait 20 ms before putting controller to Operational */
		clock_stall_noint(2000);
		ohci_set_mode(controller, X86_OHCI_CTRL_USB_WORKING);

		/* Acknowledge Interrupt */
		controller->registers->HcInterruptStatus = X86_OHCI_INTR_RESUME_DETECT;
		intr_state = intr_state & ~(X86_OHCI_INTR_RESUME_DETECT);
	}

	/* Frame Overflow
	* Happens when it rolls over from 0xFFFF to 0 */
	if (intr_state & X86_OHCI_INTR_FRAME_OVERFLOW)
	{
		/* Acknowledge Interrupt */
		controller->registers->HcInterruptStatus = X86_OHCI_INTR_FRAME_OVERFLOW;
		intr_state = intr_state & ~(X86_OHCI_INTR_FRAME_OVERFLOW);
	}

	/* Why yes, yes it was, wake up the TD handler thread
	* if it was head_done_writeback */
	if (intr_state & X86_OHCI_INTR_HEAD_DONE)
	{
		/* Wuhu, handle this! */
		uint32_t td_address = (controller->hcca->head_done & ~(0x00000001));

		ohci_process_done_queue(controller, td_address);

		/* Acknowledge Interrupt */
		controller->hcca->head_done = 0;
		controller->registers->HcInterruptStatus = X86_OHCI_INTR_HEAD_DONE;
		intr_state = intr_state & ~(X86_OHCI_INTR_HEAD_DONE);
	}

	/* Root Hub Status Change
	* Do a port status check */
	if (intr_state & X86_OHCI_INTR_ROOT_HUB_EVENT)
	{
		/* Port does not matter here */
		usb_event_create(usb_get_hcd(controller->hcd_id), 0, X86_USB_EVENT_ROOTHUB_CHECK);

		/* Acknowledge Interrupt */
		controller->registers->HcInterruptStatus = X86_OHCI_INTR_ROOT_HUB_EVENT;
		intr_state = intr_state & ~(X86_OHCI_INTR_ROOT_HUB_EVENT);
	}

	/* Start of Frame? */
	if (intr_state & X86_OHCI_INTR_SOF)
	{
		/* Acknowledge Interrupt */
		controller->registers->HcInterruptStatus = X86_OHCI_INTR_SOF;
		intr_state = intr_state & ~(X86_OHCI_INTR_SOF);
	}

	/* Mask out remaining interrupts, we dont use them */
	if (intr_state & ~(X86_OHCI_INTR_MASTER_INTR))
		controller->registers->HcInterruptDisable = intr_state;

	/* Enable Interrupts */
	controller->registers->HcInterruptEnable = (uint32_t)X86_OHCI_INTR_MASTER_INTR;
}
