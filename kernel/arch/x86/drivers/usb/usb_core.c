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
* MollenOS X86-32 USB Core Driver
*/

/* Includes */
#include <arch.h>
#include <drivers\usb\usb.h>
#include <semaphore.h>
#include <heap.h>
#include <list.h>
#include <stdio.h>
#include <string.h>

/* Globals */
list_t *glb_usb_controllers = NULL;
list_t *glb_usb_devices = NULL;
list_t *glb_usb_events = NULL;
semaphore_t *glb_event_lock = NULL;
volatile uint32_t glb_usb_id = 0;

/* Prototypes */
void usb_core_event_handler(void*);
void usb_device_setup(usb_hc_t *hc, int port);
void usb_device_destroy(usb_hc_t *hc, int port);

/* Gets called once a USB controller is registreret */
void usb_core_init(void)
{
	glb_usb_devices = list_create(LIST_SAFE);
	glb_usb_controllers = list_create(LIST_SAFE);
	glb_usb_events = list_create(LIST_SAFE);
	glb_usb_id = 0;

	/* Initialize Event Semaphore */
	glb_event_lock = semaphore_create(0);

	/* Start Event Thread */
	threading_create_thread("UsbEventHandler", usb_core_event_handler, NULL, 0);
}

/* Registrate an OHCI/UHCI/EHCI/XHCI controller */
usb_hc_t *usb_init_controller(void *controller_data, uint32_t controller_type, uint32_t controller_ports)
{
	usb_hc_t *usb_controller;

	/* Allocate Resources */
	usb_controller = (usb_hc_t*)kmalloc(sizeof(usb_hc_t));
	memset(usb_controller, 0, sizeof(usb_hc_t));

	usb_controller->hc = controller_data;
	usb_controller->type = controller_type;
	usb_controller->num_ports = controller_ports;

	return usb_controller;
}

uint32_t usb_register_controller(usb_hc_t *controller)
{
	uint32_t id;

	/* First call? */
	if (glb_usb_controllers == NULL)
	{
		/* Oh shit, put hat on quick! */
		usb_core_init();
	}

	/* Get id */
	id = glb_usb_id;
	glb_usb_id++;

	/* Add to list */
	list_append(glb_usb_controllers, list_create_node(id, controller));

	return id;
}

/* Create Event */
void usb_event_create(usb_hc_t *hc, int port, uint32_t type)
{
	usb_event_t *event;

	/* Allocate */
	event = (usb_event_t*)kmalloc(sizeof(usb_event_t));
	event->controller = hc;
	event->port = port;
	event->type = type;

	/* Append */
	list_append(glb_usb_events, list_create_node((int)type, event));

	/* Signal */
	semaphore_V(glb_event_lock);
}

/* Device Connected */
void usb_device_setup(usb_hc_t *hc, int port)
{
	usb_hc_device_t *device;
	int i;

	/* Make sure we have the port allocated */
	if (hc->ports[port] == NULL)
		hc->ports[port] = usb_create_port(hc, port);

	/* Create a device */
	device = (usb_hc_device_t*)kmalloc(sizeof(usb_hc_device_t));
	device->num_endpoints = 1;
	
	/* Initial Address must be 0 */
	device->address = 0;

	/* Allocate control endpoint */
	for (i = 0; i < 1; i++)
	{
		device->endpoints[i] = (usb_hc_endpoint_t*)kmalloc(sizeof(usb_hc_endpoint_t));
		device->endpoints[i]->type = X86_USB_EP_TYPE_CONTROL;
		device->endpoints[i]->toggle = 0;
		device->endpoints[i]->max_packet_size = 64;
		device->endpoints[i]->direction = X86_USB_EP_DIRECTION_BOTH;
		device->endpoints[i]->interval = 0;
	}

	/* Bind it */
	hc->ports[port]->device = device;

	/* Set Device Address (Just bind it to the port number + 1 (never set address 0) ) */
	if (!usb_function_set_address(hc, port, (uint32_t)(port + 1)))
	{
		/* Try again */
		if (!usb_function_set_address(hc, port, (uint32_t)(port + 1)))
		{
			printf("USB_Handler: (Set_Address) Failed to setup port %u\n", port);
			return;
		}
	}

	/* Get Device Descriptor */
	if (!usb_function_get_device_descriptor(hc, port))
	{
		/* Try Again */
		if (!usb_function_get_device_descriptor(hc, port))
		{
			printf("USB_Handler: (Get_Device_Desc) Failed to setup port %u\n", port);
			return;
		}
	}
	
	/* Get Config Descriptor */
	if (!usb_function_get_config_descriptor(hc, port))
	{
		/* Try Again */
		if (!usb_function_get_config_descriptor(hc, port))
		{
			printf("USB_Handler: (Get_Config_Desc) Failed to setup port %u\n", port);
			return;
		}
	}

	/* Set Configuration */
	if (!usb_function_set_configuration(hc, port, 1))
	{
		/* Try Again */
		if (!usb_function_set_configuration(hc, port, 1))
		{
			printf("USB_Handler: (Set_Configuration) Failed to setup port %u\n", port);
			return;
		}
	}

	/* Determine Driver */
	

	/* Done */
	printf("OHCI: Setup of port %u done!\n", port);
}

void usb_device_destroy(usb_hc_t *hc, int port)
{
	/* Destroy device */
	hc = hc;
	port = port;
}

/* Ports */
usb_hc_port_t *usb_create_port(usb_hc_t *hc, int port)
{
	usb_hc_port_t *hc_port;

	/* Allocate Resources */
	hc_port = kmalloc(sizeof(usb_hc_port_t));

	/* Get Port Status */
	hc_port->id = port;
	hc->port_status(hc->hc, hc_port);

	/* Done */
	return hc_port;
}

/* USB Events */
void usb_core_event_handler(void *args)
{
	usb_event_t *event;
	list_node_t *node;

	/* Unused */
	_CRT_UNUSED(args);

	while (1)
	{
		/* Acquire Semaphore */
		semaphore_P(glb_event_lock);

		/* Pop Event */
		node = list_pop(glb_usb_events);

		/* Sanity */
		if (node == NULL)
			continue;

		event = (usb_event_t*)node->data;

		/* Again, sanity */
		if (event == NULL)
			continue;

		/* Handle Event */
		switch (event->type)
		{
			case X86_USB_EVENT_CONNECTED:
			{
				/* Setup Device */
				usb_device_setup(event->controller, event->port);

			} break;

			case X86_USB_EVENT_DISCONNECTED:
			{
				/* Destroy Device */
				usb_device_destroy(event->controller, event->port);

			} break;

			default:
			{
				printf("Unhandled Event: %u on port %i\n", event->type, event->port);
			} break;
		}
	}
}

/* Gets */
usb_hc_t *usb_get_hcd(uint32_t controller_id)
{
	return (usb_hc_t*)list_get_data_by_id(glb_usb_controllers, controller_id, 0);
}

usb_hc_port_t *usb_get_port(usb_hc_t *controller, int port)
{
	/* Sanity */
	if (controller == NULL || port >= (int)controller->num_ports)
		return NULL;

	return controller->ports[port];
}