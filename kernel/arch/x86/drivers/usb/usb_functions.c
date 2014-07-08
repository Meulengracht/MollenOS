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
	dev_request->endpoint = endpoint;
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
void usb_function_set_address(usb_hc_port_t *port)
{
	port = port;
}

/* Gets the device descriptor */
void usb_function_get_device_descriptor(usb_hc_t *hc, int port)
{
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

	printf("USB Length 0x%x - Device Class & Subclass: 0x%x - 0x%x\n", dev_info.length, dev_info.class_code, dev_info.subclass_code);
}