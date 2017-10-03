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
 * MollenOS MCore - Human Input Device Driver (Generic)
 */

#ifndef __USB_HID_H__
#define __USB_HID_H__

/* Includes 
 * - Library */
#include <os/osdefs.h>

/* Includes
 * - Interfaces */
#include <os/driver/input.h>
#include <os/driver/contracts/base.h>
#include <os/driver/contracts/usbhost.h>
#include <os/driver/contracts/usbdevice.h>

/* HID Class Definitions 
 * Contains generic magic constants and definitions */
#define HID_CLASS                           0x3

/* HID Subclass Definitions 
 * Contains generic magic constants and definitions */
#define HID_SUBCLASS_NONE                   0x0
#define HID_SUBCLASS_BOOT                   0x1

/* HID Protocol Definitions 
 * Contains generic magic constants and definitions */
#define HID_PROTOCOL_NONE                   0x0
#define HID_PROTOCOL_KEYBOARD               0x1
#define HID_PROTOCOL_MOUSE                  0x2

/* HID bRequest Definitions 
 * Contains generic magic constants and definitions */
#define HID_GET_REPORT                      0x1
#define HID_GET_IDLE                        0x2
#define HID_GET_PROTOCOL                    0x3
#define HID_SET_REPORT                      0x9
#define HID_SET_IDLE                        0xA
#define HID_SET_PROTOCOL                    0xB

/* HID Descriptor Definitions 
 * Contains generic magic constants and definitions */
#define DESCRIPTOR_TYPE_HID                 0x21
#define DESCRIPTOR_TYPE_REPORT              0x22

/* HID Report Type Definitions 
 * Contains generic magic constants and definitions */
#define HID_REPORT_TYPE_MAIN                0x0
#define HID_REPORT_TYPE_GLOBAL              0x4
#define HID_REPORT_TYPE_LOCAL               0x8
#define HID_REPORT_TYPE_LONGITEM            0xC

/* HID Report Tag (Main) Definitions 
 * Contains generic magic constants and definitions */
#define HID_MAIN_INPUT                      0x80
#define HID_MAIN_OUTPUT                     0x90
#define HID_MAIN_COLLECTION                 0xA0
#define HID_MAIN_FEATURE                    0xB0
#define HID_MAIN_ENDCOLLECTION              0xC0

/* HID Report Tag (Global) Definitions 
 * Contains generic magic constants and definitions */
#define HID_GLOBAL_USAGE_PAGE               0x0
#define HID_GLOBAL_LOGICAL_MIN              0x10
#define HID_GLOBAL_LOGICAL_MAX              0x20
#define HID_GLOBAL_PHYSICAL_MIN             0x30
#define HID_GLOBAL_PHYSICAL_MAX             0x40
#define HID_GLOBAL_UNIT_EXPONENT            0x50
#define HID_GLOBAL_UNIT_VALUE               0x60
#define HID_GLOBAL_REPORT_SIZE              0x70
#define HID_GLOBAL_REPORT_ID                0x80
#define HID_GLOBAL_REPORT_COUNT             0x90
#define HID_GLOBAL_PUSH                     0xA0
#define HID_GLOBAL_POP                      0xB0

/* HID Report Tag (Local) Definitions 
 * Contains generic magic constants and definitions */
#define HID_LOCAL_USAGE                     0x0
#define HID_LOCAL_USAGE_MIN                 0x10
#define HID_LOCAL_USAGE_MAX                 0x20
#define HID_LOCAL_DESIGN_INDEX              0x30
#define HID_LOCAL_DESIGN_MIN                0x40
#define HID_LOCAL_DESIGN_MAX                0x50
#define HID_LOCAL_STRING_INDEX              0x70
#define HID_LOCAL_STRING_MIN                0x80
#define HID_LOCAL_STRING_MAX                0x90
#define HID_LOCAL_DELIMITER                 0xA0

/* HID Report Usage Pages Definitions 
 * Contains generic magic constants and definitions */
#define HID_USAGE_PAGE_UNKNOWN              0x0
#define HID_USAGE_PAGE_GENERIC_PC           0x1 // Generic Desktop (mouse etc)
#define HID_REPORT_USAGE_PAGE_SIMULATION    0x2
#define HID_REPORT_USAGE_PAGE_VR            0x3
#define HID_REPORT_USAGE_PAGE_SPORT         0x4
#define HID_REPORT_USAGE_PAGE_GAME          0x5
#define HID_REPORT_USAGE_PAGE_GENERICDEV    0x6 // Generic Device
#define HID_REPORT_USAGE_PAGE_KEYBOARD      0x7
#define HID_REPORT_USAGE_PAGE_LED           0x8
#define HID_REPORT_USAGE_PAGE_BUTTON        0x9
#define HID_REPORT_USAGE_PAGE_ORDIAL        0xA
#define HID_REPORT_USAGE_PAGE_TELEPHONY     0xB
#define HID_REPORT_USAGE_PAGE_CONSUMER      0xC
#define HID_REPORT_USAGE_PAGE_DIGITIZER     0xD
#define HID_REPORT_USAGE_PAGE_PID           0xF
#define HID_REPORT_USAGE_PAGE_UNICODE       0x10

/* HID Report Usage Definitions 
 * Contains generic magic constants and definitions */
#define HID_REPORT_USAGE_POINTER            0x1
#define HID_REPORT_USAGE_MOUSE              0x2
#define HID_REPORT_USAGE_JOYSTICK           0x4
#define HID_REPORT_USAGE_GAMEPAD            0x5
#define HID_REPORT_USAGE_KEYBOARD           0x6
#define HID_REPORT_USAGE_KEYPAD             0x7
#define HID_REPORT_USAGE_X_AXIS             0x30
#define HID_REPORT_USAGE_Y_AXIS             0x31
#define HID_REPORT_USAGE_Z_AXIS             0x32
#define HID_REPORT_USAGE_R_X                0x33
#define HID_REPORT_USAGE_R_Y                0x34
#define HID_REPORT_USAGE_R_Z                0x35
#define HID_REPORT_USAGE_WHEEL              0x36

/* UsbHidDescriptor
 * A descriptor containing the setup of the HID device
 * and it's physical layout. */
PACKED_TYPESTRUCT(UsbHidDescriptor, {
    uint8_t                         Length;
    uint8_t                         Type;
    uint16_t                        BcdHid;
    uint8_t                         CountryCode;
    uint8_t                         NumDescriptors;
    uint8_t                         ClassDescriptorType;
    uint16_t                        ClassDescriptorLength;
});

/* UsbHidReportGlobalStats
 * Global state variables that applies to all non-local state
 * items. Only other stats can overwrite the contained stats. */
PACKED_TYPESTRUCT(UsbHidReportGlobalStats, {
    uint32_t                        UsagePage;

    int32_t                         LogicalMin;
    int32_t                         LogicalMax;
    uint8_t                         HasLogicalMin;
    uint8_t                         HasLogicalMax;

    int32_t                         PhysicalMin;
    int32_t                         PhysicalMax;
    uint8_t                         HasPhysicalMin;
    uint8_t                         HasPhysicalMax;
    
    int32_t                         UnitType;
    int32_t                         UnitExponent;
    
    uint32_t                        ReportSize;
    uint32_t                        ReportId;
    uint32_t                        ReportCount;
});

/* UsbHidReportItemStats
 * Describes an HID-item for which kind of usages, and
 * where in the report (bit index) its data is */
typedef struct _UsbHidReportItemStats {
    uint32_t                        Usages[16];

    uint32_t                        UsageMin;
    uint32_t                        UsageMax;

    uint32_t                        BitOffset;
} UsbHidReportItemStats_t;

/* UsbHidReportInputItem
 * List item for an report item. Also contains the above ItemStats. */
typedef struct _UsbHidReportInputItem {
    Flags_t                         Flags;
    UsbHidReportItemStats_t         LocalState;
} UsbHidReportInputItem_t;

/* UsbHidReportInputItem::Flags
 * Contains definitions and bitfield definitions for UsbHidReportInputItem::Flags */
#define REPORT_INPUT_TYPE_CONSTANT          0x0
#define REPORT_INPUT_TYPE_RELATIVE          0x1
#define REPORT_INPUT_TYPE_ABSOLUTE          0x2
#define REPORT_INPUT_TYPE_ARRAY             0x3

/* UsbHidReportCollectionItem
 * Each of these items describe some form of physical input. */
typedef struct _UsbHidReportCollectionItem {
    int                                 Type;
    MInputType_t                        InputType;
    void                               *Data;
    UsbHidReportGlobalStats_t           Stats;
    struct _UsbHidReportCollectionItem *Link;
} UsbHidReportCollectionItem_t;

/* UsbHidReportCollectionItem::Type
 * Contains definitions and bitfield definitions for UsbHidReportCollectionItem::Type */
#define HID_TYPE_COLLECTION                 0x0
#define HID_TYPE_INPUT                      0x1
#define HID_TYPE_OUTPUT                     0x2
#define HID_TYPE_FEATURE                    0x3

/* UsbHidReportCollection
 * Represents a collection of hid-items that each describe
 * some form of input. */
typedef struct _UsbHidReportCollection {
    size_t                           UsagePage;
    size_t                           Usage;
    
    UsbHidReportCollectionItem_t    *Childs;
    struct _UsbHidReportCollection  *Link;
} UsbHidReportCollection_t;

/* HidDevice
 * Represents a human input device. */
typedef struct _HidDevice {
    MCoreUsbDevice_t             Base;
    MContract_t                  Contract;

    // Buffers
    UsbHidReportCollection_t    *Collection;
    uintptr_t                    BufferAddress;
    uintptr_t                   *BufferPointer;
    uintptr_t                   *BufferPointerPrevious;
    size_t DataLength;
    
    // Endpoint Information
    UsbHcEndpointDescriptor_t   *Control;
    UsbHcEndpointDescriptor_t   *Interrupt;
} HidDevice_t;

/* HidDeviceCreate
 * Initializes a new hid-device from the given usb-device */
__EXTERN
HidDevice_t*
HidDeviceCreate(
    _In_ MCoreUsbDevice_t *UsbDevice);

/* HidDeviceDestroy
 * Destroys an existing hid device instance and cleans up
 * any resources related to it */
__EXTERN
OsStatus_t
HidDeviceDestroy(
    _In_ HidDevice_t *Device);

/* HidSetupGeneric 
 * Sets up a generic HID device like a mouse or a keyboard. */
__EXTERN
OsStatus_t
HidSetupGeneric(
    _In_ HidDevice_t *Device);

/* HidSetProtocol
 * Changes the current protocol of the device. 
 * 0 = Boot Protocol, 1 = Report Protocol */
__EXTERN
OsStatus_t
HidSetProtocol(
    _In_ HidDevice_t *Device,
    _In_ int Protocol);

/* HidSetIdle
 * Changes the current situation of the device to idle. 
 * Set ReportId = 0 to apply to all reports. 
 * Set Duration = 0 to apply indefinite duration. Use this
 * to set the report time-out time, minimum value is device polling rate */
__EXTERN
OsStatus_t
HidSetIdle(
    _In_ HidDevice_t *Device,
    _In_ int ReportId,
    _In_ int Duration);

#endif //!__USB_HID_H__
