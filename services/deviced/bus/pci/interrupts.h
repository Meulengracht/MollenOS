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

#ifndef __DEVICED_BUS_PCI_INTERRUPTS_H__
#define __DEVICED_BUS_PCI_INTERRUPTS_H__

#include <os/osdefs.h>

struct PciDevice;

/**
 * @brief Resolves the interrupt line and pin for the specified PCI device.
 *
 * @param parent The parent PCI device or bridge.
 * @param bus The bus number of the PCI device.
 * @param slot The slot number of the PCI device.
 * @param function The function number of the PCI device.
 * @param pciDevice The PCI device for which to resolve the interrupt line and pin.
 */
__EXTERN void
PciResolveInterruptLineAndPin(
    _In_ struct PciDevice* parent,
    _In_ int          bus,
    _In_ int          slot,
    _In_ int          function,
    _In_ struct PciDevice* pciDevice);

/**
 * @brief Reports whether ACPI interrupt routing is available.
 */
__EXTERN int
PciIsAcpiAvailable(void);

#endif // __DEVICED_BUS_PCI_INTERRUPTS_H__
