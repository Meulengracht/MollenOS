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

#ifndef __DEVICED_BUS_PCI_ENUMERATE_H__
#define __DEVICED_BUS_PCI_ENUMERATE_H__

#include <bus/pci/host.h>

struct PciDevice;

/**
 * @brief Scans a bus for devices and follows any bridges it finds.
 * @param parent Device or host root that owns this bus.
 * @param bus Bus number to scan.
 */
__EXTERN void
PciCheckBus(
    _In_ struct PciDevice* parent,
    _In_ int               bus);

#ifdef __OSCONFIG_HAS_LEGACY_PCI

/**
 * @brief Selects present legacy root functions and scans their buses.
 *
 * @param host The host to scan for legacy PCI roots.
 */
__EXTERN void
PciScanLegacyRoots(
    _In_ PciHost_t* host);

#endif //!__OSCONFIG_HAS_LEGACY_PCI

#endif // __DEVICED_BUS_PCI_ENUMERATE_H__
