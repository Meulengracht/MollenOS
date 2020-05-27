/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * MollenOS MCore - Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */

#ifndef __DEVICEMANAGER_INTERFACE__
#define __DEVICEMANAGER_INTERFACE__

#include <os/osdefs.h>

DECL_STRUCT(Device);
DECL_STRUCT(BusDevice);

/* DmRegisterDevice
 * Allows registering of a new device in the
 * device-manager, and automatically queries for a driver for the new device */
__EXTERN
OsStatus_t
DmRegisterDevice(
	_In_  Device_t*   device,
	_In_  const char* name,
	_In_  unsigned int     flags,
	_Out_ UUId_t*     idOut);

/* DmUnregisterDevice
 * Allows removal of a device in the device-manager, and automatically 
 * unloads drivers for the removed device */
__EXTERN
OsStatus_t
DmUnregisterDevice(
	_In_ UUId_t DeviceId);

/* DmIoctlDevice
 * Allows manipulation of a given device to either disable
 * or enable, or configure the device */
__EXTERN
OsStatus_t
DmIoctlDevice(
    _In_ BusDevice_t* Device,
    _In_ unsigned int Command,
    _In_ unsigned int Flags);

/* DmIoctlDeviceEx
 * Allows manipulation of a given device to either disable
 * or enable, or configure the device.
 * <Direction> = 0 (Read), 1 (Write) */
__EXTERN
OsStatus_t
DmIoctlDeviceEx(
	_In_ BusDevice_t* Device,
	_In_ int          Direction,
	_In_ unsigned int Register,
	_In_ size_t*      Value,
	_In_ size_t       Width);

#endif //! __DEVICEMANAGER_INTERFACE__
