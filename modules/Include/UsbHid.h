/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS USB Core HID Driver
*/

#ifndef _USB_HID_H_
#define _USB_HID_H_

/* Includes */
#include <UsbCore.h>
#include <DeviceManager.h>
#include <crtdefs.h>

/* Definitions */

/* Codes */
#define USB_HID_CLASS					0x3

#define USB_HID_SUBCLASS_NONE			0x0
#define USB_HID_SUBCLASS_BOOT			0x1

#define USB_HID_PROTOCOL_NONE			0x0
#define USB_HID_PROTOCOL_KEYBOARD		0x1
#define USB_HID_PROTOCOL_MOUSE			0x2

/* Class Specific bRequests */
#define USB_HID_GET_REPORT				0x1
#define USB_HID_GET_IDLE				0x2
#define USB_HID_GET_PROTOCOL			0x3
#define USB_HID_SET_REPORT				0x9
#define USB_HID_SET_IDLE				0xA
#define USB_HID_SET_PROTOCOL			0xB

#define USB_DESCRIPTOR_TYPE_HID			0x21
#define USB_DESCRIPTOR_TYPE_REPORT		0x22

/* Report Definitions */
#define USB_HID_REPORT_MAIN				0x0
#define USB_HID_REPORT_GLOBAL			0x4
#define USB_HID_REPORT_LOCAL			0x8
#define USB_HID_REPORT_LONGITEM			0xC

/* Main Items */
#define USB_HID_MAIN_INPUT				0x80
#define USB_HID_MAIN_OUTPUT				0x90
#define USB_HID_MAIN_COLLECTION			0xA0
#define USB_HID_MAIN_FEATURE			0xB0
#define USB_HID_MAIN_ENDCOLLECTION		0xC0

/* Global Items */
#define USB_HID_GLOBAL_USAGE_PAGE		0x0
#define USB_HID_GLOBAL_LOGICAL_MIN		0x10
#define USB_HID_GLOBAL_LOGICAL_MAX		0x20
#define USB_HID_GLOBAL_PHYSICAL_MIN		0x30
#define USB_HID_GLOBAL_PHYSICAL_MAX		0x40
#define USB_HID_GLOBAL_UNIT_EXPONENT	0x50
#define USB_HID_GLOBAL_UNIT_VALUE		0x60
#define USB_HID_GLOBAL_REPORT_SIZE		0x70
#define USB_HID_GLOBAL_REPORT_ID		0x80
#define USB_HID_GLOBAL_REPORT_COUNT		0x90
#define USB_HID_GLOBAL_PUSH				0xA0
#define USB_HID_GLOBAL_POP				0xB0

/* Local Items */
#define USB_HID_LOCAL_USAGE				0x0
#define USB_HID_LOCAL_USAGE_MIN			0x10
#define USB_HID_LOCAL_USAGE_MAX			0x20
#define USB_HID_LOCAL_DESIGN_INDEX		0x30
#define USB_HID_LOCAL_DESIGN_MIN		0x40
#define USB_HID_LOCAL_DESIGN_MAX		0x50
#define USB_HID_LOCAL_STRING_INDEX		0x70
#define USB_HID_LOCAL_STRING_MIN		0x80
#define USB_HID_LOCAL_STRING_MAX		0x90
#define USB_HID_LOCAL_DELIMITER			0xA0

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

/* Structures */
#pragma pack(push, 1)
typedef struct _UsbHidDescriptor
{
	/* Length */
	uint8_t Length;

	/* Type */
	uint8_t Type;

	/* Numeric Version of HID */
	uint16_t BcdHid;

	/* Country Code */
	uint8_t CountryCode;

	/* Number of Class Specific Descriptors */
	uint8_t NumDescriptors;

	/* Class Descriptor Type */
	uint8_t ClassDescriptorType;

	/* Class Descriptor Length */
	uint16_t ClassDescriptorLength;

} UsbHidDescriptor_t;
#pragma pack(pop)

/* Report Structures */

/* Collection Variables */
#pragma pack(push, 1)
typedef struct _UsbHidReportGlobalStats
{
	/* Usage Page */
	uint32_t Usage;

	/* Logical Min/Max */
	int32_t LogicalMin;
	int32_t LogicalMax;
	uint8_t HasLogicalMin;
	uint8_t HasLogicalMax;

	/* Physical Min/Max */
	int32_t PhysicalMin;
	int32_t PhysicalMax;
	uint8_t HasPhysicalMin;
	uint8_t HasPhysicalMax;

	/* Unit Exponent & Unit */
	int32_t UnitType;
	int32_t UnitExponent;

	/* Report Size / Id / Count */
	uint32_t ReportSize;
	uint32_t ReportId;
	uint32_t ReportCount;

} UsbHidReportGlobalStats_t;
#pragma pack(pop)

/* Item Variables */
typedef struct _UsbHidReportItemStats
{
	/* Usage(s) */
	uint32_t Usages[16];

	/* Usage Min & Max */
	uint32_t UsageMin;
	uint32_t UsageMax;

	/* Bit Offset */
	uint32_t BitOffset;

} UsbHidReportItemStats_t;

/* Input Item */
typedef struct _UsbHidReportInputItem
{
	/* Flags */
	uint32_t Flags;

	/* Contains a local state */
	UsbHidReportItemStats_t Stats;

} UsbHidReportInputItem_t;

/* Input Item Flags */
#define X86_USB_REPORT_INPUT_TYPE_CONSTANT	0x0
#define X86_USB_REPORT_INPUT_TYPE_RELATIVE	0x1
#define X86_USB_REPORT_INPUT_TYPE_ABSOLUTE	0x2
#define X86_USB_REPORT_INPUT_TYPE_ARRAY		0x3

/* Collection Item */
typedef struct _UsbHidReportCollectionItem
{
	/* Type */
	int Type;
	int InputType;

	/* Data */
	void *Data;

	/* Stats */
	UsbHidReportGlobalStats_t Stats;

	/* Link */
	struct 
		_UsbHidReportCollectionItem *Link;

} UsbHidReportCollectionItem_t;

/* Collection Item Types */
#define USB_HID_TYPE_COLLECTION		0x0
#define USB_HID_TYPE_INPUT			0x1
#define USB_HID_TYPE_OUTPUT			0x2
#define USB_HID_TYPE_FEATURE		0x3

/* Collection (Contains a set of items) */
typedef struct _UsbHidReportCollection
{
	/* Usage Type */
	size_t UsagePage;
	size_t Usage;

	/* Childs */
	UsbHidReportCollectionItem_t *Childs;

	/* Siblings */
	struct 
		_UsbHidReportCollection *Link;

} UsbHidReportCollection_t;

typedef struct _HidDevice
{
	/* Id's */
	uint32_t Interface;
	DevId_t DeviceId;

	/* Report Data */
	UsbHidReportCollection_t *Collection;

	/* Input Buffers */
	uint8_t *DataBuffer;
	uint8_t *PrevDataBuffer;

	/* Usb Data */
	UsbHcDevice_t *UsbDevice;
	UsbHcEndpoint_t *EpInterrupt;

	/* The interrupt channel */
	UsbHcRequest_t *InterruptChannel;

} HidDevice_t;


/* Prototypes */

/* Initialise Driver for a HID */
_CRT_EXTERN void UsbHidInit(UsbHcDevice_t *UsbDevice, int InterfaceIndex);

#endif // !X86_USB_HID_H_
