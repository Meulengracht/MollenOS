/**
 * Copyright 2026, Philip Meulengracht
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

#ifndef __DEVICED_PCI_DMA_H__
#define __DEVICED_PCI_DMA_H__

#include <bus/pci/host.h>
#include <firmware/dma.h>

enum PciDmaCachePolicy {
    // No policy has been established; this is not permission to use cached RAM.
    PciDmaCacheUnknown,
    // Software must arrange cache visibility. This is the initial Broadcom policy.
    PciDmaCacheNonCoherent,
    // Reserved for a backend that establishes this guarantee for the whole path.
    PciDmaCacheCoherent
};

/**
 * @brief Copied address ranges and cache policy for a host or its RP1 child.
 *
 * Host identifies the PCI controller. Each range's DeviceBase is a PCI address
 * for a host query and an RP1 address for a child query. PhysicalBase means a CPU
 * physical address. These are addressable regions, not allocated buffers or a
 * measurement of installed RAM. The structure contains no borrowed pointers.
 */
struct PciDmaDescription {
    struct PciHostIdentification Host;
    enum PciDmaCachePolicy       CachePolicy;
    struct FdtDmaMap             Map;
};

/**
 * @brief Describe the DMA translation that this host has successfully configured.
 *
 * Keeping this query on the host prevents callers from treating firmware's
 * requested settings as configured hardware, or from interpreting private
 * controller data. It does not enable device bus mastering or clear DMA pending.
 *
 * @param host Live host. The owner must exclude setup, reset and destruction
 *             while querying and using the result, as for other host operations.
 * @param description Complete copy on success; unchanged on failure. HostId is
 *                    UUID_INVALID until PciHostRegister succeeds. A saved copy
 *                    does not retain the host or remain valid after reset.
 * @return OS_EOK, OS_ENOTSUPPORTED if the backend supplies no DMA policy,
 *         OS_EBUSY while translation is unavailable, OS_ENOENT for no RAM,
 *         or an error describing invalid arguments or ranges.
 */
__EXTERN oserr_t
PciHostGetDmaDescription(
    _In_  PciHost_t*                host,
    _Out_ struct PciDmaDescription* description);

#endif //!__DEVICED_PCI_DMA_H__
