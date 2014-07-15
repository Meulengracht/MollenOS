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

#ifndef X86_USB_H_
#define X86_USB_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Definitions */
#define X86_USB_CORE_MAX_PORTS	16
#define X86_USB_CORE_MAX_IF		4
#define X86_USB_CORE_MAX_EP		16

#define X86_USB_TYPE_OHCI		0
#define X86_USB_TYPE_UHCI		1
#define X86_USB_TYPE_EHCI		2
#define X86_USB_TYPE_XHCI		3

/* Packet Request Types */
#define X86_USB_REQ_GET_STATUS		0x00
#define X86_USB_REQ_CLR_FEATURE		0x01
#define X86_USB_REQ_SET_FEATURE		0x03
#define X86_USB_REQ_SET_ADDR		0x05
#define X86_USB_REQ_GET_DESC		0x06
#define X86_USB_REQ_SET_DESC		0x07
#define X86_USB_REQ_GET_CONFIG		0x08
#define X86_USB_REQ_SET_CONFIG		0x09
#define X86_USB_REQ_GET_INTERFACE	0x0A
#define X86_USB_REQ_SET_INTERFACE	0x0B
#define X86_USB_REQ_SYNC_FRAME		0x0C

/* Descriptor Types */
#define X86_USB_DESC_TYPE_DEVICE	0x01
#define X86_USB_DESC_TYPE_CONFIG	0x02
#define X86_USB_DESC_TYPE_STRING	0x03
#define X86_USB_DESC_TYPE_INTERFACE	0x04 //Interface
#define X86_USB_DESC_TYPE_ENDP		0x05
#define X86_USB_DESC_TYPE_DEV_QAL	0x06 //DEVICE QUALIFIER
#define X86_USB_DESC_TYPE_OSC		0x07 //Other Speed Config
#define X86_USB_DESC_TYPE_IF_PWR	0x08	//Interface Power

/* Structures */
#pragma pack(push, 1)
typedef struct _usb_packet
{
	/* Request Direction (Bit 7: 1 -> Host to device, 0 - Device To Host) 
	 *                   (Bit 0-4: 0 -> Device, 1 -> Interface, 2 -> Endpoint, 3 -> Other, 4... Reserved) */
	uint8_t direction;

	/* Request Type (see above) */
	uint8_t type;

	/* Request Value */
	uint8_t value_low;
	uint8_t value_high;

	/* Request Index */
	uint16_t index;

	/* Length */
	uint16_t length;

} usb_packet_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _usb_device_descriptor
{
	/* Descriptor Length (Bytes) */
	uint8_t length;

	/* Descriptor Type */
	uint8_t type;

	/* USB Release Number in Binary-Coded Decimal (i.e, 2.10 is expressed as 210h) */
	uint16_t usb_rn_bcd;

	/* USB Class Code (USB-IF) */
	uint8_t class_code;

	/* Usb Subclass Code (USB-IF) */
	uint8_t subclass_code;

	/* Device Protocol Code (USB-IF) */
	uint8_t protocol_code;

	/* Max packet size for endpoing zero (8, 16, 32, or 64 are the only valid options) */
	uint8_t max_packet_size;

	/* Vendor Id */
	uint16_t vendor_id;

	/* Product Id */
	uint16_t product_id;

	/* Device Release Number in Binary-Coded Decimal (i.e, 2.10 is expressed as 210h) */
	uint16_t device_rn_bcd;

	/* String Descriptor Indexes */
	uint8_t str_index_manufactor;
	uint8_t str_index_product;
	uint8_t str_index_serial_num;

	/* Number of Configuration */
	uint8_t num_configurations;

} usb_device_descriptor_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _usb_config_descriptor
{
	/* Descriptor Length (Bytes) */
	uint8_t length;

	/* Descriptor Type */
	uint8_t type;

	/* The total combined length in bytes of all
	 * the descriptors returned with the request
	 * for this CONFIGURATION descriptor */
	uint16_t total_length;

	/* Number of Interfaces */
	uint8_t num_interfaces;

	/* Configuration */
	uint8_t configuration_value;

	/* String Index */
	uint8_t	str_index_configuration;

	/* Attributes 
	 * Bit 6: 0 - Selfpowered, 1 - Local Power Source 
	 * Bit 7: 1 - Remote Wakeup Support */
	uint8_t attributes;

	/* Power Consumption 
	 * Expressed in units of 2mA (i.e., a value of 50 in this field indicates 100mA) */
	uint8_t max_power_consumption;

} usb_config_descriptor_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _usb_interface_descriptor
{
	/* Descriptor Length (Bytes) */
	uint8_t length;

	/* Descriptor Type */
	uint8_t type;

	/* Number of Interface */
	uint8_t num_interface;

	/* Alternative Setting */
	uint8_t alternative_setting;

	/* Number of Endpoints other than endpoint zero */
	uint8_t num_endpoints;
	
	/* USB Class Code (USB-IF) */
	uint8_t class_code;

	/* Usb Subclass Code (USB-IF) */
	uint8_t subclass_code;

	/* Device Protocol Code (USB-IF) */
	uint8_t protocol_code;

	/* String Index */
	uint8_t str_index_interface;

} usb_interface_descriptor_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _usb_endpoint_descriptor
{
	/* Descriptor Length (Bytes) */
	uint8_t length;

	/* Descriptor Type */
	uint8_t type;

	/* Address 
	 * Bits 0-3: Endpoint Number
	 * Bit 7: 1 - In, 0 - Out */
	uint8_t address;

	/* Attributes 
	 * Bits 0-1: Xfer Type (00 control, 01 isosync, 10 bulk, 11 interrupt) 
	 * Bits 2-3: Sync Type (00 no sync, 01 async, 10 adaptive, 11 sync) 
	 * Bits 4-5: Usage Type (00 data, 01 feedback) */
	uint8_t attributes;

	/* Maximum Packet Size (Bits 0-10) (Bits 11-15: 0) */
	uint16_t max_packet_size;

	/* Interval */
	uint8_t interval;

} usb_endpoint_descriptor_t;
#pragma pack(pop)

/* The Abstract Usb Endpoint */
typedef struct _usb_hc_endpoint
{
	/* Type */
	uint32_t type;

	/* Address */
	uint32_t address;

	/* Direction (IN, OUT) */
	uint32_t direction;

	/* Max Packet Size (Always 64 bytes, almost) */
	uint32_t max_packet_size;

	/* Data Toggle */
	uint32_t toggle;

	/* Poll Interval */
	uint32_t interval;

} usb_hc_endpoint_t;

#define X86_USB_EP_DIRECTION_IN		0x0
#define X86_USB_EP_DIRECTION_OUT	0x1
#define X86_USB_EP_DIRECTION_BOTH	0x2

#define X86_USB_EP_TYPE_CONTROL		0x0
#define X86_USB_EP_TYPE_ISOCHRONOUS	0x1
#define X86_USB_EP_TYPE_BULK		0x2
#define X86_USB_EP_TYPE_INTERRUPT	0x3

/* The Abstract Usb Interface */
typedef struct _usb_hc_interface
{
	/* Interface Type */
	uint32_t id;
	uint32_t class_code;
	uint32_t subclass_code;
	uint32_t protocol_code;

	/* Ep Numbers */
	uint32_t endpoints;

} usb_hc_interface_t;

/* The Abstract Device */
#pragma pack(push, 1)
typedef struct _usb_hc_device
{
	/* Device Information */
	uint8_t class_code;
	uint8_t subclass_code;
	uint8_t protocol_code;
	uint16_t vendor_id;
	uint16_t product_id;
	uint8_t num_configurations;
	uint16_t config_max_length;
	uint16_t max_power_consumption;
	uint8_t configuration;

	/* String Ids */
	uint8_t str_index_product;
	uint8_t str_index_manufactor;
	uint8_t str_index_sn;

	/* Device Address */
	uint32_t address;

	/* Device Interfaces */
	uint32_t num_interfaces;
	struct _usb_hc_interface *interfaces[X86_USB_CORE_MAX_IF];

	/* Device Endpoints */
	uint32_t num_endpoints;
	struct _usb_hc_endpoint *endpoints[X86_USB_CORE_MAX_EP];

} usb_hc_device_t;
#pragma pack(pop)

/* The Abstract Transaction 
 * A request consists of several transactions */
typedef struct _usb_hc_transaction
{
	/* Type */
	uint32_t type;

	/* A Transfer Descriptor Ptr */
	void *transfer_descriptor;

	/* Transfer Descriptor Buffer */
	void *transfer_buffer;

	/* Target/Source Buffer */
	void *io_buffer;

	/* Target/Source Buffer Length */
	size_t io_length;

	/* Next Transaction */
	struct _usb_hc_transaction *link;

} usb_hc_transaction_t;

/* Types */
#define X86_USB_TRANSACTION_SETUP	1
#define X86_USB_TRANSACTION_IN		2
#define X86_USB_TRANSACTION_OUT		3

/* The Abstract Transfer Request */
typedef struct _usb_hc_request
{
	/* Bulk or Control? */
	uint32_t type;
	uint32_t lowspeed;

	/* Transfer Specific Information */
	void *data;
	struct _usb_hc_device *device;

	/* Endpoint */
	uint32_t endpoint;

	/* Length */
	uint32_t length;

	/* Transaction Information */
	uint32_t token_bytes;
	uint32_t toggle;
	void *io_buffer;
	size_t io_length;

	/* Packet */
	struct _usb_packet packet;

	/* The Transaction List */
	struct _usb_hc_transaction *transactions;

	/* Is it done? */
	uint32_t completed;

} usb_hc_request_t;

/* Type Definitions */
#define X86_USB_REQUEST_TYPE_CONTROL	0x1
#define X86_USB_REQUEST_TYPE_BULK		0x2

/* The Abstract Port */
typedef struct _usb_hc_port
{
	/* Port Number */
	uint32_t id;

	/* Connection Status */
	uint32_t connected;

	/* Enabled Status */
	uint32_t enabled;

	/* Speed */
	uint32_t full_speed;

	/* Device Connected */
	struct _usb_hc_device *device;

} usb_hc_port_t;

/* The Abstract Controller */
typedef struct _usb_hc
{
	/* Controller Type */
	uint32_t type;

	/* Controller Data */
	void *hc;

	/* Controller Info */
	uint32_t num_ports;

	/* Ports */
	struct _usb_hc_port *ports[X86_USB_CORE_MAX_PORTS];

	/* Port Functions */
	void (*root_hub_check)(void *);
	void (*port_status)(void *, usb_hc_port_t *);

	/* Transaction Functions */
	void (*transaction_init)(void *, struct _usb_hc_request*);
	struct _usb_hc_transaction *(*transaction_setup)(void*, struct _usb_hc_request*);
	struct _usb_hc_transaction *(*transaction_in)(void*, struct _usb_hc_request*);
	struct _usb_hc_transaction *(*transaction_out)(void*, struct _usb_hc_request*);
	void (*transaction_send)(void*, struct _usb_hc_request*);

} usb_hc_t;

/* Usb Event */
typedef struct _usb_event
{
	/* Event Type */
	uint32_t type;

	/* Controller */
	struct _usb_hc *controller;

	/* Port */
	int port;

} usb_event_t;

/* Event Types */
#define X86_USB_EVENT_CONNECTED		0
#define X86_USB_EVENT_DISCONNECTED	1
#define X86_USB_EVENT_TRANSFER		2
#define X86_USB_EVENT_ROOTHUB_CHECK	3

/* Prototypes */

/* Returns an controller ID for used with identification */
_CRT_EXTERN usb_hc_t *usb_init_controller(void *controller_data, uint32_t controller_type, uint32_t controller_ports);
_CRT_EXTERN uint32_t usb_register_controller(usb_hc_t *controller);

/* Transfer Utilities */
_CRT_EXTERN void usb_transaction_init(usb_hc_t *hc, usb_hc_request_t *dev_request, uint32_t type,
										usb_hc_device_t *device, uint32_t endpoint, uint32_t max_length);
_CRT_EXTERN void usb_transaction_setup(usb_hc_t *hc, usb_hc_request_t *dev_request, uint32_t packet_size);
_CRT_EXTERN void usb_transaction_in(usb_hc_t *hc, usb_hc_request_t *dev_request, uint32_t handshake, void *buffer, uint32_t length);
_CRT_EXTERN void usb_transaction_out(usb_hc_t *hc, usb_hc_request_t *dev_request, uint32_t handshake, void *buffer, uint32_t length);
_CRT_EXTERN void usb_transaction_send(usb_hc_t *hc, usb_hc_request_t *dev_request);

/* Functions */
_CRT_EXTERN int usb_function_set_address(usb_hc_t *hc, int port, uint32_t address);
_CRT_EXTERN int usb_function_get_device_descriptor(usb_hc_t *hc, int port);
_CRT_EXTERN int usb_function_get_config_descriptor(usb_hc_t *hc, int port);
_CRT_EXTERN int usb_function_set_configuration(usb_hc_t *hc, int port, uint32_t configuration);

/* Events */
_CRT_EXTERN void usb_event_create(usb_hc_t *hc, int port, uint32_t type);

/* Ports */
_CRT_EXTERN usb_hc_port_t *usb_create_port(usb_hc_t *hc, int port);

/* Gets */
_CRT_EXTERN usb_hc_t *usb_get_hcd(uint32_t controller_id);
_CRT_EXTERN usb_hc_port_t *usb_get_port(usb_hc_t *controller, int port);

#endif // !X86_USB_H_
