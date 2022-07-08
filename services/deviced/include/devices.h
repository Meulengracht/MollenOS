/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */

#ifndef __DEVICES_H__
#define __DEVICES_H__

#include <os/osdefs.h>

DECL_STRUCT(Device);
DECL_STRUCT(BusDevice);

/**
 * @brief Initializes the device manager systems
 */
extern void DmDevicesInitialize(void);

/**
 * @brief Notifies a driver of a new device for the driver.
 *
 * @param[In] driverHandle
 * @param[In] deviceId
 * @return
 */
extern oserr_t
DmDevicesRegister(
        _In_ uuid_t driverHandle,
        _In_ uuid_t deviceId);

/**
 * @brief Creates a new device in the device manager. This will automatically try to resolve
 * a driver for the device.
 *
 * @param device
 * @param name
 * @param flags
 * @param idOut
 * @return
 */
extern oserr_t
DmDeviceCreate(
        _In_  Device_t*    device,
        _In_  const char*  name,
        _In_  unsigned int flags,
        _Out_ uuid_t*      idOut);

/**
 * Allows removal of a device in the device-manager, and automatically 
 * unloads drivers for the removed device */
extern oserr_t
DmDeviceDestroy(
        _In_ uuid_t DeviceId);

/* DmIoctlDevice
 * Allows manipulation of a given device to either disable
 * or enable, or configure the device */
extern oserr_t
DmIoctlDevice(
    _In_ BusDevice_t* Device,
    _In_ unsigned int Command,
    _In_ unsigned int Flags);

/* DmIoctlDeviceEx
 * Allows manipulation of a given device to either disable
 * or enable, or configure the device.
 * <Direction> = 0 (Read), 1 (Write) */
extern oserr_t
DmIoctlDeviceEx(
	_In_ BusDevice_t* device,
	_In_ int          direction,
	_In_ unsigned int Register,
	_In_ size_t*      value,
	_In_ size_t       width);

#endif //!__DEVICES_H__
