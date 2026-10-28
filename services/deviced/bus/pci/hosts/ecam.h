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

#include <bus/pci/host.h>

struct FdtPciHost;

/**
 * @brief Identifies an ECAM host, its configuration space, and I/O resource policy.
 * Base is the physical address of Identification.BusStart. Each bus uses 1 MiB
 * of configuration space. Firmware is optional. If supplied, the data it
 * references must stay mapped until host registration takes its own reference.
 */
struct PciEcamHostConfiguration {
    struct PciHostIdentification Identification;
    uint64_t                     Base;
    enum PciIoResourcePolicy     IoResourcePolicy;
    const struct FdtPciHost*     Firmware;
};

/**
 * @brief Creates a host and maps its ECAM configuration space.
 * Registration and bus scanning are separate steps. The caller owns the new
 * host until registration succeeds.
 *
 * @param configuration Host settings. Identification.HostId must be UUID_INVALID
 *                      and BusStart must not exceed BusEnd.
 * @param hostOut Receives the host, or NULL on failure unless cleanup also fails.
 *                A non-NULL host returned on failure must only be passed to
 *                PciHostDestroy until cleanup succeeds.
 * @return OS_EOK on success, or an error from validation, allocation, or I/O setup.
 */
__EXTERN oserr_t
PciEcamHostCreate(
    _In_  const struct PciEcamHostConfiguration* configuration,
    _Out_ PciHost_t**                            hostOut);

#endif //!__DEVICED_PCI_ECAM_H__
