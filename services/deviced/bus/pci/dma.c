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

//#define __TRACE

#include <ddk/utils.h>
#include <string.h>

#include <bus/pci/dma.h>
#include <bus/pci/host-private.h>

oserr_t
PciHostGetIdentityDma(
    _In_  PciHost_t*                host,
    _Out_ struct PciDmaDescription* description)
{
    (void)host;

#if defined(__amd64__) || defined(__i386__)
    memset(description, 0, sizeof(*description));

    description->CachePolicy = DmDmaCacheCoherent;
    description->Map.Count = 1;
    description->Map.Ranges[0].Length = UINT64_MAX;
    return OS_EOK;
#else
    (void)description;
    return OS_ENOTSUPPORTED;
#endif
}

oserr_t
PciHostGetDmaDescription(
    _In_  PciHost_t*                host,
    _Out_ struct PciDmaDescription* description)
{
    struct PciDmaDescription result = { 0 };
    oserr_t                  status;

    TRACE("PciHostGetDmaDescription(host=%p)", host);

    if (host == NULL || description == NULL) {
        WARNING("PciHostGetDmaDescription missing host or output");
        return OS_EINVALPARAMS;
    }

    // The controller must supply a DMA query callback before DMA can be used.
    if (host->Operations == NULL || host->Operations->GetDmaDescription == NULL) {
        TRACE("PciHostGetDmaDescription host has no established DMA support");
        return OS_ENOTSUPPORTED;
    }

    status = host->Operations->GetDmaDescription(host, &result);
    if (status != OS_EOK) {
        WARNING("PciHostGetDmaDescription host %u query failed: %u",
            host->Identification.HostId, status);
        return status;
    }

    // Use the identity assigned during PCI registration. Update the caller's
    // description only after the controller query succeeds.
    result.Host = *PciHostGetIdentification(host);

    *description = result;
    return OS_EOK;
}
