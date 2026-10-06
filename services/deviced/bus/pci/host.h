/**
 * MollenOS
 *
 * Copyright 2015, Philip Meulengracht
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
 * MollenOS PCI Bus Driver 
 * - Enumerates the bus and registers the devices/controllers
 *   available in the system
 */

#ifndef __DEVICED_PCI_HOST_H__
#define __DEVICED_PCI_HOST_H__

#include <os/osdefs.h>

// Forward declarations
typedef struct PciHost PciHost_t;

/**
 * @brief Identifies one controller and the PCI bus interval it owns.
 * HostId is assigned on successful registration and is never a segment number
 * or a device-manager ID. Initialize it to UUID_INVALID before registration.
 * Segment and the inclusive bus interval are supplied by the host constructor.
 * All fields remain unchanged while the host is registered.
 */
struct PciHostIdentification {
    uuid_t   HostId;
    uint32_t Segment;
    uint8_t  BusStart;
    uint8_t  BusEnd;
};

/**
 * @brief Registers a constructed PCI host without scanning or publishing devices.
 *
 * @param host Host with configuration operations and an ordered bus interval.
 *             Any firmware mapping reference must already be owned by the host.
 * @return OS_EOK transfers ownership to PCI and assigns a service-lifetime host
 *         ID. Overlapping intervals in the same segment or repeated registration
 *         return OS_EEXISTS. Disjoint intervals and different segments may coexist.
 *         On failure, the caller retains the unchanged host and its resources.
 *         The caller serializes discovery and teardown; request lookup uses the
 *         PCI registry lock. BusEnumerate initializes that lock at service startup.
 */
extern oserr_t
PciHostRegister(
    _In_ struct PciHost* host);

#endif
