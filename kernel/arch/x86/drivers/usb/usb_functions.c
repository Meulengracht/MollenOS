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
* MollenOS X86-32 USB Functions Core Driver
*/

/* Includes */
#include <arch.h>
#include <drivers\usb\usb.h>
#include <heap.h>
#include <stddef.h>
#include <stdio.h>

/* Transaction List Functions */
void usb_transaction_append(usb_hc_request_t *request, usb_hc_transaction_t *transaction)
{
	if (request->transactions == NULL)
		request->transactions = transaction;
	else
	{
		usb_hc_transaction_t *head = request->transactions;

		/* Go to last element */
		while (head->link != NULL)
			head = head->link;

		/* Append */
		head->link = transaction;
		transaction->link = NULL;
	}
}

/* Transaction Wrappers */
void usb_transaction_init(usb_hc_t *hc, usb_hc_request_t *dev_request, uint32_t type,
	usb_hc_device_t *device, uint32_t endpoint, uint32_t max_length)
{
	/* Control - Endpoint 0 */
	dev_request->type = type;
	dev_request->data = NULL;
	dev_request->device = device;
	dev_request->length = MIN(device->endpoints[endpoint]->max_packet_size, max_length);
	dev_request->endpoint = device->endpoints[endpoint]->address;
	dev_request->transactions = NULL;

	/* Perform */
	hc->transaction_init(hc->hc, dev_request);
}

void usb_transaction_setup(usb_hc_t *hc, usb_hc_request_t *dev_request, uint32_t packet_size)
{
	usb_hc_transaction_t *transaction;

	/* Set toggle and token-bytes */
	dev_request->toggle = 0;
	dev_request->token_bytes = packet_size;

	/* Perform it */
	transaction = hc->transaction_setup(hc->hc, dev_request);

	/* Append it */
	transaction->type = X86_USB_TRANSACTION_SETUP;
	usb_transaction_append(dev_request, transaction);

	/* Toggle Goggle*/
	dev_request->device->endpoints[dev_request->endpoint]->toggle = 1;
}

void usb_transaction_in(usb_hc_t *hc, usb_hc_request_t *dev_request, uint32_t handshake, void *buffer, uint32_t length)
{
	/* Get length */
	usb_hc_transaction_t *transaction;
	uint32_t fixed_length = MIN(dev_request->length, length);
	uint32_t remaining_length = length - fixed_length;
	uint32_t transfers_left = remaining_length / dev_request->length;

	/* Fix transfers */
	if (remaining_length % dev_request->length)
		transfers_left++;

	/* Set request io buffer */
	dev_request->io_buffer = buffer;
	dev_request->io_length = fixed_length;

	if (handshake)
		dev_request->device->endpoints[dev_request->endpoint]->toggle = 1;

	/* Get toggle */
	dev_request->toggle = dev_request->device->endpoints[dev_request->endpoint]->toggle;

	/* Perform */
	transaction = hc->transaction_in(hc->hc, dev_request);

	/* Append Transaction */
	transaction->type = X86_USB_TRANSACTION_IN;
	usb_transaction_append(dev_request, transaction);

	/* Toggle Goggle */
	dev_request->device->endpoints[dev_request->endpoint]->toggle =
		(dev_request->device->endpoints[dev_request->endpoint]->toggle == 0) ? 1 : 0;

	if (transfers_left > 0)
		usb_transaction_in(hc, dev_request, 
		dev_request->device->endpoints[dev_request->endpoint]->toggle,
		(void*)((uint32_t)buffer + fixed_length), remaining_length);
}

void usb_transaction_out(usb_hc_t *hc, usb_hc_request_t *dev_request, uint32_t handshake, void *buffer, uint32_t length)
{
	/* Get length */
	usb_hc_transaction_t *transaction;
	uint32_t fixed_length = MIN(dev_request->length, length);
	uint32_t remaining_length = length - fixed_length;
	uint32_t transfers_left = remaining_length / dev_request->length;

	/* Fix transfers */
	if (remaining_length % dev_request->length)
		transfers_left++;

	/* Set request io buffer */
	dev_request->io_buffer = buffer;
	dev_request->io_length = fixed_length;

	if (handshake)
		dev_request->device->endpoints[dev_request->endpoint]->toggle = 1;

	/* Get toggle */
	dev_request->toggle = dev_request->device->endpoints[dev_request->endpoint]->toggle;

	/* Perform */
	transaction = hc->transaction_out(hc->hc, dev_request);

	/* Append Transaction */
	transaction->type = X86_USB_TRANSACTION_OUT;
	usb_transaction_append(dev_request, transaction);

	/* Toggle Goggle */
	dev_request->device->endpoints[dev_request->endpoint]->toggle =
		(dev_request->device->endpoints[dev_request->endpoint]->toggle == 0) ? 1 : 0;

	if (transfers_left > 0)
		usb_transaction_in(hc, dev_request,
		dev_request->device->endpoints[dev_request->endpoint]->toggle,
		(void*)((uint32_t)buffer + fixed_length), remaining_length);
}

void usb_transaction_send(usb_hc_t *hc, usb_hc_request_t *dev_request)
{
	usb_hc_transaction_t *transaction = dev_request->transactions, *next_transaction;

	/* Perform */
	hc->transaction_send(hc->hc, dev_request);

	/* Free List */
	while (transaction)
	{
		/* Get Next */
		next_transaction = transaction->link;

		/* Free */
		kfree(transaction);

		/* Set next */
		transaction = next_transaction;
	}
}

/* Set address of an usb device */
int usb_function_set_address(usb_hc_t *hc, int port, uint32_t address)
{
	usb_hc_request_t dev_request;

	/* Init transfer */
	usb_transaction_init(hc, &dev_request, X86_USB_REQUEST_TYPE_CONTROL,
		hc->ports[port]->device, 0, 64);

	/* Setup Packet */
	dev_request.lowspeed = (hc->ports[port]->full_speed == 0) ? 1 : 0;
	dev_request.packet.direction = 0;
	dev_request.packet.type = X86_USB_REQ_SET_ADDR;
	dev_request.packet.value_high = 0;
	dev_request.packet.value_low = (address & 0xFF);
	dev_request.packet.index = 0;
	dev_request.packet.length = 0;		/* We do not want data */

	/* Setup Transfer */
	usb_transaction_setup(hc, &dev_request, sizeof(usb_packet_t));

	/* ACK Transfer */
	usb_transaction_in(hc, &dev_request, 1, NULL, 0);

	/* Send it */
	usb_transaction_send(hc, &dev_request);

	/* Check if it completed */
	if (dev_request.completed)
		dev_request.device->address = address;

	return dev_request.completed;
}

/* Gets the device descriptor */
int usb_function_get_device_descriptor(usb_hc_t *hc, int port)
{
	int i;
	usb_device_descriptor_t dev_info;
	usb_hc_request_t dev_request;

	/* Init transfer */
	usb_transaction_init(hc, &dev_request, X86_USB_REQUEST_TYPE_CONTROL,
		hc->ports[port]->device, 0, 64);
	
	/* Setup Packet */
	dev_request.lowspeed = (hc->ports[port]->full_speed == 0) ? 1 : 0;
	dev_request.packet.direction = 0x80;
	dev_request.packet.type = X86_USB_REQ_GET_DESC;
	dev_request.packet.value_high = X86_USB_DESC_TYPE_DEVICE;
	dev_request.packet.value_low = 0;
	dev_request.packet.index = 0;
	dev_request.packet.length = 0x12;		/* Max Descriptor Length is 18 bytes */

	/* Setup Transfer */
	usb_transaction_setup(hc, &dev_request, sizeof(usb_packet_t));

	/* In Transfer, we want to fill the descriptor */
	usb_transaction_in(hc, &dev_request, 0, &dev_info, 0x12);

	/* Out Transfer, STATUS Stage */
	usb_transaction_out(hc, &dev_request, 1, NULL, 0);

	/* Send it */
	usb_transaction_send(hc, &dev_request);

	/* Update Device Information */
	if (dev_request.completed)
	{
		printf("USB Length 0x%x - Device Vendor Id & Product Id: 0x%x - 0x%x\n", dev_info.length, dev_info.vendor_id, dev_info.product_id);
		printf("Device Configurations 0x%x, Max Packet Size: 0x%x\n", dev_info.num_configurations, dev_info.max_packet_size);

		hc->ports[port]->device->class_code = dev_info.class_code;
		hc->ports[port]->device->subclass_code = dev_info.subclass_code;
		hc->ports[port]->device->protocol_code = dev_info.protocol_code;
		hc->ports[port]->device->vendor_id = dev_info.vendor_id;
		hc->ports[port]->device->product_id = dev_info.product_id;
		hc->ports[port]->device->str_index_manufactor = dev_info.str_index_manufactor;
		hc->ports[port]->device->str_index_product = dev_info.str_index_product;
		hc->ports[port]->device->str_index_sn = dev_info.str_index_serial_num;
		hc->ports[port]->device->num_configurations = dev_info.num_configurations;
		
		/* Set MPS */
		for (i = 0; i < (int)hc->ports[port]->device->num_endpoints; i++)
			hc->ports[port]->device->endpoints[i]->max_packet_size = dev_info.max_packet_size;
	}

	return dev_request.completed;
}

/* Gets the initial config descriptor */
int usb_function_get_initial_config_descriptor(usb_hc_t *hc, int port)
{
	usb_hc_request_t dev_request;
	usb_config_descriptor_t dev_config;

	/* Step 1. Get configuration descriptor */

	/* Init transfer */
	usb_transaction_init(hc, &dev_request, X86_USB_REQUEST_TYPE_CONTROL,
		hc->ports[port]->device, 0, 64);

	/* Setup Packet */
	dev_request.lowspeed = (hc->ports[port]->full_speed == 0) ? 1 : 0;
	dev_request.packet.direction = 0x80;
	dev_request.packet.type = X86_USB_REQ_GET_DESC;
	dev_request.packet.value_high = X86_USB_DESC_TYPE_CONFIG;
	dev_request.packet.value_low = 0;
	dev_request.packet.index = 0;
	dev_request.packet.length = sizeof(usb_config_descriptor_t);

	/* Setup Transfer */
	usb_transaction_setup(hc, &dev_request, sizeof(usb_packet_t));

	/* In Transfer, we want to fill the descriptor */
	usb_transaction_in(hc, &dev_request, 0, &dev_config, sizeof(usb_config_descriptor_t));

	/* Out Transfer, STATUS Stage */
	usb_transaction_out(hc, &dev_request, 1, NULL, 0);

	/* Send it */
	usb_transaction_send(hc, &dev_request);

	/* Complete ? */
	if (dev_request.completed)
	{
		hc->ports[port]->device->configuration = dev_config.configuration_value;
		hc->ports[port]->device->config_max_length = dev_config.total_length;
		hc->ports[port]->device->num_interfaces = dev_config.num_interfaces;
		hc->ports[port]->device->max_power_consumption = (uint16_t)(dev_config.max_power_consumption * 2);
	}

	/* Done */
	return dev_request.completed;
}

/* Gets the config descriptor */
int usb_function_get_config_descriptor(usb_hc_t *hc, int port)
{
	usb_hc_request_t dev_request;
	void *buffer;

	/* Step 1. Get configuration descriptor */
	if (!usb_function_get_initial_config_descriptor(hc, port))
		return 0;
	
	/* Step 2. Get FULL descriptor */
	printf("OHCI_Handler: (Get_Config_Desc) Configuration Length: 0x%x\n",
		hc->ports[port]->device->config_max_length);
	buffer = kmalloc(hc->ports[port]->device->config_max_length);
	
	/* Init transfer */
	usb_transaction_init(hc, &dev_request, X86_USB_REQUEST_TYPE_CONTROL,
		hc->ports[port]->device, 0, 64);

	/* Setup Packet */
	dev_request.lowspeed = (hc->ports[port]->full_speed == 0) ? 1 : 0;
	dev_request.packet.direction = 0x80;
	dev_request.packet.type = X86_USB_REQ_GET_DESC;
	dev_request.packet.value_high = X86_USB_DESC_TYPE_CONFIG;
	dev_request.packet.value_low = 0;
	dev_request.packet.index = 0;
	dev_request.packet.length = hc->ports[port]->device->config_max_length;

	/* Setup Transfer */
	usb_transaction_setup(hc, &dev_request, sizeof(usb_packet_t));

	/* In Transfer, we want to fill the descriptor */
	usb_transaction_in(hc, &dev_request, 0, buffer, hc->ports[port]->device->config_max_length);

	/* Out Transfer, STATUS Stage */
	usb_transaction_out(hc, &dev_request, 1, NULL, 0);

	/* Send it */
	usb_transaction_send(hc, &dev_request);

	/* Completed ? */
	if (dev_request.completed)
	{
		uint8_t *buf_ptr = (uint8_t*)buffer;
		uint32_t bytes_left = hc->ports[port]->device->config_max_length;
		uint32_t endpoints = 1;
		uint32_t ep_itr = 1;
		int i;
		hc->ports[port]->device->num_interfaces = 0;

		/* Parse Interface & Endpoints */
		while (bytes_left > 0)
		{
			/* Cast */
			uint8_t length = *buf_ptr;
			uint8_t type = *(buf_ptr + 1);

			/* Is this an interface or endpoint? :O */
			if (length == sizeof(usb_interface_descriptor_t)
				&& type == X86_USB_DESC_TYPE_INTERFACE)
			{
				usb_hc_interface_t *usb_if;
				usb_interface_descriptor_t *iface = (usb_interface_descriptor_t*)buf_ptr;

				/* Debug Print */
				if (hc->ports[port]->device->interfaces[iface->num_interface] == NULL)
				{
					printf("Interface %u - Endpoints %u (Class %u, Subclass %u, Protocol %u)\n",
						iface->num_interface, iface->num_endpoints, iface->class_code,
						iface->subclass_code, iface->protocol_code);

					/* Allocate */
					usb_if = (usb_hc_interface_t*)kmalloc(sizeof(usb_hc_interface_t));
					usb_if->id = iface->num_interface;
					usb_if->endpoints = iface->num_endpoints;
					usb_if->class_code = iface->class_code;
					usb_if->subclass_code = iface->subclass_code;
					usb_if->protocol_code = iface->protocol_code;
					endpoints += iface->num_endpoints;

					/* Update Device */
					hc->ports[port]->device->interfaces[hc->ports[port]->device->num_interfaces] = usb_if;
					hc->ports[port]->device->num_interfaces++;
				}
				
				/* Increase Pointer */
				bytes_left -= iface->length;
				buf_ptr += iface->length;
			}
			else
			{
				buf_ptr++;
				bytes_left--;
			}
		}

		/* Prepare Endpoint Loop */
		buf_ptr = (uint8_t*)buffer;
		bytes_left = hc->ports[port]->device->config_max_length;

		/* Reallocate new endpoints */
		if (endpoints > 1)
		{
			for (i = 1; i < (int)(endpoints + 1); i++)
				hc->ports[port]->device->endpoints[i] = (usb_hc_endpoint_t*)kmalloc(sizeof(usb_hc_endpoint_t));
		}
		else
			return dev_request.completed;

		/* Update Device */
		hc->ports[port]->device->num_endpoints = (endpoints + 1);
		

		while (bytes_left > 0)
		{
			/* Cast */
			uint8_t length = *buf_ptr;
			uint8_t type = *(buf_ptr + 1);

			/* Is this an interface or endpoint? :O */
			if (length == sizeof(usb_endpoint_descriptor_t)
				&& type == X86_USB_DESC_TYPE_ENDP)
			{
				usb_endpoint_descriptor_t *endpoint = (usb_endpoint_descriptor_t*)buf_ptr;
				uint32_t ep_address = endpoint->address & 0xF;
				uint32_t ep_type = endpoint->attributes & 0x3;

				if (ep_itr < endpoints)
				{
					printf("Endpoint %u - Attributes 0x%x (MaxPacketSize 0x%x)\n",
						endpoint->address, endpoint->attributes, endpoint->max_packet_size);

					/* Update Device */
					hc->ports[port]->device->endpoints[ep_itr]->address = ep_address;
					hc->ports[port]->device->endpoints[ep_itr]->max_packet_size = endpoint->max_packet_size;
					hc->ports[port]->device->endpoints[ep_itr]->interval = endpoint->interval;
					hc->ports[port]->device->endpoints[ep_itr]->toggle = 0;
					hc->ports[port]->device->endpoints[ep_itr]->type = ep_type;

					/* In or Out? */
					if (endpoint->address & 0x80)
						hc->ports[port]->device->endpoints[ep_itr]->direction = X86_USB_EP_DIRECTION_IN;
					else
						hc->ports[port]->device->endpoints[ep_itr]->direction = X86_USB_EP_DIRECTION_OUT;

					ep_itr++;
				}
				

				/* Increase Pointer */
				bytes_left -= endpoint->length;
				buf_ptr += endpoint->length;
			}
			else
			{
				buf_ptr++;
				bytes_left--;
			}
		}
	}

	/* Done */
	return dev_request.completed;
}

/* Set configuration of an usb device */
int usb_function_set_configuration(usb_hc_t *hc, int port, uint32_t configuration)
{
	usb_hc_request_t dev_request;

	/* Init transfer */
	usb_transaction_init(hc, &dev_request, X86_USB_REQUEST_TYPE_CONTROL,
		hc->ports[port]->device, 0, 64);

	/* Setup Packet */
	dev_request.lowspeed = (hc->ports[port]->full_speed == 0) ? 1 : 0;
	dev_request.packet.direction = 0;
	dev_request.packet.type = X86_USB_REQ_SET_CONFIG;
	dev_request.packet.value_high = 0;
	dev_request.packet.value_low = (configuration & 0xFF);
	dev_request.packet.index = 0;
	dev_request.packet.length = 0;		/* We do not want data */

	/* Setup Transfer */
	usb_transaction_setup(hc, &dev_request, sizeof(usb_packet_t));

	/* ACK Transfer */
	usb_transaction_in(hc, &dev_request, 1, NULL, 0);

	/* Send it */
	usb_transaction_send(hc, &dev_request);

	/* Done */
	return dev_request.completed;
}