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
* MollenOS X86-32 USB UHCI Controller Driver
* Todo:
* Fix the interrupt spam of HcHalted
* Figure out how we can send transactions correctly
* For gods sake make it work, and get some sleep
*/

/* Includes */
#include <arch.h>
#include <lapic.h>
#include <assert.h>
#include <memory.h>
#include <timers.h>
#include <scheduler.h>
#include <heap.h>
#include <list.h>
#include <stdio.h>
#include <string.h>

#include <drivers\usb\usb.h>
#include <drivers\usb\uhci\uhci.h>

/* Uhci Devices Strings */
static pci_dev_info_t uhci_devices_intel[] = {
	{ 0x2412, "82801AA (ICH)" },
	{ 0x2422, "82801AB (ICH0)" },
	{ 0x2442, "82801BA/BAM (ICH2) USB-A" },
	{ 0x2444, "82801BA/BAM (ICH2) USB-B" },
	{ 0x2452, "82801E" },
	{ 0x2482, "82801CA/CAM (ICH3) USB-A" },
	{ 0x2484, "82801CA/CAM (ICH3) USB-B" },
	{ 0x2487, "82801CA/CAM (ICH3) USB-C" },
	{ 0x24c2, "82801DB (ICH4) USB-A" },
	{ 0x24c4, "82801DB (ICH4) USB-B" },
	{ 0x24c7, "82801DB (ICH4) USB-C" },
	{ 0x24d2, "82801EB/ER (ICH5/ICH5R) USB-A" },
	{ 0x24d4, "82801EB/ER (ICH5/ICH5R) USB-B" },
	{ 0x24d7, "82801EB/ER (ICH5/ICH5R) USB-C" },
	{ 0x24de, "82801EB/ER (ICH5/ICH5R) USB-D" },
	{ 0x25a9, "6300ESB" },
	{ 0x24aa, "6300ESB" },
	{ 0x7020, "82371SB (PIIX3)" },
	{ 0x7112, "82371AB/EB/MB (PIIX4)" },
	{ 0x719a, "82443MX" },
	{ 0x7602, "82372FB/82468GX (PIIX5)" },
	{ 0, 0 }
};

static pci_dev_info_t uhci_devices_via[] = {
	{ 0x3038, "VT83C572, VT6202" },
	{ 0, 0 }
};

/* Globals */
volatile uint32_t glb_uhci_id = 0;

/* Externs */
extern void _yield(void);

/* Prototypes (Internal) */
void uhci_init_queues(uhci_controller_t *controller);
void uhci_setup(void *c_data);
int uhci_interrupt_handler(void *args);

void uhci_port_reset(uhci_controller_t *controller, int port, int noint);
void uhci_port_check(uhci_controller_t *controller, int port);
void uhci_ports_check(void *data);

void uhci_port_setup(void *data, usb_hc_port_t *port);

void uhci_transaction_init(void *controller, usb_hc_request_t *request);
usb_hc_transaction_t *uhci_transaction_setup(void *controller, usb_hc_request_t *request);
usb_hc_transaction_t *uhci_transaction_in(void *controller, usb_hc_request_t *request);
usb_hc_transaction_t *uhci_transaction_out(void *controller, usb_hc_request_t *request);
void uhci_transaction_send(void *controller, usb_hc_request_t *request);

void uhci_install_interrupt(void *controller, usb_hc_device_t *device, usb_hc_endpoint_t *endpoint,
	void *in_buffer, size_t in_bytes, void(*callback)(void*, size_t), void *arg);

/* Helpers */
uint16_t uhci_read16(uhci_controller_t *controller, uint16_t reg)
{
	/* Write new state */
	return inw((controller->io_base + reg));
}

uint32_t uhci_read32(uhci_controller_t *controller, uint16_t reg)
{
	return inl((controller->io_base + reg));
}

void uhci_write8(uhci_controller_t *controller, uint16_t reg, uint8_t value)
{
	/* Write new state */
	outb((controller->io_base + reg), value);
}

void uhci_write16(uhci_controller_t *controller, uint16_t reg, uint16_t value)
{
	/* Write new state */
	outw((controller->io_base + reg), value);
}

void uhci_write32(uhci_controller_t *controller, uint16_t reg, uint32_t value)
{
	outl((controller->io_base + reg), value);
}

addr_t uhci_align(addr_t addr, addr_t alignment_bits, addr_t alignment)
{
	addr_t aligned_addr = addr;

	if (aligned_addr & alignment_bits)
	{
		aligned_addr &= ~alignment_bits;
		aligned_addr += alignment;
	}

	return aligned_addr;
}

/* The two functions below are to setup QH Frame List */
uint32_t uhci_ffs(uint32_t val)
{
	uint32_t num = 0;

	/* 16 Bits */
	if (!(val & 0xFFFF))
	{
		num += 16;
		val >>= 16;
	}

	/* 8 Bits */
	if (!(val & 0xFF))
	{
		num += 8;
		val >>= 8;
	}

	/* 4 Bits */
	if (!(val & 0xF))
	{
		num += 4;
		val >>= 4;
	}

	/* 2 Bits */
	if (!(val & 0x3))
	{
		num += 2;
		val >>= 2;
	}

	/* 1 Bit */
	if (!(val & 0x1))
		num++;

	/* Done */
	return num;
}

uint32_t uhci_determine_intqh(uhci_controller_t *controller, uint32_t frame)
{
	uint32_t index;

	/* Determine index from first free bit 
	 * 8 queues */
	index = 8 - uhci_ffs(frame | X86_UHCI_NUM_FRAMES);

	/* Sanity */
	if (index < 2 || index > 8)
		index = X86_UHCI_POOL_ASYNC;

	return (controller->qh_pool_phys[index] | X86_UHCI_TD_LINK_QH);
}

/* Start / Stop */
void uhci_start(uhci_controller_t *controller)
{
	/* Send run command */
	uint16_t junk = uhci_read16(controller, X86_UHCI_REGISTER_COMMAND);
	junk |= X86_UHCI_CMD_RUN;
	uhci_write16(controller, X86_UHCI_REGISTER_COMMAND, junk);

	/* Wait for it to start */
	//while (uhci_read16(controller, X86_UHCI_REGISTER_STATUS) & X86_UHCI_STATUS_HALTED);
}

void uhci_stop(uhci_controller_t *controller)
{
	/* Send stop command */
	uint16_t junk = uhci_read16(controller, X86_UHCI_REGISTER_COMMAND);
	junk &= ~(X86_UHCI_CMD_RUN);
	uhci_write16(controller, X86_UHCI_REGISTER_COMMAND, junk);
}

/* Function Allocates Resources
* and starts a init thread */
void uhci_init(pci_driver_t *device)
{
	uhci_controller_t *controller = NULL;

	/* Allocate Resources for this controller */
	controller = (uhci_controller_t*)kmalloc(sizeof(uhci_controller_t));
	controller->pci_info = device;
	controller->id = glb_uhci_id;
	controller->initialized = 0;
	spinlock_reset(&controller->lock);
	glb_uhci_id++;

	/* Enable i/o and bus mastering */
	pci_write_word((const uint16_t)device->bus, (const uint16_t)device->device, (const uint16_t)device->function, 0x4, 0x0005);

	/* Get I/O Base from Bar4 */
	controller->io_base = (device->header->bar4 & 0x0000FFFE);
	
	/* Get DMA */
	controller->frame_list_phys = physmem_alloc_block_dma();
	controller->frame_list = (void*)controller->frame_list_phys;

	/* Memset */
	memset(controller->frame_list, 0, 0x1000);

	/* Reset Controller */
	threading_create_thread("UhciSetup", uhci_setup, controller, 0);
}

void uhci_setup(void *c_data)
{
	usb_hc_t *hc;
	uint16_t enabled_ports = 0;
	uint16_t temp = 0, i = 0;
	uhci_controller_t *controller;

	controller = (uhci_controller_t*)c_data;

	/* Disable Legacy Emulation & Interrupts */
	pci_write_word((const uint16_t)controller->pci_info->bus, (const uint16_t)controller->pci_info->device,
		(const uint16_t)controller->pci_info->function, X86_UHCI_USBLEG, 0x8F00);

	/* Disable interrupts while configuring (and stop controller) */
	uhci_write16(controller, X86_UHCI_REGISTER_COMMAND, 0x0000);
	uhci_write16(controller, X86_UHCI_REGISTER_INTR, 0x0000);

	/* Reset Controller */

	/* Global Reset */
	uhci_write16(controller, X86_UHCI_REGISTER_COMMAND, X86_UHCI_CMD_GRESET);
	clock_stall(100);
	uhci_write16(controller, X86_UHCI_REGISTER_COMMAND, 0x0000);

	/* HC Reset */
	uhci_write16(controller, X86_UHCI_REGISTER_COMMAND, X86_UHCI_CMD_HCRESET);

	/* Recovery */
	clock_stall(1);

	/* Wait for reset */
	while (uhci_read16(controller, X86_UHCI_REGISTER_COMMAND) & X86_UHCI_CMD_HCRESET)
		clock_stall(20); 

	/* Disable interrupts while configuring (and stop controller) */
	uhci_write16(controller, X86_UHCI_REGISTER_COMMAND, 0x0000);
	uhci_write16(controller, X86_UHCI_REGISTER_INTR, 0x0000);

	/* We get port count & 0 them */
	for (i = 0; i < 8; i++)
	{
		temp = uhci_read16(controller, (X86_UHCI_REGISTER_PORT_BASE + (i * 2)));

		/* Is it a valid port? */
		if (!(temp & X86_UHCI_PORT_RESERVED))
		{
			/* This reserved bit must be 1 */
			/* And we must have 2 ports atleast */
			if (i > 1)
				break;
		}

		/* 0 It */
		uhci_write16(controller, (X86_UHCI_REGISTER_PORT_BASE + (i * 2)), 0);
	}

	/* Ports are now i */
	controller->ports = i;

	/* Setup Framelist */
	uhci_write32(controller, X86_UHCI_REGISTER_FRBASEADDR, controller->frame_list_phys);
	uhci_write16(controller, X86_UHCI_REGISTER_FRNUM, 0);
	uhci_write8(controller, X86_UHCI_REGISTER_SOFMOD, 64); /* Frame Length 1 ms */

	/* Setup Queues */
	uhci_init_queues(controller);

	/* Turn on only if connected ports */
	for (i = 0; i < controller->ports; i++)
	{
		temp = uhci_read16(controller, (X86_UHCI_REGISTER_PORT_BASE + (i * 2)));
		
		/* enabled? */
		if (temp & X86_UHCI_PORT_CONNECT_STATUS)
			enabled_ports++;
	}

	/* Install IRQ Handler */
	interrupt_install_pci(controller->pci_info, uhci_interrupt_handler, controller);

	/* Enable PCI Interrupts */
	pci_write_word((const uint16_t)controller->pci_info->bus,
		(const uint16_t)controller->pci_info->device,
		(const uint16_t)controller->pci_info->function,
		X86_UHCI_USBLEG, 0x2000);

	/* If vendor is Intel we null out the intel register */
	if (controller->pci_info->header->vendor_id == 0x8086)
		pci_write_byte((const uint16_t)controller->pci_info->bus, (const uint16_t)controller->pci_info->device,
		(const uint16_t)controller->pci_info->function, X86_UHCI_USBRES_INTEL, 0x00);

	/* Now start and wait */
	if (enabled_ports == 0)
	{
		controller->initialized = 2;
		temp = (X86_UHCI_CMD_CF | X86_UHCI_CMD_MAXPACKET64);
	}
	else
	{
		controller->initialized = 1;
		temp = (X86_UHCI_CMD_CF | X86_UHCI_CMD_RUN | X86_UHCI_CMD_MAXPACKET64);
	}
	
	/* Start controller */
	uhci_write16(controller, X86_UHCI_REGISTER_COMMAND, temp);

	/* Enable interrupts */
	uhci_write16(controller, X86_UHCI_REGISTER_INTR, 
		(X86_UHCI_INTR_TIMEOUT | X86_UHCI_INTR_SHORT_PACKET
		| X86_UHCI_INTR_RESUME | X86_UHCI_INTR_COMPLETION));

	/* Give it a 10 ms */
	clock_stall(10);

	/* Debug */
	printf("UHCI %u: Port Count %u, Command Register 0x%x\n", controller->id,
		controller->ports, uhci_read16(controller, X86_UHCI_REGISTER_COMMAND));

	/* Setup HCD */
	hc = usb_init_controller((void*)controller, X86_USB_TYPE_UHCI, controller->ports);

	/* Port Functions */
	hc->root_hub_check = uhci_ports_check;
	hc->port_setup = uhci_port_setup;

	/* Transaction Functions */
	hc->transaction_init = uhci_transaction_init;
	hc->transaction_setup = uhci_transaction_setup;
	hc->transaction_in = uhci_transaction_in;
	hc->transaction_out = uhci_transaction_out;
	hc->transaction_send = uhci_transaction_send;
	hc->install_interrupt = uhci_install_interrupt;

	controller->hcd_id = usb_register_controller(hc);

	/* Install Periodic Check (WTF NO HUB INTERRUPTS!?)
	 * Anyway this will initiate ports */
	timers_create_periodic(uhci_ports_check, controller, 500);
}

/* Initialises Queue Heads & Interrupt Queeue */
void uhci_init_queues(uhci_controller_t *controller)
{
	uint32_t *fl_ptr = (uint32_t*)controller->frame_list;
	addr_t buffer_address = 0, buffer_address_max = 0;
	addr_t pool = 0, pool_phys = 0;
	uint32_t i;

	/* Setup Pools */
	controller->td_index = 1;
	controller->qh_index = 11;
	
	/* Buffer Iterator */
	buffer_address = (addr_t)kmalloc_a(0x1000);
	buffer_address_max = buffer_address + 0x1000 - 1;

	/* Allocate one large block of cake for TD pool */
	pool = (addr_t)kmalloc((sizeof(uhci_transfer_desc_t) * X86_UHCI_POOL_NUM_TD) + X86_UHCI_STRUCT_ALIGN);
	pool = uhci_align(pool, X86_UHCI_STRUCT_ALIGN_BITS, X86_UHCI_STRUCT_ALIGN);
	pool_phys = memory_getmap(NULL, pool);
	memset((void*)pool, 0, sizeof(uhci_transfer_desc_t) * X86_UHCI_POOL_NUM_TD);
	for (i = 0; i < X86_UHCI_POOL_NUM_TD; i++)
	{
		/* Allocate TD */
		controller->td_pool[i] = (uhci_transfer_desc_t*)pool;
		controller->td_pool_phys[i] = pool_phys;

		/* Allocate Buffer */

		/* Allocate another page? */
		if (buffer_address > buffer_address_max)
		{
			buffer_address = (addr_t)kmalloc_a(0x1000);
			buffer_address_max = buffer_address + 0x1000 - 1;
		}

		/* Bind it to the new TD */
		controller->td_pool_buffers[i] = (addr_t*)buffer_address;
		controller->td_pool[i]->buffer = memory_getmap(NULL, buffer_address);
		
		/* Increase */
		buffer_address += 0x200;
		pool += sizeof(uhci_transfer_desc_t);
		pool_phys += sizeof(uhci_transfer_desc_t);
	}

	/* Now its time for QH */
	pool = (addr_t)kmalloc((sizeof(uhci_queue_head_t) * X86_UHCI_POOL_NUM_QH) + X86_UHCI_STRUCT_ALIGN);
	pool = uhci_align(pool, X86_UHCI_STRUCT_ALIGN_BITS, X86_UHCI_STRUCT_ALIGN);
	pool_phys = memory_getmap(NULL, pool);
	memset((void*)pool, 0, sizeof(uhci_queue_head_t) * X86_UHCI_POOL_NUM_QH);
	for (i = 0; i < X86_UHCI_POOL_NUM_QH; i++)
	{
		/* Set QH */
		controller->qh_pool[i] = (uhci_queue_head_t*)pool;
		controller->qh_pool_phys[i] = pool_phys;

		/* Set its index */
		controller->qh_pool[i]->flags = X86_UHCI_QH_INDEX(i);

		/* Increase */
		pool += sizeof(uhci_queue_head_t);
		pool_phys += sizeof(uhci_queue_head_t);
	}

	/* Setup interrupt queues */
	for (i = 2; i < X86_UHCI_POOL_ASYNC; i++)
	{
		/* Set QH Link */
		controller->qh_pool[i]->link_ptr = (controller->qh_pool_phys[X86_UHCI_POOL_ASYNC] | X86_UHCI_TD_LINK_QH);
		controller->qh_pool[i]->link = (uint32_t)controller->qh_pool[X86_UHCI_POOL_ASYNC];

		/* Disable TD List */
		controller->qh_pool[i]->head_ptr = X86_UHCI_TD_LINK_INVALID;
		controller->qh_pool[i]->head = 0;

		/* Set in use */
		controller->qh_pool[i]->flags |= (X86_UHCI_QH_SET_POOL_NUM(i) | X86_UHCI_QH_ACTIVE);
	}
	
	/* Setup Iso Qh */

	/* Setup async Qh */
	controller->qh_pool[X86_UHCI_POOL_ASYNC]->link_ptr = X86_UHCI_TD_LINK_INVALID;
	controller->qh_pool[X86_UHCI_POOL_ASYNC]->link = 0;
	controller->qh_pool[X86_UHCI_POOL_ASYNC]->head_ptr = controller->td_pool_phys[X86_UHCI_POOL_NULL_TD];
	controller->qh_pool[X86_UHCI_POOL_ASYNC]->head = (uint32_t)controller->td_pool[X86_UHCI_POOL_NULL_TD];

	/* Setup null QH */
	controller->qh_pool[X86_UHCI_POOL_NULL]->link_ptr = (controller->qh_pool_phys[X86_UHCI_POOL_NULL] | X86_UHCI_TD_LINK_QH);
	controller->qh_pool[X86_UHCI_POOL_NULL]->link = (uint32_t)controller->qh_pool[X86_UHCI_POOL_NULL];
	controller->qh_pool[X86_UHCI_POOL_NULL]->head_ptr = controller->td_pool_phys[X86_UHCI_POOL_NULL_TD];
	controller->qh_pool[X86_UHCI_POOL_NULL]->head = (uint32_t)controller->td_pool[X86_UHCI_POOL_NULL_TD];

	/* Make sure they have a NULL td */
	controller->td_pool[X86_UHCI_POOL_NULL_TD]->control = 0;
	controller->td_pool[X86_UHCI_POOL_NULL_TD]->header = 
		(uint32_t)(X86_UHCI_TD_PID_IN | X86_UHCI_TD_DEVICE_ADDR(0x7F) | X86_UHCI_TD_MAX_LEN(0x7FF));
	controller->td_pool[X86_UHCI_POOL_NULL_TD]->link_ptr = X86_UHCI_TD_LINK_INVALID;

	/* 1024 Entries 
	 * Set all entries to the 8 interrupt queues, and we 
	 * want them interleaved such that some queues get visited more than others x*/
	for (i = 0; i < X86_UHCI_NUM_FRAMES; i++)
		fl_ptr[i] = uhci_determine_intqh(controller, i);

	/* Init transaction list */
	controller->transactions_list = list_create(LIST_SAFE);
}

/* Ports */
void uhci_port_reset(uhci_controller_t *controller, int port, int noint)
{
	uint16_t temp, i;
	uint16_t offset = (X86_UHCI_REGISTER_PORT_BASE + ((uint16_t)port * 2));
	_CRT_UNUSED(noint);

	/* Step 1. Send reset signal */
	temp = uhci_read16(controller, offset) & 0xFFF5;
	uhci_write16(controller, offset, temp | X86_UHCI_PORT_RESET);

	/* Wait atlest 50 ms (per USB specification) */
	clock_stall(60);

	/* Now deassert reset signal */
	temp = uhci_read16(controller, offset) & 0xFFF5;
	uhci_write16(controller, offset, temp & ~X86_UHCI_PORT_RESET);

	/* Recovery Wait */
	clock_stall(10);

	/* Step 2. Enable Port */
	temp = uhci_read16(controller, offset) & 0xFFF5;
	uhci_write16(controller, offset, temp | X86_UHCI_PORT_ENABLED);

	/* Wait for enable, with timeout */
	i = 0;
	while (i < 10)
	{
		/* Increase */
		i++;

		/* Stall */
		clock_stall(12);

		/* Check status */
		temp = uhci_read16(controller, offset);

		/* Is device still connected? */
		if (!(temp & X86_UHCI_PORT_CONNECT_STATUS))
			return;

		/* Has it raised any event bits? In that case clear'em */
		if (temp & (X86_UHCI_PORT_CONNECT_EVENT | X86_UHCI_PORT_ENABLED_EVENT))
		{
			uhci_write16(controller, offset, (temp & 0xFFF5) | (X86_UHCI_PORT_CONNECT_EVENT | X86_UHCI_PORT_ENABLED_EVENT));
			continue;
		}

		/* Done? */
		if (temp & X86_UHCI_PORT_ENABLED)
			break;
	}

	/* Sanity */
	if (i == 1000)
	{
		printf("UHCI: Port Reset Failed!\n");
		return;
	}
}

/* Detect any port changes */
void uhci_port_check(uhci_controller_t *controller, int port)
{
	uint16_t pstatus = uhci_read16(controller, (X86_UHCI_REGISTER_PORT_BASE + ((uint16_t)port * 2)));
	usb_hc_t *hc;

	/* Is device connected? :/ */
	if (!(pstatus & X86_UHCI_PORT_CONNECT_STATUS))
		return;

	/* Clear bits asap */
	if ((pstatus & X86_UHCI_PORT_CONNECT_EVENT)
		|| (pstatus & X86_UHCI_PORT_ENABLED_EVENT)
		|| (pstatus & X86_UHCI_PORT_RESUME_DETECT))
		uhci_write16(controller, (X86_UHCI_REGISTER_PORT_BASE + ((uint16_t)port * 2)), pstatus);

	/* Get HCD data */
	hc = usb_get_hcd(controller->hcd_id);

	/* Sanity */
	if (hc == NULL)
		return;

	/* Any connection changes? */
	if (pstatus & X86_UHCI_PORT_CONNECT_EVENT)
	{
		/* Connect event? */
		if (pstatus & X86_UHCI_PORT_CONNECT_STATUS)
		{
			/* Connection Event */
			usb_event_create(hc, port, X86_USB_EVENT_CONNECTED);
		}
		else
		{
			/* Disconnect Event */
			usb_event_create(hc, port, X86_USB_EVENT_DISCONNECTED);
		}
	}
}

/* Go through ports */
void uhci_ports_check(void *data)
{
	int i;
	uhci_controller_t *controller = (uhci_controller_t*)data;

	for (i = 0; i < (int)controller->ports; i++)
		uhci_port_check(controller, i);
}

/* Gets port status */
void uhci_port_setup(void *data, usb_hc_port_t *port)
{
	uhci_controller_t *controller = (uhci_controller_t*)data;
	uint16_t pstatus = 0;

	/* Reset Port */
	uhci_port_reset(controller, (int)port->id, 0);

	/* Dump info */
	pstatus = uhci_read16(controller, (X86_UHCI_REGISTER_PORT_BASE + ((uint16_t)port->id * 2)));
	printf("UHCI %u.%u Status: 0x%x\n", controller->id, port->id, pstatus);

	/* Is it connected? */
	if (pstatus & X86_UHCI_PORT_CONNECT_STATUS)
		port->connected = 1;
	else
		port->connected = 0;

	/* Enabled? */
	if (pstatus & X86_UHCI_PORT_ENABLED)
		port->enabled = 1;
	else
		port->enabled = 0;

	/* Lowspeed? */
	if (pstatus & X86_UHCI_PORT_LOWSPEED)
		port->full_speed = 0;
	else
		port->full_speed = 1;
}

/* QH Functions */
uint32_t uhci_get_qh(uhci_controller_t *controller)
{
	interrupt_status_t int_state;
	uint32_t index = 0;
	uhci_queue_head_t *qh;

	/* Pick a QH */
	int_state = interrupt_disable();
	spinlock_acquire(&controller->lock);

	/* Grap it, locked operation */
	while (!index)
	{
		qh = controller->qh_pool[controller->qh_index];

		if (!(qh->flags & X86_UHCI_QH_ACTIVE))
		{
			/* Done! */
			index = controller->qh_index;
			qh->flags |= X86_UHCI_QH_ACTIVE;
		}
		
		controller->qh_index++;

		/* Index Sanity */
		if (controller->qh_index == X86_UHCI_POOL_NUM_QH)
			controller->qh_index = 11;
	}

	/* Release lock */
	spinlock_release(&controller->lock);
	interrupt_set_state(int_state);

	return index;
}

/* TD Functions */
uint32_t uhci_get_td(uhci_controller_t *controller)
{
	interrupt_status_t int_state;
	uint32_t index = 0;
	uhci_transfer_desc_t *td;

	/* Pick a QH */
	int_state = interrupt_disable();
	spinlock_acquire(&controller->lock);

	/* Grap it, locked operation */
	while (!index)
	{
		td = controller->td_pool[controller->td_index];

		if (!(td->control & X86_UHCI_TD_CTRL_ACTIVE))
		{
			/* Done! */
			index = controller->td_index;
		}

		controller->td_index++;

		/* Index Sanity */
		if (controller->td_index == X86_UHCI_POOL_NUM_TD)
			controller->td_index = 1;
	}

	/* Release lock */
	spinlock_release(&controller->lock);
	interrupt_set_state(int_state);

	return index;
}

/* Setup TD */
uhci_transfer_desc_t *uhci_td_setup(uhci_controller_t *controller, addr_t next_td,
	uint32_t lowspeed, uint32_t device_addr, uint32_t ep_addr, uint32_t toggle, uint8_t request_direction,
	uint8_t request_type, uint8_t request_value_low, uint8_t request_value_high, uint16_t request_index,
	uint16_t request_length, void **td_buffer)
{
	uint32_t td_index;
	void *buffer;
	uhci_transfer_desc_t *td;
	addr_t td_phys;
	usb_packet_t *packet;

	/* Start out by grapping a TD */
	td_index = uhci_get_td(controller);
	buffer = controller->td_pool_buffers[td_index];
	td = controller->td_pool[td_index];
	td_phys = controller->td_pool_phys[td_index];

	/* Set link to next TD */
	if (next_td == X86_UHCI_TD_LINK_INVALID)
		td->link_ptr = X86_UHCI_TD_LINK_INVALID;
	else
		td->link_ptr = memory_getmap(NULL, (virtaddr_t)next_td);

	/* Setup TD Control Status */
	td->control = 0;
	td->control |= X86_UHCI_TD_CTRL_ACTIVE;
	td->control |= X86_UHCI_TD_SET_ERR_CNT(3);

	if (lowspeed)
		td->control |= X86_UHCI_TD_LOWSPEED;

	/* Setup TD Header Packet */
	td->header = X86_UHCI_TD_PID_SETUP;
	td->header |= X86_UHCI_TD_DEVICE_ADDR(device_addr);
	td->header |= X86_UHCI_TD_EP_ADDR(ep_addr);
	td->header |= X86_UHCI_TD_DATA_TOGGLE(toggle);
	td->header |= X86_UHCI_TD_MAX_LEN((sizeof(usb_packet_t) - 1));

	/* Setup SETUP packet */
	*td_buffer = buffer;
	packet = (usb_packet_t*)buffer;
	packet->direction = request_direction;
	packet->type = request_type;
	packet->value_low = request_value_low;
	packet->value_high = request_value_high;
	packet->index = request_index;
	packet->length = request_length;

	/* Set buffer */
	td->buffer = memory_getmap(NULL, (virtaddr_t)buffer);

	/* Done */
	return td;
}

/* In/Out TD */
uhci_transfer_desc_t *uhci_td_io(uhci_controller_t *controller, addr_t next_td,
	uint32_t lowspeed, uint32_t device_addr, uint32_t ep_addr, uint32_t toggle, 
	uint32_t pid, uint32_t length, void **td_buffer)
{
	uint32_t td_index;
	void *buffer;
	uhci_transfer_desc_t *td;
	addr_t td_phys;

	/* Start out by grapping a TD */
	td_index = uhci_get_td(controller);
	buffer = controller->td_pool_buffers[td_index];
	td = controller->td_pool[td_index];
	td_phys = controller->td_pool_phys[td_index];

	/* Set link to next TD */
	if (next_td == X86_UHCI_TD_LINK_INVALID)
		td->link_ptr = X86_UHCI_TD_LINK_INVALID;
	else
		td->link_ptr = memory_getmap(NULL, (virtaddr_t)next_td);

	/* Setup TD Control Status */
	td->control = 0;
	td->control |= X86_UHCI_TD_CTRL_ACTIVE;
	td->control |= X86_UHCI_TD_SET_ERR_CNT(3);

	if (lowspeed)
		td->control |= X86_UHCI_TD_LOWSPEED;

	/* Setup TD Header Packet */
	td->header = pid;
	td->header |= X86_UHCI_TD_DEVICE_ADDR(device_addr);
	td->header |= X86_UHCI_TD_EP_ADDR(ep_addr);
	td->header |= X86_UHCI_TD_DATA_TOGGLE(toggle);

	if (length > 0)
		td->header |= X86_UHCI_TD_MAX_LEN((length - 1));
	else
		td->header |= X86_UHCI_TD_MAX_LEN(0x7FF);

	/* Set buffer */
	*td_buffer = buffer;
	td->buffer = memory_getmap(NULL, (virtaddr_t)buffer);

	/* Done */
	return td;
}

/* Transaction Functions */
void uhci_transaction_init(void *controller, usb_hc_request_t *request)
{	
	uhci_controller_t *ctrl = (uhci_controller_t*)controller;
	uint32_t qh_index = 0;

	/* Get a QH */
	qh_index = uhci_get_qh(ctrl);
	request->data = (void*)ctrl->qh_pool[qh_index];

	/* Setup some of the QH metadata */
	ctrl->qh_pool[qh_index]->head = 0;
	ctrl->qh_pool[qh_index]->head_ptr = 0;
	ctrl->qh_pool[qh_index]->link = 0;
	ctrl->qh_pool[qh_index]->link_ptr = 0;
	ctrl->qh_pool[qh_index]->hcd_data = 0;

	/* Set as not completed for start */
	request->completed = 0;
}

usb_hc_transaction_t *uhci_transaction_setup(void *controller, usb_hc_request_t *request)
{
	uhci_controller_t *ctrl = (uhci_controller_t*)controller;
	usb_hc_transaction_t *transaction;

	/* Allocate transaction */
	transaction = (usb_hc_transaction_t*)kmalloc(sizeof(usb_hc_transaction_t));
	transaction->io_buffer = 0;
	transaction->io_length = 0;
	transaction->link = NULL;

	/* Create a setup TD */
	transaction->transfer_descriptor = (void*)uhci_td_setup(ctrl, X86_UHCI_TD_LINK_INVALID, request->lowspeed,
		request->device->address, request->endpoint, request->toggle, request->packet.direction, request->packet.type,
		request->packet.value_low, request->packet.value_high, request->packet.index, request->packet.length,
		&transaction->transfer_buffer);

	/* If previous transaction */
	if (request->transactions != NULL)
	{
		uhci_transfer_desc_t *prev_td;
		usb_hc_transaction_t *ctrans = request->transactions;

		while (ctrans->link)
			ctrans = ctrans->link;

		prev_td = (uhci_transfer_desc_t*)ctrans->transfer_descriptor;
		prev_td->link_ptr = (memory_getmap(NULL, (virtaddr_t)transaction->transfer_descriptor) | X86_UHCI_TD_LINK_DEPTH);
	}

	return transaction;
}

usb_hc_transaction_t *uhci_transaction_in(void *controller, usb_hc_request_t *request)
{
	uhci_controller_t *ctrl = (uhci_controller_t*)controller;
	usb_hc_transaction_t *transaction;

	/* Allocate transaction */
	transaction = (usb_hc_transaction_t*)kmalloc(sizeof(usb_hc_transaction_t));
	transaction->io_buffer = request->io_buffer;
	transaction->io_length = request->io_length;
	transaction->link = NULL;

	/* Create a In TD */
	transaction->transfer_descriptor = (void*)uhci_td_io(ctrl, X86_UHCI_TD_LINK_INVALID, request->lowspeed,
		request->device->address, request->endpoint, request->toggle, X86_UHCI_TD_PID_IN, request->io_length,
		&transaction->transfer_buffer);

	/* If previous transaction */
	if (request->transactions != NULL)
	{
		uhci_transfer_desc_t *prev_td;
		usb_hc_transaction_t *ctrans = request->transactions;

		while (ctrans->link)
			ctrans = ctrans->link;

		prev_td = (uhci_transfer_desc_t*)ctrans->transfer_descriptor;
		prev_td->link_ptr = (memory_getmap(NULL, (virtaddr_t)transaction->transfer_descriptor) | X86_UHCI_TD_LINK_DEPTH);
	}

	return transaction;
}

usb_hc_transaction_t *uhci_transaction_out(void *controller, usb_hc_request_t *request)
{
	uhci_controller_t *ctrl = (uhci_controller_t*)controller;
	usb_hc_transaction_t *transaction;

	/* Allocate transaction */
	transaction = (usb_hc_transaction_t*)kmalloc(sizeof(usb_hc_transaction_t));
	transaction->io_buffer = 0;
	transaction->io_length = 0;
	transaction->link = NULL;

	/* Create a In TD */
	transaction->transfer_descriptor = (void*)uhci_td_io(ctrl, X86_UHCI_TD_LINK_INVALID, request->lowspeed,
		request->device->address, request->endpoint, request->toggle, X86_UHCI_TD_PID_OUT, request->io_length,
		&transaction->transfer_buffer);

	/* Copy Data */
	if (request->io_buffer != NULL && request->io_length != 0)
		memcpy(transaction->transfer_buffer, request->io_buffer, request->io_length);

	/* If previous transaction */
	if (request->transactions != NULL)
	{
		uhci_transfer_desc_t *prev_td;
		usb_hc_transaction_t *ctrans = request->transactions;

		while (ctrans->link)
			ctrans = ctrans->link;

		prev_td = (uhci_transfer_desc_t*)ctrans->transfer_descriptor;
		prev_td->link_ptr = (memory_getmap(NULL, (virtaddr_t)transaction->transfer_descriptor) | X86_UHCI_TD_LINK_DEPTH);
	}

	return transaction;
}

void uhci_transaction_send(void *controller, usb_hc_request_t *request)
{
	/* Wuhu */
	usb_hc_transaction_t *transaction = request->transactions;
	uhci_controller_t *ctrl = (uhci_controller_t*)controller;
	uhci_transfer_desc_t *td = NULL;
	uhci_queue_head_t *qh = NULL, *parent_qh = NULL;
	int completed = 1;
	addr_t qh_address;

	/* Get physical */
	qh_address = memory_getmap(NULL, (virtaddr_t)request->data);

	/* Set as not completed for start */
	request->completed = 0;

	/* Initialize QH */
	qh = (uhci_queue_head_t*)request->data;
	
	/* Set TD List */
	qh->head_ptr = memory_getmap(NULL, (virtaddr_t)transaction->transfer_descriptor);
	qh->head = (uint32_t)transaction->transfer_descriptor;

	/* Set next link to NULL, we insert it as tail :-) */
	qh->link_ptr = X86_UHCI_TD_LINK_INVALID;
	qh->link = 0;

	/* Set HCD data */
	qh->hcd_data = (uint32_t)request->transactions;

	/* Get spinlock */
	spinlock_acquire(&ctrl->lock);

	/* Debug */
	printf("QH at 0x%x, Head 0x%x, Link 0x%x\n", (addr_t)qh, qh->head_ptr, qh->link_ptr);

	/* Set last TD to produce an interrupt */
	transaction = request->transactions;
	while (transaction)
	{
		/* Get TD */
		td = (uhci_transfer_desc_t*)transaction->transfer_descriptor;

		/* If this is last, we set it to IOC */
		if (transaction->link == NULL)
			td->control |= X86_UHCI_TD_IOC;

		/* Debug */
		printf("TD at 0x%x, Control 0x%x, Header 0x%x, Buffer 0x%x, Link 0x%x\n",
			(addr_t)td, td->control, td->header, td->buffer, td->link_ptr);
		
		/* Next Link */
		transaction = transaction->link;
	}

	/* Stop the controller, we are going to modify the frame-list */
	uhci_stop(ctrl);

	/* Update the QH List */
	/* Link this to the async list */
	parent_qh = ctrl->qh_pool[X86_UHCI_POOL_ASYNC];

	/* Should not take too long this loop */
	while (parent_qh->link != 0)
		parent_qh = (uhci_queue_head_t*)parent_qh->link;

	/* Now insert us at the tail */
	parent_qh->link_ptr = (qh_address | X86_UHCI_TD_LINK_QH);
	parent_qh->link = (uint32_t)qh;

	/* Add our transaction :-) */
	list_append(ctrl->transactions_list, list_create_node(0, request->data));

	/* Wait for interrupt */
	//scheduler_sleep_thread((addr_t*)request->data);

	/* Start controller */
	uhci_start(ctrl);

	/* Release lock */
	spinlock_release(&ctrl->lock);

	/* Yield */
	_yield();

	printf("Heya! Got woken up!\n");

	/* Check Conditions (WithOUT dummy) */
	transaction = request->transactions;
	while (transaction)
	{
		td = (uhci_transfer_desc_t*)transaction->transfer_descriptor;

		/* Debug */
		printf("TD at 0x%x, Control 0x%x, Header 0x%x, Buffer 0x%x, Link 0x%x\n",
			(addr_t)td, td->control, td->header, td->buffer, td->link_ptr);
		
		/* Error? :s */
		if (X86_UHCI_TD_STATUS(td->control))
		{
			completed = 0;
			break;
		}

		transaction = transaction->link;
	}

	/* Lets see... */
	if (completed)
	{
		/* Build Buffer */
		transaction = request->transactions;

		while (transaction)
		{
			/* Copy Data? */
			if (transaction->io_buffer != NULL && transaction->io_length != 0)
			{
				printf("Buffer Copy 0x%x, Length 0x%x\n", transaction->io_buffer, transaction->io_length);
				memcpy(transaction->io_buffer, transaction->transfer_buffer, transaction->io_length);
			}

			/* Next Link */
			transaction = transaction->link;
		}

		/* Set as completed */
		request->completed = 1;
	}
}

void uhci_install_interrupt(void *controller, usb_hc_device_t *device, usb_hc_endpoint_t *endpoint,
	void *in_buffer, size_t in_bytes, void(*callback)(void*, size_t), void *arg)
{
	_CRT_UNUSED(controller);
	_CRT_UNUSED(device);
	_CRT_UNUSED(endpoint);
	_CRT_UNUSED(in_buffer);
	_CRT_UNUSED(in_bytes);
	_CRT_UNUSED(callback);
	_CRT_UNUSED(arg);
}

/* Interrupt Handler */
int uhci_interrupt_handler(void *args)
{
	uint16_t intr_state = 0;
	uhci_controller_t *controller = (uhci_controller_t*)args;

	/* Get INTR state */
	intr_state = uhci_read16(controller, X86_UHCI_REGISTER_STATUS);
	
	/* Did this one come from us? */
	if (!(intr_state & 0x1F))
		return X86_IRQ_NOT_HANDLED;

	/* Debug */
	printf("UHCI_INTERRUPT Controller %u: 0x%x\n", controller->id, intr_state);

	/* Clear Interrupt Bits :-) */
	uhci_write16(controller, X86_UHCI_REGISTER_STATUS, intr_state);

	/* Sanity */
	if (controller->initialized == 0)
	{
		/* Bleh */
		return X86_IRQ_HANDLED;
	}

	/* So.. */
	if (intr_state & (X86_UHCI_STATUS_USBINT | X86_UHCI_STATUS_INTR_ERROR))
	{
		/* Transaction is completed / Failed */
		list_t *transaction_list = (list_t*)controller->transactions_list;
		uhci_queue_head_t *qh;
		list_node_t *ta;
		int n = 0;

		/* Get transactions in progress and find the offender */
		ta = list_get_node_by_id(transaction_list, 0, n);
		while (ta != NULL)
		{
			usb_hc_transaction_t *transactions;
			uint32_t completed = 1;
			
			/* Get transactions linked to his QH */
			qh = (uhci_queue_head_t*)ta->data;
			transactions = (usb_hc_transaction_t*)qh->hcd_data;

			/* Loop through transactions */
			while (transactions && completed != 0)
			{
				uhci_transfer_desc_t *td;

				/* Get transfer descriptor */
				td = (uhci_transfer_desc_t*)transactions->transfer_descriptor;

				/* Check status */
				if (td->control & X86_UHCI_TD_CTRL_ACTIVE)
				{
					/* If its still active this can't possibly be the transfer */
					completed = 0;
					break;
				}

				/* Error Transfer ? */
				if ((X86_UHCI_TD_ERROR_COUNT(td->control) == 0 && X86_UHCI_TD_STATUS(td->control))
					|| (intr_state & X86_UHCI_STATUS_INTR_ERROR))
				{
					/* Error */
					printf("Interrupt ERROR: Td Control 0x%x, Header 0x%x\n", td->control, td->header);
				}

				/* Get next transaction */
				transactions = transactions->link;
			}

			/* Was it a completed transaction ? ? */
			if (completed)
			{
				/* Wuhuuu... */

				/* Mark EP Descriptor as free SEHR IMPORTANTE */
				qh->flags &= ~(X86_UHCI_QH_ACTIVE);

				/* Wake a node */
				scheduler_wakeup_one((addr_t*)qh);

				/* Remove from list */
				list_remove_by_node(transaction_list, ta);

				/* Cleanup node */
				kfree(ta);

				/* Done */
				break;
			}

			/* Get next head */
			n++;
			ta = list_get_node_by_id(transaction_list, 0, n);
		}
	}

	/* Resume Detected */
	if (intr_state & X86_UHCI_STATUS_RESUME_DETECT)
	{
		/* Set controller to working state :/ */
		if (controller->initialized != 0)
		{
			uint16_t temp = (X86_UHCI_CMD_CF | X86_UHCI_CMD_RUN | X86_UHCI_CMD_MAXPACKET64);
			uhci_write16(controller, X86_UHCI_REGISTER_COMMAND, temp);
		}
	}

	/* Host Error */
	if (intr_state & X86_UHCI_STATUS_HOST_SYSERR)
	{
		/* Reset Controller */
	}

	/* TD Processing Error */
	if (intr_state & X86_UHCI_STATUS_PROCESS_ERR)
	{
		/* Fatal Error 
		 * Unschedule TDs and restart controller */
		printf("UHCI: Processing Error :/ \n");
	}

	return X86_IRQ_HANDLED;
}
