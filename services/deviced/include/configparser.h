/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */

#ifndef __RAMDISK_H__
#define __RAMDISK_H__

/**
 * @brief Maps the ramdisk in to memory space and parses it for all available drivers
 */
extern void
DmRamdiskDiscover(void);

/**
 * @brief Parses a yaml configuration file and if valid, registers a new driver object.
 *
 * @param[In]  yaml   The yaml file content.
 * @param[In]  length The length of the file content.
 * @param[Out] driverConfig The driver configuration instance to store the parsed data.
 * @return     Returns OsOK if the yaml configuration was valid, otherwise OsError
 */
extern oscode_t
DmDriverConfigParseYaml(
        _In_  const uint8_t*              yaml,
        _In_  size_t                      length,
        _Out_ struct DriverConfiguration* driverConfig);

/**
 * @brief Cleans up a previously allocated driver configuration structure.
 *
 * @param[In] driverConfig A pointer to the driver configuration class that should be freed.
 */
extern void
DmDriverConfigDestroy(
        _In_ struct DriverConfiguration* driverConfig);

#endif //!__RAMDISK_H__
