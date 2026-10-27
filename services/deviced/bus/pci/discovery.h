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
 */

#ifndef __DEVICED_BUS_PCI_DISCOVERY_H__
#define __DEVICED_BUS_PCI_DISCOVERY_H__

#include <startup.h>

/**
 * @brief Discover hosts, finish pending cleanup, and retry scans and publication.
 *
 * Call PciInitialize first. Serialize calls with publication and host removal.
 * Existing hosts are not initialized again. An independent host may publish while
 * another is blocked. Failed cleanup defers new host construction. Unsupported
 * host descriptions are logged and skipped. After a permanent startup failure,
 * the host's descriptions remain blocked until explicit unpublication or
 * destruction.
 *
 * @return First error and combined outcome. DmStartupRetry means cleanup or host
 *         startup needs another attempt, even if a different host has failed
 *         permanently.
 */
__EXTERN struct DmStartupResult
BusRecoverAndRepublish(void);

#endif // __DEVICED_BUS_PCI_DISCOVERY_H__
