/**
 * MollenOS
 *
 * Copyright 2021, Philip Meulengracht
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
 * Usb Hub Device Driver
 */

#ifndef __USB_HUB_H__
#define __USB_HUB_H__

#include <ddk/usbdevice.h>
#include <ds/list.h>
#include <os/osdefs.h>

/**
 * HUB Class Definitions
 * Contains generic magic constants and definitions
 */
#define HUB_CLASS                           0x9

/**
 * HUB Subclass Definitions
 * Contains generic magic constants and definitions
 */
#define HUB_SUBCLASS_NONE                   0x0

/**
 * HUB Protocol Definitions
 * Contains generic magic constants and definitions
 */
#define HUB_PROTOCOL_FULLSPEED              0x0
#define HUB_PROTOCOL_HISPEED_SINGLE_TT      0x1
#define HUB_PROTOCOL_HISPEED_MULTI_TT       0x2

/**
 * HUB bRequest Definitions
 * Contains generic magic constants and definitions
 */
#define HUB_REQUEST_CLEAR_TT_BUFFER 0x8
#define HUB_REQUEST_RESET_TT        0x9
#define HUB_REQUEST_GET_TT_STATE    0xA
#define HUB_REQUEST_STOP_TT         0xB
#define HUB_REQUEST_SET_DEPTH       0xC

/**
 * HUB Class Features Definitions
 * Contains generic magic constants and definitions
 */
#define HUB_FEATURE_C_HUB_LOCAL_POWER  0
#define HUB_FEATURE_C_HUB_OVER_CURRENT 1

#define HUB_FEATURE_PORT_CONNECTION     0
#define HUB_FEATURE_PORT_ENABLE         1
#define HUB_FEATURE_PORT_SUSPEND        2
#define HUB_FEATURE_PORT_OVER_CURRENT   3
#define HUB_FEATURE_PORT_RESET          4
#define HUB_FEATURE_PORT_POWER          8
#define HUB_FEATURE_PORT_LOW_SPEED      9
#define HUB_FEATURE_C_PORT_CONNECTION   16
#define HUB_FEATURE_C_PORT_ENABLE       17
#define HUB_FEATURE_C_PORT_SUSPEND      18
#define HUB_FEATURE_C_PORT_OVER_CURRENT 19
#define HUB_FEATURE_C_PORT_RESET        20
#define HUB_FEATURE_PORT_TEST           21
#define HUB_FEATURE_PORT_INDICATOR      22

/**
 * HUB Descriptor Definitions
 * Contains generic magic constants and definitions
 */
#define DESCRIPTOR_TYPE_HUB                 0x29
#define DESCRIPTOR_TYPE_HUB_SUPERSPEED      0x2A

/**
 * UsbHubDescriptor
 * A descriptor containing the setup of the HUB device
 */
PACKED_TYPESTRUCT(UsbHubDescriptor, {
    uint8_t                         Length;
    uint8_t                         Type;
    uint8_t                         NumberOfPorts;
    uint16_t                        HubCharacteristics;
    uint8_t                         PowerOnDelay;        // Time in 2ms units that are needed before power on is done
    uint8_t                         MaxMilliAmpsDraw;
    uint8_t                         BitmapRemovable[8];  // Bit 0 is reserved. Not zero indexing
});

#define HUB_CHARACTERISTICS_POWERMODE(value)     (value & 0x3)
#define HUB_CHARACTERISTICS_POWERMODE_GLOBAL      0 // All ports are powered at once
#define HUB_CHARACTERISTICS_POWERMODE_INDIVIDUAL  1 // Each port can be powered individually

#define HUB_CHARACTERISTICS_COMPOUND_DEVICE       0x4

#define HUB_CHARACTERISTICS_OCMODE(value)        ((value >> 3) & 0x3)
#define HUB_CHARACTERISTICS_OCMODE_GLOBAL        0 // Overcurrent is reported as a summation of all ports power draw
#define HUB_CHARACTERISTICS_OCMODE_INDIVIDUAL    1 // Overcurrent is reported on individual port basis

#define HUB_CHARACTERISTICS_TTMODE(value)        ((value >> 5) & 0x3)
#define HUB_CHARACTERISTICS_TTMODE_8FS           0 // TT requires at most 8 FS bit times of inter transaction gap on a full-/low-speed downstream bus.
#define HUB_CHARACTERISTICS_TTMODE_16FS          1
#define HUB_CHARACTERISTICS_TTMODE_24FS          2
#define HUB_CHARACTERISTICS_TTMODE_32FS          3

#define HUB_CHARACTERISTICS_PORT_INDICATORS      0x80

/**
 * UsbHubSuperDescriptor
 * A descriptor containing the setup of the super-speed HUB device
 */
PACKED_TYPESTRUCT(UsbHubSuperDescriptor, {
    uint8_t                         Length;
    uint8_t                         Type;
    uint8_t                         NumberOfPorts;
    uint16_t                        HubCharacteristics;
    uint8_t                         PowerOnDelay;        // Time in 2ms units that are needed before power on is done
    uint8_t                         MaxMilliAmpsDraw;
    uint8_t                         HeaderDecLat;        // Hub Packet Header Decode Latency
    uint16_t                        HubDelay;            // This field defines the maximum delay in nanoseconds a hub introduces while forwarding packets in either direction.
    uint8_t                         BitmapRemovable[8];  // Bit 0 is reserved. Not zero indexing
});

PACKED_TYPESTRUCT(HubStatus, {
    uint16_t Status;
    uint16_t Change;
});

#define HUB_STATUS_LOCAL_POWER_ACTIVE 0x1
#define HUB_STATUS_OVERCURRENT_ACTIVE 0x2

#define HUB_CHANGE_LOCAL_POWER        0x1  // Local Power Status has changed.
#define HUB_CHANGE_OVERCURRENT        0x2  // Over-Current Status has changed

PACKED_TYPESTRUCT(PortStatus, {
    uint16_t Status;
    uint16_t Change;
});

#define HUB_PORT_STATUS_CONNECTED   0x1
#define HUB_PORT_STATUS_ENABLED     0x2
#define HUB_PORT_STATUS_SUSPENDED   0x4 // Suspended or resuming
#define HUB_PORT_STATUS_OVERCURRENT 0x8
#define HUB_PORT_STATUS_RESET       0x10
#define HUB_PORT_STATUS_POWER       0x100
#define HUB_PORT_STATUS_LOWSPEED    0x200
#define HUB_PORT_STATUS_HIGHSPEED   0x400
#define HUB_PORT_STATUS_TESTMODE    0x800
#define HUB_PORT_STATUS_INDICATOR   0x1000 // 0 = Default Colors, 1 = Software controlled colors

#define HUB_PORT_CHANGE_CONNECTED   0x1
#define HUB_PORT_CHANGE_DISABLE     0x2 // This field is set to one when a port is disabled because of a Port_Error condition
#define HUB_PORT_CHANGE_SUSPEND     0x4 // If set, Resume complete.
#define HUB_PORT_CHANGE_OVERCURRENT 0x8 // Over-Current Indicator has changed.
#define HUB_PORT_CHANGE_RESET       0x10 // Reset complete.

typedef struct HubDevice {
    UsbDevice_t   Base;
    element_t     Header;
    UsbTransfer_t Transfer;
    UUId_t        TransferId;

    uint8_t       PortCount;
    uint8_t       DescriptorLength;
    uint16_t      HubCharacteristics;
    unsigned int  PowerOnDelay;

    uintptr_t*                 Buffer;
    uint8_t                    InterfaceId;
    usb_endpoint_descriptor_t* Interrupt;
} HubDevice_t;

/**
 *
 * @param usbDevice
 * @return
 */
__EXTERN HubDevice_t*
HubDeviceCreate(
    _In_ UsbDevice_t *usbDevice);

/**
 *
 * @param hubDevice
 */
__EXTERN void
HubDeviceDestroy(
    _In_ HubDevice_t *hubDevice);

/**
 *
 * @param hubDevice
 * @param portIndex
 * @return
 */
__EXTERN OsStatus_t
HubPowerOnPort(
        _In_ HubDevice_t*  hubDevice,
        _In_ uint8_t       portIndex);

/**
 *
 * @param hubDevice
 * @param status
 * @return
 */
__EXTERN OsStatus_t
HubGetStatus(
        _In_ HubDevice_t* hubDevice,
        _In_ HubStatus_t* status);

/**
 *
 * @param hubDevice
 * @param change
 * @return
 */
__EXTERN OsStatus_t
HubClearChange(
        _In_ HubDevice_t*  hubDevice,
        _In_ uint8_t       change);

/**
 *
 * @param hubDevice
 * @param portIndex
 * @param status
 * @return
 */
__EXTERN OsStatus_t
HubGetPortStatus(
        _In_ HubDevice_t*  hubDevice,
        _In_ uint8_t       portIndex,
        _In_ PortStatus_t* status);

/**
 *
 * @param hubDevice
 * @param portIndex
 * @param change
 * @return
 */
__EXTERN OsStatus_t
HubPortClearChange(
        _In_ HubDevice_t*  hubDevice,
        _In_ uint8_t       portIndex,
        _In_ uint8_t       change);

/**
 *
 * @param hubDevice
 * @param portIndex
 * @return
 */
__EXTERN OsStatus_t
HubResetPort(
        _In_ HubDevice_t*  hubDevice,
        _In_ uint8_t       portIndex);

__EXTERN void
HubInterrupt(
    _In_ HubDevice_t* hubDevice,
    _In_ size_t       dataIndex);


static inline int __IsHubPortsPoweredGlobal(
        _In_ HubDevice_t* hubDevice)
{
    if (HUB_CHARACTERISTICS_POWERMODE(hubDevice->HubCharacteristics) == HUB_CHARACTERISTICS_POWERMODE_GLOBAL) {
        return 1;
    }
    return 0;
}

static inline int __IsHubOverCurrentGlobal(
        _In_ HubDevice_t* hubDevice)
{
    if (HUB_CHARACTERISTICS_OCMODE(hubDevice->HubCharacteristics) == HUB_CHARACTERISTICS_OCMODE_GLOBAL) {
        return 1;
    }
    return 0;
}

#endif //!__USB_HUB_H__
