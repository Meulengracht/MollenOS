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

/* Globals */
volatile uint32_t glb_uhci_id = 0;

/* Externs */

/* Prototypes (Internal) */
void uhci_init_queues(uhci_controller_t *controller);
void uhci_setup(uhci_controller_t *controller);
void uhci_interrupt_handler(void *args);

void uhci_port_reset(uhci_controller_t *controller, int port, int noint);
void uhci_port_check(uhci_controller_t *controller, int port);
void uhci_ports_check(void *data);

void uhci_port_status(void *data, usb_hc_port_t *port);

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

/* Function Allocates Resources
* and starts a init thread */
void uhci_init(pci_driver_t *device)
{
	uint16_t pci_command;
	uhci_controller_t *controller = NULL;

	/* Allocate Resources for this controller */
	controller = (uhci_controller_t*)kmalloc(sizeof(uhci_controller_t));
	controller->pci_info = device;
	controller->id = glb_uhci_id;
	spinlock_reset(&controller->lock);
	glb_uhci_id++;

	/* Enable memory and bus mastering */
	pci_command = pci_read_word((const uint16_t)device->bus, (const uint16_t)device->device, (const uint16_t)device->function, 0x4);
	pci_write_word((const uint16_t)device->bus, (const uint16_t)device->device, (const uint16_t)device->function, 0x4, pci_command | 0x6);

	/* Disable Legacy Emulation & Interrupts */
	pci_write_word((const uint16_t)device->bus, (const uint16_t)device->device, (const uint16_t)device->function, 0xC0, 0xF800);

	/* Get I/O Base from Bar4 */
	controller->io_base = (device->header->bar4 & 0x0000FFFE);

	/* Install IRQ Handler */
	interrupt_install_pci(device, uhci_interrupt_handler, controller);

	/* Get DMA */
	controller->frame_list_phys = physmem_alloc_block_dma();
	controller->frame_list = (void*)controller->frame_list_phys;

	/* Memset */
	memset(controller->frame_list, 0, 0x1000);

	/* Debug */
	printf("UHCI - Id %u, Bar4: 0x%x, Dma: 0x%x\n", controller->id, controller->io_base, controller->frame_list_phys);

	/* Reset Controller */
	uhci_setup(controller);
}

void uhci_setup(uhci_controller_t *controller)
{
	usb_hc_t *hc;
	uint16_t temp = 0, i = 0;

	/* Step 1. Stop the controller (if its on because of usb boot etc) 
	 * We also clear the CF flag */
	temp = uhci_read16(controller, X86_UHCI_REGISTER_COMMAND);
	temp &= ~(X86_UHCI_CMD_CF | X86_UHCI_CMD_RUN);
	uhci_write16(controller, X86_UHCI_REGISTER_COMMAND, temp);

	/* Wait for it to stop */
	while (!(uhci_read16(controller, X86_UHCI_REGISTER_STATUS) & X86_UHCI_STATUS_HALTED))
		clock_stall(20);

	/* Clear status */
	uhci_write16(controller, X86_UHCI_REGISTER_STATUS, 
		uhci_read16(controller, X86_UHCI_REGISTER_STATUS));

	/* Initialise Queues */
	uhci_init_queues(controller);

	/* Reset Controller */
	temp = uhci_read16(controller, X86_UHCI_REGISTER_COMMAND);
	temp |= X86_UHCI_CMD_HCRESET;
	uhci_write16(controller, X86_UHCI_REGISTER_COMMAND, temp);

	/* Wait for reset */
	while (uhci_read16(controller, X86_UHCI_REGISTER_COMMAND) & X86_UHCI_CMD_HCRESET)
		clock_stall(20); 

	/* Setup Framelist */
	uhci_write32(controller, X86_UHCI_REGISTER_FRBASEADDR, controller->frame_list_phys);
	uhci_write16(controller, X86_UHCI_REGISTER_FRNUM, 0);
	uhci_write8(controller, X86_UHCI_REGISTER_SOFMOD, 64); /* Frame Length 1 ms */

	/* Enable interrupts */
	temp = (X86_UHCI_INTR_TIMEOUT | X86_UHCI_INTR_SHORT_PACKET
		| X86_UHCI_INTR_RESUME | X86_UHCI_INTR_COMPLETION);
	uhci_write16(controller, X86_UHCI_REGISTER_INTR, temp);
	pci_write_word((const uint16_t)controller->pci_info->bus, 
					(const uint16_t)controller->pci_info->device, 
					(const uint16_t)controller->pci_info->function, 
					0xC0, 0x2000);

	/* Start controller & force a global resume */
	temp = (X86_UHCI_CMD_CF | X86_UHCI_CMD_RUN | X86_UHCI_CMD_FGR | X86_UHCI_CMD_MAXPACKET64);
	uhci_write16(controller, X86_UHCI_REGISTER_COMMAND, temp);

	/* Wait 20 ms for it to happen, then we start it for real
	 * we must set the global resume to 0 after 20 ms */
	clock_stall(20);

	/* Now start and wait */
	temp = (X86_UHCI_CMD_CF | X86_UHCI_CMD_RUN | X86_UHCI_CMD_MAXPACKET64);
	uhci_write16(controller, X86_UHCI_REGISTER_COMMAND, temp);

	/* Wait */
	while (uhci_read16(controller, X86_UHCI_REGISTER_STATUS) & X86_UHCI_STATUS_HALTED)
		clock_stall(10);

	/* Further delay, wait for ports to stabilize */
	clock_stall(100);

	/* We first get port count */
	for (i = 0; i < 7; i++)
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
	}

	/* Ports are now i */
	controller->ports = i;

	printf("UHCI: Port Count %u\n", controller->ports);

	/* Setup HCD */
	hc = usb_init_controller((void*)controller, X86_USB_TYPE_UHCI, controller->ports);

	/* Port Functions */
	hc->root_hub_check = uhci_ports_check;
	hc->port_status = uhci_port_status;

	/* Transaction Functions */
	hc->transaction_init = uhci_transaction_init;
	hc->transaction_setup = uhci_transaction_setup;
	hc->transaction_in = uhci_transaction_in;
	hc->transaction_out = uhci_transaction_out;
	hc->transaction_send = uhci_transaction_send;
	hc->install_interrupt = uhci_install_interrupt;

	controller->hcd_id = usb_register_controller(hc);

	/* Do a HUB Check, it will setup ports */
	usb_event_create(hc, 0, X86_USB_EVENT_ROOTHUB_CHECK);

	/* Install Periodic Check (WTF NO HUB INTERRUPTS!?) */
	timers_create_periodic(uhci_ports_check, controller, 10);
}

/* Initialises Queue Heads & Interrupt Queeue */
void uhci_init_queues(uhci_controller_t *controller)
{
	uint32_t *fl_ptr = (uint32_t*)controller->frame_list;
	addr_t buffer_address = 0, buffer_address_max = 0;
	addr_t pool = 0, pool_phys = 0;
	uint32_t i;

	/* Setup Pools */
	controller->td_index = 0;
	controller->qh_index = 1;
	
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
		controller->qh_pool[i]->qh_index = i;

		/* Increase */
		pool += sizeof(uhci_queue_head_t);
		pool_phys += sizeof(uhci_queue_head_t);
	}

	/* Setup first QH */
	controller->qh_pool[0]->head_ptr = 0x1;
	controller->qh_pool[0]->link_ptr = (controller->qh_pool_phys[0] | 0x2);

	/* 1024 Entries 
	 * Set all entries to the first QH */
	for (i = 0; i < 1024; i++)
		fl_ptr[i] = (controller->qh_pool_phys[0] | 0x2);

	/* Init transaction list */
	controller->transactions_list = list_create(LIST_SAFE);
}

/* Ports */
void uhci_port_reset(uhci_controller_t *controller, int port, int noint)
{
	uint16_t temp, i;
	_CRT_UNUSED(noint);

	/* Step 1. Reset Port for 50 ms */
	uhci_write16(controller, (X86_UHCI_REGISTER_PORT_BASE + ((uint16_t)port * 2)), X86_UHCI_PORT_RESET);

	/* Wait 50 ms (per USB specification) */
	clock_stall(50);

	/* Now deassert reset signal */
	uhci_write16(controller, (X86_UHCI_REGISTER_PORT_BASE + ((uint16_t)port * 2)), 0);

	/* Recovery Wait */
	clock_stall(4);

	/* Step 2. Enable Port */
	temp = uhci_read16(controller, (X86_UHCI_REGISTER_PORT_BASE + ((uint16_t)port * 2)));
	temp |= X86_UHCI_PORT_ENABLED;
	uhci_write16(controller, (X86_UHCI_REGISTER_PORT_BASE + ((uint16_t)port * 2)), temp);

	/* Give it 100 ms to stabilize */
	clock_stall(100);

	/* Wait for enable, with timeout */
	i = 0;
	while (i < 1000)
	{
		/* Check status */
		temp = uhci_read16(controller, (X86_UHCI_REGISTER_PORT_BASE + ((uint16_t)port * 2)));
		if (temp & X86_UHCI_PORT_ENABLED)
			break;

		/* Stall */
		clock_stall(10);

		/* Increase */
		i++;
	}

	/* Sanity */
	if (i == 1000)
	{
		printf("UHCI: Port Reset Failed!\n");
		return;
	}
	
	/* Clear Event Bits */
	temp = uhci_read16(controller, (X86_UHCI_REGISTER_PORT_BASE + ((uint16_t)port * 2)));
	temp |= (X86_UHCI_PORT_CONNECT_EVENT | X86_UHCI_PORT_ENABLED_EVENT);
	uhci_write16(controller, (X86_UHCI_REGISTER_PORT_BASE + ((uint16_t)port * 2)), temp);
}

/* Detect any port changes */
void uhci_port_check(uhci_controller_t *controller, int port)
{
	uint16_t pstatus = uhci_read16(controller, (X86_UHCI_REGISTER_PORT_BASE + ((uint16_t)port * 2)));
	usb_hc_t *hc;

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
void uhci_port_status(void *data, usb_hc_port_t *port)
{
	uhci_controller_t *controller = (uhci_controller_t*)data;
	uint16_t pstatus = uhci_read16(controller, (X86_UHCI_REGISTER_PORT_BASE + ((uint16_t)port->id * 2)));

	/* Connection Event */
	printf("UHCI: PrePort Status 0x%x\n", pstatus);

	/* Reset Port */
	uhci_port_reset(controller, (int)port->id, 0);

	pstatus = uhci_read16(controller, (X86_UHCI_REGISTER_PORT_BASE + ((uint16_t)port->id * 2)));
	printf("UHCI: PostPort Status: 0x%x\n", pstatus);

	/* Is it connected? */
	if (pstatus & X86_UHCI_PORT_CONNECT_STATUS)
		port->connected = 1;

	/* Enabled? */
	if (pstatus & X86_UHCI_PORT_ENABLED)
		port->enabled = 1;

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
			controller->qh_index = 1;
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
			controller->td_index = 0;
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
		prev_td->link_ptr = memory_getmap(NULL, (virtaddr_t)transaction->transfer_descriptor);
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
		request->device->address, request->endpoint, request->toggle, X86_UHCI_TD_PID_IN, request->length,
		&transaction->transfer_buffer);

	/* If previous transaction */
	if (request->transactions != NULL)
	{
		uhci_transfer_desc_t *prev_td;
		usb_hc_transaction_t *ctrans = request->transactions;

		while (ctrans->link)
			ctrans = ctrans->link;

		prev_td = (uhci_transfer_desc_t*)ctrans->transfer_descriptor;
		prev_td->link_ptr = memory_getmap(NULL, (virtaddr_t)transaction->transfer_descriptor);
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
		request->device->address, request->endpoint, request->toggle, X86_UHCI_TD_PID_OUT, request->length,
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
		prev_td->link_ptr = memory_getmap(NULL, (virtaddr_t)transaction->transfer_descriptor);
	}

	return transaction;
}

void uhci_transaction_send(void *controller, usb_hc_request_t *request)
{
	_CRT_UNUSED(controller);
	_CRT_UNUSED(request);
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
void uhci_interrupt_handler(void *args)
{
	_CRT_UNUSED(args);
}
