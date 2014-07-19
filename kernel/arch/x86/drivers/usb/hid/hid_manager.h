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
* MollenOS X86-32 USB Core HID Driver
*/

#ifndef X86_USB_HID_H_
#define X86_USB_HID_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Definitions */

/* Class Specific bRequests */
#define X86_USB_REQ_GET_REPORT			0x1
#define X86_USB_REQ_GET_IDLE			0x2
#define X86_USB_REQ_GET_PROTOCOL		0x3
#define X86_USB_REQ_SET_REPORT			0x9
#define X86_USB_REQ_SET_IDLE			0xA
#define X86_USB_REQ_SET_PROTOCOL		0xB

#define X86_USB_DESC_TYPE_HID			0x21
#define X86_USB_DESC_TYPE_REPORT		0x22

/* Report Definitions */
#define X86_USB_REPORT_TYPE_MAIN		0x0
#define X86_USB_REPORT_TYPE_GLOBAL		0x4
#define X86_USB_REPORT_TYPE_LOCAL		0x8
#define X86_USB_REPORT_TYPE_LONGITEM	0xC

/* Main Items */
#define X86_USB_REPORT_MAIN_TAG_INPUT			0x80
#define X86_USB_REPORT_MAIN_TAG_OUTPUT			0x90
#define X86_USB_REPORT_MAIN_TAG_COLLECTION		0xA0
#define X86_USB_REPORT_MAIN_TAG_FEATURE			0xB0
#define X86_USB_REPORT_MAIN_TAG_ENDCOLLECTION	0xC0

/* Global Items */
#define X86_USB_REPORT_GLOBAL_TAG_USAGE_PAGE	0x0
#define X86_USB_REPORT_GLOBAL_TAG_LOGICAL_MIN	0x10
#define X86_USB_REPORT_GLOBAL_TAG_LOGICAL_MAX	0x20
#define X86_USB_REPORT_GLOBAL_TAG_PHYSICAL_MIN	0x30
#define X86_USB_REPORT_GLOBAL_TAG_PHYSICAL_MAX	0x40
#define X86_USB_REPORT_GLOBAL_TAG_UNIT_EXPONENT	0x50
#define X86_USB_REPORT_GLOBAL_TAG_UNIT_VALUE	0x60
#define X86_USB_REPORT_GLOBAL_TAG_REPORT_SIZE	0x70
#define X86_USB_REPORT_GLOBAL_TAG_REPORT_ID		0x80
#define X86_USB_REPORT_GLOBAL_TAG_REPORT_COUNT	0x90
#define X86_USB_REPORT_GLOBAL_TAG_PUSH			0xA0
#define X86_USB_REPORT_GLOBAL_TAG_POP			0xB0

/* Local Items */
#define X86_USB_REPORT_LOCAL_TAG_USAGE			0x0
#define X86_USB_REPORT_LOCAL_TAG_USAGE_MIN		0x10
#define X86_USB_REPORT_LOCAL_TAG_USAGE_MAX		0x20
#define X86_USB_REPORT_LOCAL_TAG_DESIGN_INDEX	0x30
#define X86_USB_REPORT_LOCAL_TAG_DESIGN_MIN		0x40
#define X86_USB_REPORT_LOCAL_TAG_DESIGN_MAX		0x50
#define X86_USB_REPORT_LOCAL_TAG_STRING_INDEX	0x70
#define X86_USB_REPORT_LOCAL_TAG_STRING_MIN		0x80
#define X86_USB_REPORT_LOCAL_TAG_STRING_MAX		0x90
#define X86_USB_REPORT_LOCAL_TAG_DELIMITER		0xA0

/* Report Usage Pages */
#define X86_USB_REPORT_USAGE_PAGE_UNDEFINED		0x0
#define X86_USB_REPORT_USAGE_PAGE_GENERIC_PC	0x1	/* Geneic Desktop (mouse etc) */
#define X86_USB_REPORT_USAGE_PAGE_SIMULATION	0x2
#define X86_USB_REPORT_USAGE_PAGE_VR			0x3
#define X86_USB_REPORT_USAGE_PAGE_SPORT			0x4
#define X86_USB_REPORT_USAGE_PAGE_GAME			0x5
#define X86_USB_REPORT_USAGE_PAGE_GENERICDEV	0x6 /* Generic Device */
#define X86_USB_REPORT_USAGE_PAGE_KEYBOARD		0x7
#define X86_USB_REPORT_USAGE_PAGE_LED			0x8
#define X86_USB_REPORT_USAGE_PAGE_BUTTON		0x9
#define X86_USB_REPORT_USAGE_PAGE_ORDIAL		0xA
#define X86_USB_REPORT_USAGE_PAGE_TELEPHONY		0xB
#define X86_USB_REPORT_USAGE_PAGE_CONSUMER		0xC
#define X86_USB_REPORT_USAGE_PAGE_DIGITIZER		0xD
#define X86_USB_REPORT_USAGE_PAGE_PID			0xF
#define X86_USB_REPORT_USAGE_PAGE_UNICODE		0x10

/* Report Usages */
#define X86_USB_REPORT_USAGE_POINTER			0x1
#define X86_USB_REPORT_USAGE_MOUSE				0x2
#define X86_USB_REPORT_USAGE_JOYSTICK			0x4
#define X86_USB_REPORT_USAGE_GAMEPAD			0x5
#define X86_USB_REPORT_USAGE_KEYBOARD			0x6
#define X86_USB_REPORT_USAGE_KEYPAD				0x7
#define X86_USB_REPORT_USAGE_X_AXIS				0x30
#define X86_USB_REPORT_USAGE_Y_AXIS				0x31
#define X86_USB_REPORT_USAGE_Z_AXIS				0x32
#define X86_USB_REPORT_USAGE_R_X				0x33
#define X86_USB_REPORT_USAGE_R_Y				0x34
#define X86_USB_REPORT_USAGE_R_Z				0x35
#define X86_USB_REPORT_USAGE_WHEEL				0x36

/* Types */
#define X86_USB_HID_TYPE_UNKNOWN				0x0
#define X86_USB_HID_TYPE_MOUSE					0x1
#define X86_USB_HID_TYPE_KEYBOARD				0x2
#define X86_USB_HID_TYPE_KEYPAD					0x3
#define X86_USB_HID_TYPE_JOYSTICK				0x4
#define X86_USB_HID_TYPE_GAMEPAD				0x5
#define X86_USB_HID_TYPE_OTHER					0x6

/* Structures */
#pragma pack(push, 1)
typedef struct _usb_hid_descriptor
{
	/* Length */
	uint8_t length;

	/* Type */
	uint8_t type;

	/* Numeric Version of HID */
	uint16_t bcd_hid;

	/* Country Code */
	uint8_t country_code;

	/* Number of Class Specific Descriptors */
	uint8_t num_descriptors;

	/* Class Descriptor Type */
	uint8_t class_descriptor_type;

	/* Class Descriptor Length */
	uint16_t class_descriptor_length;

} usb_hid_descriptor_t;
#pragma pack(pop)

/* Report Structures */

/* Collection Variables */
#pragma pack(push, 1)
typedef struct _usb_hid_report_global_stats
{
	/* Usage Page */
	uint32_t usage;

	/* Logical Min/Max */
	int32_t log_min;
	int32_t log_max;
	uint8_t has_log_min;
	uint8_t has_log_max;

	/* Physical Min/Max */
	int32_t physical_min;
	int32_t physical_max;
	uint8_t has_phys_min;
	uint8_t has_phys_max;

	/* Unit Exponent & Unit */
	int32_t unit_type;
	int32_t unit_exponent;

	/* Report Size / Id / Count */
	uint32_t report_size;
	uint32_t report_id;
	uint32_t report_count;

} usb_report_global_stats_t;
#pragma pack(pop)

/* Item Variables */
typedef struct _usb_hid_report_item_stats
{
	/* Usage(s) */
	uint32_t usages[16];

	/* Usage Min & Max */
	uint32_t usage_min;
	uint32_t usage_max;

	/* Bit Offset */
	uint32_t bit_offset;

} usb_report_item_stats_t;

/* Input Item */
typedef struct _usb_hid_report_input_item
{
	/* Flags */
	uint32_t flags;

	/* Contains a local state */
	struct
		_usb_hid_report_item_stats stats;

} usb_report_input_t;

/* Input Item Flags */
#define X86_USB_REPORT_INPUT_TYPE_CONSTANT	0x0
#define X86_USB_REPORT_INPUT_TYPE_RELATIVE	0x1
#define X86_USB_REPORT_INPUT_TYPE_ABSOLUTE	0x2
#define X86_USB_REPORT_INPUT_TYPE_ARRAY		0x3

/* Collection Item */
typedef struct _usb_hid_report_collection_item
{
	/* Type */
	uint32_t type;
	uint32_t input_type;

	/* Data */
	void *data;

	/* Stats */
	struct
		_usb_hid_report_global_stats stats;

	/* Link */
	struct
		_usb_hid_report_collection_item *link;

} usb_report_collection_item_t;

/* Collection Item Types */
#define X86_USB_COLLECTION_TYPE_COLLECTION	0x0
#define X86_USB_COLLECTION_TYPE_INPUT		0x1
#define X86_USB_COLLECTION_TYPE_OUTPUT		0x2
#define X86_USB_COLLECTION_TYPE_FEATURE		0x3

/* Collection (Contains a set of items) */
typedef struct _usb_hid_report_collection
{
	/* Simply a linked list */

	/* Childs */
	struct
		_usb_hid_report_collection_item *childs;

	/* Parent */
	struct
		_usb_hid_report_collection *parent;

} usb_report_collection_t;

typedef struct _usb_hid_driver
{
	/* Endpoint in question */
	void *endpoint;

	/* Report Data */
	struct 
		_usb_hid_report_collection *collection;

	/* Input Buffers */
	uint8_t *previous_data_buffer;
	uint8_t *data_buffer;

} usb_hid_driver_t;


/* Prototypes */

/* Initialise Driver for a HID */
_CRT_EXTERN void usb_hid_initialise(usb_hc_device_t *device, uint32_t iface);

#endif // !X86_USB_HID_H_
