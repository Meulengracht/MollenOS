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
 * (Bus) Device Definitions & Structures
 * - This header describes the base device-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_BUSDEVICE_H__
#define __DDK_BUSDEVICE_H__

#include <ddk/ddkdefs.h>
#include <ddk/device.h>
#include <ddk/io.h>

#define __DEVICEMANAGER_MAX_IOSPACES 6
#define __DEVICEMANAGER_IOSPACE_END  (int)-1

#define __DEVICEMANAGER_IOCTL_BUS    0x00000000
#define __DEVICEMANAGER_IOCTL_EXT    0x00000001

// Ioctl-Bus Specific Flags
#define __DEVICEMANAGER_IOCTL_ENABLE           0x00000001
#define __DEVICEMANAGER_IOCTL_IO_ENABLE        0x00000002
#define __DEVICEMANAGER_IOCTL_MMIO_ENABLE      0x00000004
#define __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE 0x00000008
#define __DEVICEMANAGER_IOCTL_FASTBTB_ENABLE   0x00000010  // Fast Back-To-Back

// Ioctl-Ext Specific Flags
#define __DEVICEMANAGER_IOCTL_EXT_WRITE        0x00000000
#define __DEVICEMANAGER_IOCTL_EXT_READ         0x80000000

// Device Bus Information 
// This describes the location on the bus, and these informations
// can be used to control the bus-device
typedef struct BusDevice {
    Device_t     Base;
    DeviceIo_t   IoSpaces[__DEVICEMANAGER_MAX_IOSPACES];
    int          InterruptLine;
    int          InterruptPin;
    unsigned int InterruptAcpiConform;
    unsigned int Segment;
    unsigned int Bus;
    unsigned int Slot;
    unsigned int Function;
} BusDevice_t;

/* IoctlDevice
 * Allows manipulation of a given device to either disable
 * or enable, or configure the device */
DDKDECL(OsStatus_t,
IoctlDevice(
    _In_ UUId_t  Device,
    _In_ Flags_t Command,
    _In_ Flags_t Flags));

/* IoctlDeviceEx
 * Allows manipulation of a given device to either disable
 * or enable, or configure the device.
 * <Direction> = 0 (Read), 1 (Write) */
DDKDECL(OsStatus_t,
IoctlDeviceEx(
    _In_    UUId_t  Device,
    _In_    int     Direction,
    _In_    Flags_t Register,
    _InOut_ size_t* Value,
    _In_    size_t  Width));

#endif //!__DDK_BUSDEVICE_H__
