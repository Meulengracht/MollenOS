/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Device Definitions & Structures
 * - This header describes the base device-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_DEVICE_H__
#define __DDK_DEVICE_H__

#include <ddk/ddkdefs.h>

#define __DEVICEMANAGER_NAMEBUFFER_LENGTH 64

#define DEVICE_REGISTER_FLAG_LOADDRIVER 0x00000001

// Device Information
// This is used both by the devicemanager and by the driver to match
typedef struct Device {
    uuid_t Id;
    uuid_t ParentId;
    size_t Length;
    char   Name[__DEVICEMANAGER_NAMEBUFFER_LENGTH];

    unsigned int VendorId;
    unsigned int ProductId;
    unsigned int Class;
    unsigned int Subclass;
} Device_t;

/* RegisterDevice
 * Allows registering of a new device in the
 * device-manager, and automatically queries for a driver for the new device */
DDKDECL(uuid_t,
        RegisterDevice(
    _In_ Device_t* Device, 
    _In_ unsigned int   Flags));

/* UnregisterDevice
 * Allows removal of a device in the device-manager, and automatically 
 * unloads drivers for the removed device */
DDKDECL(oscode_t,
        UnregisterDevice(
    _In_ uuid_t DeviceId));

#endif //!__DDK_DEVICE_H__
