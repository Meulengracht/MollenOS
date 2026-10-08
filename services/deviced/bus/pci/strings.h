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

#ifndef __DEVICED_BUS_PCI_STRINGS_H__
#define __DEVICED_BUS_PCI_STRINGS_H__

#include <os/osdefs.h>

/**
 * @brief Converts the given class, subclass and interface into descriptive string to give the pci-entry a description.
 */
__EXTERN const char*
PciToString(
    _In_ uint8_t Class,
    _In_ uint8_t SubClass,
    _In_ uint8_t Interface);


/**
 * @brief Converts PCI class codes to device-manager identifiers.
 */
__EXTERN unsigned int
PciToDevClass(
    _In_ uint32_t Class,
    _In_ uint32_t SubClass);
__EXTERN unsigned int
PciToDevSubClass(
    _In_ uint32_t Interface);

#endif // __DEVICED_BUS_PCI_STRINGS_H__
