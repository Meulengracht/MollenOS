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
#include <os/types/device.h>

DECL_STRUCT(Device);
DECL_STRUCT(BusDevice);

/**
 * @brief Initializes the device registry.
 */
extern void DmDevicesInitialize(void);

/**
 * @brief Tries to find drivers for devices that do not have one yet.
 * Call this after adding drivers so devices that were already registered can
 * also be matched.
 */
extern void DmDeviceRefreshDrivers(void);

/**
 * @brief Sends a device description to its selected driver.
 *
 * @param driverHandle The ID of the driver that should receive the device.
 * @param deviceId The ID of the device to send.
 * @return OS_EOK if the request was sent, or an error code if the device has
 *         no driver, already has a driver, or the request could not be sent.
 */
extern oserr_t
DmDevicesRegister(
        _In_ uuid_t driverHandle,
        _In_ uuid_t deviceId);

/**
 * @brief Adds a device to the registry and optionally looks for a driver.
 * When this succeeds, the registry owns the device description and frees it
 * when the device is destroyed. Set DEVICE_REGISTER_FLAG_LOADDRIVER in flags
 * to look for a driver as soon as the device is added.
 *
 * @param device The device description to add.
 * @param flags Options that control how the device is registered.
 * @param idOut Receives the ID assigned to the new device.
 * @return OS_EOK if the device was added, or an error code if its description
 *         is invalid or the registry could not store it.
 */
extern oserr_t
DmDeviceCreate(
        _In_  Device_t*    device,
        _In_  unsigned int flags,
        _Out_ uuid_t*      idOut);

/**
 * @brief Allows the registry to look for a driver for an existing device.
 * This is useful when the device was added before it was ready for a driver.
 *
 * @param deviceId The ID of the device to check for a driver.
 * @return OS_EOK if the device was found, or OS_ENOENT if it is not registered.
 */
extern oserr_t
DmDeviceEnableDriverBinding(
    _In_ uuid_t deviceId);

/**
 * @brief Checks whether a registered device may be matched with a driver.
 *
 * @param deviceId The ID of the device to check.
 * @return A nonzero value if the device exists and may be matched; otherwise zero.
 */
extern int
DmDeviceIsBindable(
    _In_ uuid_t deviceId);

/**
 * @brief Removes a device and frees its description.
 * Stop programs using the device before calling this function, and remove its
 * child devices first. This function does not stop those programs or unload a driver.
 *
 * @param deviceId The ID of the device to remove.
 * @return OS_EOK if the device was removed, OS_ENOENT if it was not registered,
 *         or OS_EBUSY if it still has child devices.
 */
extern oserr_t
DmDeviceDestroy(
        _In_ uuid_t DeviceId);

/**
 * @brief Applies a control request to a bus device.
 * The request flags specify which bus settings to change.
 *
 * @param device The bus device to control.
 * @param request The bus settings to apply.
 * @return OS_EOK if the request succeeded, or an error code if the request
 *         is not supported or could not be applied.
 */
extern oserr_t
DMBusControl(
    _In_ BusDevice_t*              device,
    _In_ struct OSIOCtlBusControl* request);

/**
 * @brief Reads or writes a register on a bus device.
 * For a read, the register value is written to value. For a write, value holds
 * the data to write. The width must be 1, 2, or 4 bytes.
 *
 * @param device The bus device whose register is accessed.
 * @param direction Whether to read from or write to the register.
 * @param Register The register offset to access.
 * @param value Receives the value read, or supplies the value to write.
 * @param width The number of bytes to read or write: 1, 2, or 4.
 * @return OS_EOK if the access succeeded, or an error code if the register
 *         could not be accessed or the width is invalid.
 */
extern oserr_t
DmIoctlDeviceEx(
	_In_ BusDevice_t* device,
	_In_ int          direction,
	_In_ unsigned int Register,
	_In_ size_t*      value,
	_In_ size_t       width);

#endif //!__DEVICES_H__
