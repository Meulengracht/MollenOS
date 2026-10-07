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
 * ECAM host construction, independent of discovery and scanning.
 * 
 */

#ifndef __DEVICED_PCI_ECAM_H__
#define __DEVICED_PCI_ECAM_H__

#include "../bus.h"

/**
 * @brief Base is the physical address of BusStart; each bus uses 1 MiB of
 * configuration space. Firmware is optional. If supplied, its data must stay
 * mapped until host registration takes its own reference.
 */
struct PciEcamHostConfiguration {
    struct PciHostIdentification Identification;
    uint64_t                     Base;
    enum PciIoResourcePolicy     IoResourcePolicy;
    const struct FdtPciHost*     Firmware;
};

/**
 * @brief Creates a host and acquires its I/O mapping, but does not register it
 * or scan its buses. On failure, leaves hostOut NULL and frees partial resources.
 * The caller owns the new host until registration succeeds.
 */
__EXTERN oserr_t
PciEcamHostCreate(
    _In_  const struct PciEcamHostConfiguration* configuration,
    _Out_ PciHost_t**                            hostOut);

#endif //!__DEVICED_PCI_ECAM_H__
