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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
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

struct DriverIdentification {
    uint32_t VendorId;
    uint32_t ProductId;
};

/**
 * @brief Initializes the discover subsystems that finds available drivers in the system
 * and also manages the state of those drivers.
 */
extern void
DmDiscoverInitialize(void);

/**
 * @brief
 *
 * @param[In] driverPath
 * @param[In] identifiers
 * @param[In] identifiersCount
 * @return
 */
OsStatus_t
DmDiscoverAddDriver(
        _In_ MString_t*                   driverPath,
        _In_ struct DriverIdentification* identifiers,
        _In_ int                          identifiersCount);

/**
 *
 * @param[In] driverPath
 * @return
 */
OsStatus_t
DmDiscoverRemoveDriver(
        _In_ MString_t* driverPath);

/**
 * @brief
 *
 * @param[In] deviceId
 * @param[In] deviceIdentification
 * @return
 */
OsStatus_t
DmDiscoverFindDriver(
        _In_ UUId_t                       deviceId,
        _In_ struct DriverIdentification* deviceIdentification);

#endif //!__DISCOVER_H__
