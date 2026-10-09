/**
 * MollenOS
 *
 * Copyright (C) Philip Meulengracht
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
 * Legacy configuration-port host construction.
 * 
 */

#ifndef __DEVICED_PCI_LEGACY_H__
#define __DEVICED_PCI_LEGACY_H__

#include <bus/pci/host.h>

#ifdef __OSCONFIG_HAS_LEGACY_PCI

/**
 * @brief Creates a host and acquires the legacy PCI I/O ports, but does not
 * register it or scan its buses. The caller owns the host until registration
 * succeeds. On failure, leaves hostOut NULL and frees partial resources.
 */
__EXTERN oserr_t
PciLegacyHostCreate(
    _Out_ PciHost_t** hostOut);

#endif //!__OSCONFIG_HAS_LEGACY_PCI

#endif //!__DEVICED_PCI_LEGACY_H__
