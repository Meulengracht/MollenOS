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

#include <os/osdefs.h>

/**
 * @brief Finds PCI controllers described by firmware and scans their buses.
 * On supported systems, scans legacy PCI if no firmware-described host starts.
 */
__EXTERN void
BusEnumerate(void);

#endif // __DEVICED_BUS_PCI_DISCOVERY_H__
