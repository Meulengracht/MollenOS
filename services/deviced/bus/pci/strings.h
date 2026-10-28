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
 * @brief Gets a device description from its PCI class codes.
 *
 * @param Class The base class from the PCI configuration header.
 * @param SubClass The subclass within the base class.
 * @param Interface The programming interface within the subclass.
 * @return A static string describing the device. The caller must not free it.
 */
__EXTERN const char*
PciToString(
    _In_ uint8_t Class,
    _In_ uint8_t SubClass,
    _In_ uint8_t Interface);

/**
 * @brief Packs the PCI class and subclass into a device-manager class identifier.
 *
 * @param Class The PCI base class. Only the low 16 bits are used.
 * @param SubClass The PCI subclass. Only the low 16 bits are used.
 * @return The class in bits 16 through 31 and the subclass in bits 0 through 15.
 */
__EXTERN unsigned int
PciToDevClass(
    _In_ uint32_t Class,
    _In_ uint32_t SubClass);

/**
 * @brief Packs the PCI programming interface into a device-manager subclass identifier.
 *
 * @param Interface The PCI programming interface. Only the low 16 bits are used.
 * @return The interface in bits 16 through 31, with bits 0 through 15 set to zero.
 */
__EXTERN unsigned int
PciToDevSubClass(
    _In_ uint32_t Interface);

#endif // __DEVICED_BUS_PCI_STRINGS_H__
