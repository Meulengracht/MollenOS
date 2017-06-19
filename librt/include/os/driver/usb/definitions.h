/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Usb Definitions & Structures
 * - This header describes the base usb-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _USB_DEFINITIONS_H_
#define _USB_DEFINITIONS_H_

/* Includes
 * - Library */
#include <os/osdefs.h>

/* Usb Controller Definitions
 * Shared constants and magics here that are usefull to all
 * parts of an usb-implementation and usb-user. */
#define USB_MAX_PORTS					16
#define USB_MAX_VERSIONS				4
#define USB_MAX_INTERFACES				8
#define USB_MAX_ENDPOINTS				16
#define USB_MAX_LANGUAGES				16

/* Usb Class Definitions
 * Contains the different overall usb-classes that a device can exhibit */
#define USB_CLASS_INTERFACE				0x00	// Obtain from interface
#define USB_CLASS_AUDIO					0x01
#define USB_CLASS_CDC					0x02
#define USB_CLASS_HID					0x03
#define USB_CLASS_PHYSICAL				0x05
#define USB_CLASS_IMAGE					0x06
#define USB_CLASS_PRINTER				0x07
#define USB_CLASS_MSD					0x08
#define USB_CLASS_HUB					0x09
#define USB_CLASS_CDC_DATA				0x0A
#define USB_CLASS_SMART_CARD			0x0B
#define USB_CLASS_SECURITY				0x0D
#define USB_CLASS_VIDEO					0x0E
#define USB_CLASS_HEALTHCARE			0x0F
#define USB_CLASS_DIAGNOSIS				0xDC
#define USB_CLASS_WIRELESS				0xE0
#define USB_CLASS_MISC					0xEF
#define USB_CLASS_APP_SPECIFIC			0xFE
#define USB_CLASS_VENDOR_SPEC			0xFF

/* Usb Language Id Definitions
 * Contains the common language code numbers */
#define USB_LANGUAGE_ARABIC		0x401
#define USB_LANGUAGE_CHINESE	0x404
#define USB_LANGUAGE_GERMAN		0x407
#define USB_LANGUAGE_ENGLISH	0x409
#define USB_LANGUAGE_SPANISH	0x40A
#define USB_LANGUAGE_FRENCH		0x40C
#define USB_LANGUAGE_ITALIAN	0x410
#define USB_LANGUAGE_JAPANESE	0x411
#define USB_LANGUAGE_PORTUGUESE	0x416
#define USB_LANGUAGE_RUSSIAN	0x419

/* Usb Endpoint Types Definitions
 * Contains the types of endpoint that can be exposed in an interface */
typedef enum _UsbEndpointType {
	EndpointControl			= 0,
	EndpointIsochronous		= 1,
	EndpointBulk			= 2,
	EndpointInterrupt		= 3
} UsbEndpointType_t;

/* Usb Endpoint Types Definitions
 * Contains the types of synchronization that an endpoint is using */
typedef enum _UsbEndpointSynchronization {
	EndpointNoSync			= 0,
	EndpointAsync			= 1,
	EndpointAdaptive		= 2,
	EndpointSync			= 3
} UsbEndpointSynchronization_t;

/* UsbPacket (Shared)
 * Contains the structure of a default usb-setup packet used
 * in usb-transactions. */
PACKED_TYPESTRUCT(UsbPacket, {
	uint8_t						Direction;	// Request Direction
	uint8_t						Type;		// Request Type
	uint8_t						ValueLo;	// Value (Low)
	uint8_t						ValueHi;	// Value (High)
	uint16_t					Index;		// Index
	uint16_t					Length;		// Length
});

/* UsbPacket Definitions
 * Contains bit-definitions and magic values for the field UsbPacket::Direction */
#define USBPACKET_DIRECTION_OUT			0x00
#define USBPACKET_DIRECTION_IN			0x80
#define USBPACKET_DIRECTION_DEVICE		0x00
#define USBPACKET_DIRECTION_INTERFACE	0x01
#define USBPACKET_DIRECTION_ENDPOINT	0x02
#define USBPACKET_DIRECTION_OTHER		0x03
#define USBPACKET_DIRECTION_CLASS		0x20

/* UsbPacket Definitions
 * Contains bit-definitions and magic values for the field UsbPacket::Type */
#define USBPACKET_TYPE_GET_STATUS		0x00
#define USBPACKET_TYPE_CLR_FEATURE		0x01
#define USBPACKET_TYPE_SET_FEATURE		0x03
#define USBPACKET_TYPE_SET_ADDRESS		0x05
#define USBPACKET_TYPE_GET_DESC			0x06
#define USBPACKET_TYPE_SET_DESC			0x07
#define USBPACKET_TYPE_GET_CONFIG		0x08
#define USBPACKET_TYPE_SET_CONFIG		0x09
#define USBPACKET_TYPE_GET_INTERFACE	0x0A
#define USBPACKET_TYPE_SET_INTERFACE	0x0B
#define USBPACKET_TYPE_SYNC_FRAME		0x0C
#define USBPACKET_TYPE_RESET_IF			0xFF

/* UsbPacket Definitions
 * Contains the common descriptor code numbers */
#define USB_DESCRIPTOR_DEVICE			0x01
#define USB_DESCRIPTOR_CONFIG			0x02
#define USB_DESCRIPTOR_STRING			0x03
#define USB_DESCRIPTOR_INTERFACE		0x04	//Interface
#define USB_DESCRIPTOR_ENDPOINT			0x05
#define USB_DESCRIPTOR_DEVQUALIFIER		0x06	//DEVICE QUALIFIER
#define USB_DESCRIPTOR_OTHERSPEEDCONF	0x07	//Other Speed Config
#define USB_DESCRIPTOR_INTERFACEPOWER	0x08	//Interface Power
#define USB_DESCRIPTOR_OTG				0x09
#define USB_DESCRIPTOR_DEBUG			0x0A
#define USB_DESCRIPTOR_INTERFACE_ASC	0x0B
#define USB_DESCRIPTOR_BOS				0x0F
#define USB_DESCRIPTOR_DEV_CAPS			0x10
#define USB_DESCRIPTOR_SS_EP_CPN		0x30
#define USB_DESCRIPTOR_SS_ISO_EP_CPN	0x31

/* UsbPacket Definitions
 * Contains the common feature code numbers */
#define USB_FEATURE_HALT				0x00

/* UsbDeviceDescriptor (Shared)
 * Contains the structure of the device-descriptor returned by an usb device */
PACKED_TYPESTRUCT(UsbDeviceDescriptor, {
	uint8_t						Length;		// Header - Length
	uint8_t						Type;		// Header - Type

	uint16_t					UsbReleaseNumberBCD;	// Binary-Coded Decimal (i.e, 2.10 is expressed as 210h)
	uint8_t						Class;					// Class Code
	uint8_t						Subclass;				// Subclass Code
	uint8_t						Protocol;				// Protocol Code
	uint8_t						MaxPacketSize;			// Endpoint 0 - (8, 16, 32, or 64 are the only valid options)
	uint16_t					VendorId;
	uint16_t					ProductId;
	uint16_t					DeviceReleaseNumberBCD;	// Binary-Coded Decimal (i.e, 2.10 is expressed as 210h)
	uint8_t						StringIndexManufactor;	// Index for manufactorer
	uint8_t						StringIndexProduct;		// Index for product
	uint8_t						StringIndexSerialNumber;// Index for serial number
	uint8_t						ConfigurationCount;		// Possible configurations
});

/* UsbConfigDescriptor (Shared)
 * Contains the structure of the configuration-descriptor returned 
 * by an usb device */
PACKED_TYPESTRUCT(UsbConfigDescriptor, {
	uint8_t						Length;		// Header - Length
	uint8_t						Type;		// Header - Type

	uint16_t					TotalLength;			// Total size of this configuration
	uint8_t						NumInterfaces;			// Number of interfaces
	uint8_t						ConfigurationValue;		// Value of this configuration
	uint8_t						StrIndexConfiguration;	// String index of this configuration
	uint8_t						Attributes;				// Configuration attributes
	uint8_t						MaxPowerConsumption;	// Power consumption in units of 2 mA
});

/* UsbConfigDescriptor Definitions
 * Contains bit-definitions and magic values for the field UsbConfigDescriptor::Attributes */
#define USB_CONFIGURATION_ATTRIBUTES_LOCALPOWER		0x40
#define USB_CONFIGURATION_ATTRIBUTES_REMOTEWAKEUP	0x80

/* UsbInterfaceDescriptor (Shared)
 * Contains the structure of the interface-descriptor returned 
 * by an usb device */
PACKED_TYPESTRUCT(UsbInterfaceDescriptor, {
	uint8_t						Length;		// Header - Length
	uint8_t						Type;		// Header - Type

	uint8_t						NumInterface;		// Interface Index
	uint8_t						AlternativeSetting;	// Which alternative setting is this? if non-zero

	uint8_t						NumEndpoints;		// Number of endpoints for this interface
	uint8_t						Class;				// Class code
	uint8_t						Subclass;			// Subclass code
	uint8_t						Protocol;			// Protocol code
	uint8_t						StrIndexInterface;	// String index of this interface
});

/* UsbEndpointDescriptor (Shared)
 * Contains the structure of the endpoint-descriptor returned 
 * by an usb device */
PACKED_TYPESTRUCT(UsbEndpointDescriptor, {
	uint8_t						Length;		// Header - Length
	uint8_t						Type;		// Header - Type

	uint8_t						Address;		// Address and Direction
	uint8_t						Attributes;		// Attributes of this endpoint
	uint16_t					MaxPacketSize;	// Only bits 0-10 are valid here
	uint8_t						Interval;		// If interrupt endpoint, this tells us how often we should update
	uint8_t						Refresh;		// Optional
	uint8_t						SyncAddress;	// Optional
});

/* UsbEndpointDescriptor Definitions
 * Contains bit-definitions and magic values for the field UsbEndpointDescriptor::Address */
#define USB_ENDPOINT_ADDRESS(Address)			(Address & 0x0F)
#define USB_ENDPOINT_ADDRESS_IN					0x80

/* UsbEndpointDescriptor Definitions
 * Contains bit-definitions and magic values for the field UsbEndpointDescriptor::Attributes */
#define USB_ENDPOINT_ATTRIBUTES_TYPE(Attributes)	((UsbEndpointType_t)(Attributes & 0x3))
#define USB_ENDPOINT_ATTRIBUTES_SYNC(Attributes)	((UsbEndpointSynchronization_t)((Attributes >> 2) & 0x3))
#define USB_ENDPOINT_ATTRIBUTES_FEEDBACK			0x10

/* UsbStringDescriptor (Shared)
 * Contains the structure of the string-descriptor returned 
 * by an usb device */
PACKED_TYPESTRUCT(UsbStringDescriptor, {
	uint8_t						Length;		// Header - Length
	uint8_t						Type;		// Header - Type
	uint8_t						Data[20];	// String data
});

/* UsbStringDescriptor (Shared)
 * Contains the structure of the string-descriptor returned 
 * by an usb device. Length here is size + (2 * numUnicodeCharacters) */
PACKED_TYPESTRUCT(UsbUnicodeStringDescriptor, {
	uint8_t						Length;		// Header - Length
	uint8_t						Type;		// Header - Type
	uint8_t						String[60];	// Use a buffer large enough in all cases
});

#endif //!_USB_DEFINITIONS_H_
