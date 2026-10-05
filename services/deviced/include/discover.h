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

#ifndef __DISCOVER_H__
#define __DISCOVER_H__

#include <os/osdefs.h>
#include <ds/mstring.h>
#include <ds/list.h>

struct DriverProduct {
    element_t ListHeader;
    uint32_t  Id;
};

struct DriverVendor {
    element_t ListHeader;
    uint32_t  Id;
    list_t    Products;
};

struct DriverResource {
    element_t ListHeader;
    int       Type;
    uintptr_t Base;
    size_t    Length;
};

struct DriverCompatible {
    element_t ListHeader;
    char*     Name;
};

struct DriverConfiguration {
    uint32_t Class;
    uint32_t Subclass;
    list_t   Vendors;
    list_t   Compatibles;
    list_t   Resources;
};

struct DriverIdentification {
    int         IsPlatform;

    // One of the identification methods are the
    // compatible string. Especially on devices that use DT
    // enumeration instead of ACPI.
    const char* Compatibles;
    size_t      CompatibleLength;

    // The other common identification method is the PCI bus identifiers.
    uint32_t    VendorId;
    uint32_t    ProductId;

    uint32_t    Class;
    uint32_t    Subclass;
};

/**
 * @brief Checks whether a driver's settings match a device.
 * A positive score means the driver matches; a lower score means a better
 * match. Platform devices are matched using complete device descriptions
 * provided by the firmware, in the order provided by the device.
 *
 * @param configuration The settings that describe which devices the driver supports.
 * @param identification The identifiers and class information for the device.
 * @return A positive match score, or zero if the driver does not match.
 */
__EXTERN unsigned int
DmDriverMatchScore(
    _In_ const struct DriverConfiguration*  configuration,
    _In_ const struct DriverIdentification* identification);

/**
 * @brief Removes a device from the device lists maintained for drivers.
 * Call this after stopping programs that use the device. This function only
 * clears the driver's tracking information; it does not stop those programs.
 *
 * @param deviceId The ID of the device to remove from the driver lists.
 */
__EXTERN void
DmDiscoverForgetDevice(
    _In_ uuid_t deviceId);

/**
 * @brief Starts driver discovery and checks existing devices for matching drivers.
 * Driver discovery reads the available drivers and then tries to match them
 * with devices already known to the system.
 */
__EXTERN void
DmDiscoverInitialize(void);

/**
 * @brief Adds a driver to the list of drivers that can be matched with devices.
 *
 * @param driverPath The path to the driver's program.
 * @param driverConfig The settings that describe which devices the driver supports.
 * @return OS_EOK if the driver was added, or an error code if it could not be added.
 */
__EXTERN oserr_t
DmDiscoverAddDriver(
    _In_ mstring_t*                  driverPath,
    _In_ struct DriverConfiguration* driverConfig);

/**
 * @brief Removes a driver from the list of drivers that can be matched with devices.
 * A driver that has started loading is kept in the list.
 *
 * @param driverPath The path of the driver to remove.
 * @return OS_EOK if the driver is known, or OS_ENOENT if no driver has this path.
 */
__EXTERN oserr_t
DmDiscoverRemoveDriver(
    _In_ mstring_t* driverPath);

/**
 * @brief Finds the best matching driver for a device and starts or connects it.
 *
 * @param deviceId The ID of the device to find a driver for.
 * @param deviceIdentification The identifiers and class information used to find a match.
 * @return OS_EOK if a matching driver was started or connected, or an error code
 *         if the device cannot be matched or the driver cannot be started or connected.
 */
__EXTERN oserr_t
DmDiscoverFindDriver(
    _In_ uuid_t                       deviceId,
    _In_ struct DriverIdentification* deviceIdentification);

#endif //!__DISCOVER_H__
