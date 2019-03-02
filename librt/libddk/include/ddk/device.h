/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Device Definitions & Structures
 * - This header describes the base device-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _DEVICE_INTERFACE_H_
#define _DEVICE_INTERFACE_H_

#include <os/osdefs.h>
#include <ddk/interrupt.h>
#include <ddk/io.h>

#define __DEVICEMANAGER_INTERFACE_VERSION           1

#define __DEVICEMANAGER_REGISTERDEVICE              IPC_DECL_FUNCTION(0)
#define __DEVICEMANAGER_UNREGISTERDEVICE            IPC_DECL_FUNCTION(1)
#define __DEVICEMANAGER_QUERYDEVICE                 IPC_DECL_FUNCTION(2)
#define __DEVICEMANAGER_IOCTLDEVICE                 IPC_DECL_FUNCTION(3)

#define __DEVICEMANAGER_REGISTERCONTRACT            IPC_DECL_FUNCTION(4)
#define __DEVICEMANAGER_UNREGISTERCONTRACT          IPC_DECL_FUNCTION(5)
#define __DEVICEMANAGER_QUERYCONTRACT               IPC_DECL_FUNCTION(6)

#define __DEVICEMANAGER_NAMEBUFFER_LENGTH           128
#define __DEVICEMANAGER_MAX_IOSPACES                6
#define __DEVICEMANAGER_IOSPACE_END                 (int)-1

/* MCoreDevice ACPI Conform flags
 * This is essentially some bonus information that is
 * needed when registering interrupts */
#define __DEVICEMANAGER_ACPICONFORM_PRESENT         0x00000001
#define __DEVICEMANAGER_ACPICONFORM_TRIGGERMODE     0x00000002
#define __DEVICEMANAGER_ACPICONFORM_POLARITY        0x00000004
#define __DEVICEMANAGER_ACPICONFORM_SHAREABLE       0x00000008
#define __DEVICEMANAGER_ACPICONFORM_FIXED           0x00000010

/* MCoreDevice Register Flags
 * Flags related to registering of new devices */
#define __DEVICEMANAGER_REGISTER_LOADDRIVER         0x00000001

/* MCoreDevice IoCtrl Flags
 * Flags related to registering of new devices */
#define __DEVICEMANAGER_IOCTL_BUS                   0x00000000
#define __DEVICEMANAGER_IOCTL_EXT                   0x00000001

// Ioctl-Bus Specific Flags
#define __DEVICEMANAGER_IOCTL_ENABLE                0x00000001
#define __DEVICEMANAGER_IOCTL_IO_ENABLE             0x00000002
#define __DEVICEMANAGER_IOCTL_MMIO_ENABLE           0x00000004
#define __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE      0x00000008
#define __DEVICEMANAGER_IOCTL_FASTBTB_ENABLE        0x00000010  // Fast Back-To-Back

// Ioctl-Ext Specific Flags
#define __DEVICEMANAGER_IOCTL_EXT_WRITE             0x00000000
#define __DEVICEMANAGER_IOCTL_EXT_READ              0x80000000

PACKED_TYPESTRUCT(MCoreDevice, {
    UUId_t              Id;
    char                Name[__DEVICEMANAGER_NAMEBUFFER_LENGTH];
    size_t              Length;

    // Device Information
    // This is used both by the devicemanager and by the driver to match
    DevInfo_t           VendorId;
    DevInfo_t           DeviceId;
    DevInfo_t           Class;
    DevInfo_t           Subclass;
    DeviceInterrupt_t   Interrupt;
    DeviceIo_t          IoSpaces[__DEVICEMANAGER_MAX_IOSPACES];

    // Device Bus Information 
    // This describes the location on the bus, and these informations
    // can be used to control the bus-device
    DevInfo_t           Segment;
    DevInfo_t           Bus;
    DevInfo_t           Slot;
    DevInfo_t           Function;
});

/* RegisterDevice
 * Allows registering of a new device in the
 * device-manager, and automatically queries for a driver for the new device */
CRTDECL(UUId_t,
RegisterDevice(
    _In_ UUId_t         Parent,
    _In_ MCoreDevice_t* Device, 
    _In_ Flags_t        Flags));

/* UnregisterDevice
 * Allows removal of a device in the device-manager, and automatically 
 * unloads drivers for the removed device */
CRTDECL(OsStatus_t,
UnregisterDevice(
    _In_ UUId_t DeviceId));

/* IoctlDevice
 * Allows manipulation of a given device to either disable
 * or enable, or configure the device */
CRTDECL(OsStatus_t,
IoctlDevice(
    _In_ UUId_t  Device,
    _In_ Flags_t Command,
    _In_ Flags_t Flags));

/* IoctlDeviceEx
 * Allows manipulation of a given device to either disable
 * or enable, or configure the device.
 * <Direction> = 0 (Read), 1 (Write) */
CRTDECL(OsStatus_t,
IoctlDeviceEx(
    _In_    UUId_t   Device,
    _In_    int      Direction,
    _In_    Flags_t  Register,
    _InOut_ Flags_t* Value,
    _In_    size_t   Width));

/* InstallDriver 
 * Tries to find a suitable driver for the given device
 * by searching storage-medias for the vendorid/deviceid 
 * combination or the class/subclass combination if specific
 * is not found */
CRTDECL(OsStatus_t,
InstallDriver(
    _In_ MCoreDevice_t* Device, 
    _In_ size_t         Length,
    _In_ const void*    DriverBuffer,
    _In_ size_t         DriverBufferLength));

#endif //!_DEVICE_INTERFACE_H_
