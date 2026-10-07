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
 * @brief Identifies one PCI controller and the bus numbers it can access.
 * Registration assigns HostId; it is neither the PCI segment nor a device ID.
 * Set it to UUID_INVALID before registration. The constructor supplies Segment
 * and the inclusive bus range. These values do not change while the host is active.
 */
struct PciHostIdentification {
    uuid_t   HostId;
    uint32_t Segment;
    uint8_t  BusStart;
    uint8_t  BusEnd;
};

/**
 * @brief Registers a constructed PCI host without scanning its buses or adding devices.
 *
 * @param host Host with configuration access and a valid bus range. The host must
 *             already hold a reference to any firmware data it uses.
 * @return OS_EOK transfers ownership to PCI and assigns a unique host ID that is
 *         not reused. Registration fails if the host is already registered or its
 *         bus range overlaps another host in the same segment. On failure, the
 *         caller still owns the unchanged host and its resources. The caller must
 *         not run discovery and teardown at the same time. PCI protects request
 *         lookups with a lock; call PciInitialize before registration or teardown.
 */
__EXTERN oserr_t
PciHostRegister(
    _In_ struct PciHost* host);

/**
 * @brief Initializes PCI once during startup when no other thread is changing
 * the host list. Calling it again leaves registered hosts in place.
 */
__EXTERN void
PciInitialize(void);

#endif
