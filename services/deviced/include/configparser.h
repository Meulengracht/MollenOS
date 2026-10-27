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
 * @brief Maps the ramdisk and discovers the drivers stored in it.
 */
extern void
DmRamdiskDiscover(void);

/**
 * @brief Parses YAML into caller-supplied driver configuration storage.
 *
 * The output storage must have no allocated configuration data. On success,
 * the caller owns the parsed configuration. On failure, the output is unchanged.
 *
 * @param[In]  yaml         The YAML file content.
 * @param[In]  length       The file content length in bytes. Must be nonzero.
 * @param[Out] driverConfig Storage for the parsed configuration.
 * @return OS_EOK on success, OS_EINVALPARAMS for missing input or output,
 *         OS_EOOM if parser initialization fails, or OS_EUNKNOWN for invalid
 *         YAML or rejected configuration data.
 */
extern oserr_t
DmDriverConfigParseYaml(
    _In_  const uint8_t*              yaml,
    _In_  size_t                      length,
    _Out_ struct DriverConfiguration* driverConfig);

/**
 * @brief Frees the configuration data and the configuration structure itself.
 *
 * @param[In] driverConfig A heap-allocated configuration initialized by a
 *                        successful parse, or NULL.
 */
extern void
DmDriverConfigDestroy(
    _In_ struct DriverConfiguration* driverConfig);

#endif //!__RAMDISK_H__
