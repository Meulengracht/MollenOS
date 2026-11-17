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
#include <ddk/pci/acpi.h>
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
    WARNING("PciHostGetIdentityDma: host %p has no identity DMA support on this architecture", host);
    return OS_ENOTSUPPORTED;
#endif
}

oserr_t
PciHostGetAcpiDma(
    _In_  PciHost_t*                host,
    _Out_ struct PciDmaDescription* description)
{
    struct PciDmaDescription result = { 0 };
    const AcpiPciDma_t*      policy;

    if (host == NULL || description == NULL) {
        return OS_EINVALPARAMS;
    }

    if (host->AcpiRootId == 0) {
        WARNING("PciHostGetAcpiDma: host %u has no ACPI root ID", host->Identification.HostId);
        return OS_ENOTSUPPORTED;
    }

    if (!OSERR_VALID(host->AcpiDmaStatus)) {
        ERROR("PciHostGetAcpiDma: root %u has invalid DMA status %u",
              host->AcpiRootId, host->AcpiDmaStatus);
        return OS_EINVALPARAMS;
    }

    if (host->AcpiDmaStatus != OS_EOK) {
        WARNING("PciHostGetAcpiDma: host %u root %u DMA policy unavailable (%u)",
                host->Identification.HostId, host->AcpiRootId, host->AcpiDmaStatus);
        return (oserr_t)host->AcpiDmaStatus;
    }

    policy = &host->AcpiDma;
    if (policy->Flags & ~ACPI_PCI_DMA_DIRECT) {
        ERROR("PciHostGetAcpiDma: root %u has unknown DMA flags 0x%x",
              host->AcpiRootId, policy->Flags);
        return OS_EINVALPARAMS;
    }

    if (!(policy->Flags & ACPI_PCI_DMA_DIRECT)) {
        WARNING("PciHostGetAcpiDma: root %u has no direct DMA policy", host->AcpiRootId);
        return OS_ENOTSUPPORTED;
    }

    if (policy->CachePolicy != SHMDeviceCacheCoherent) {
        WARNING("PciHostGetAcpiDma: root %u has unsupported cache policy %u",
                host->AcpiRootId, policy->CachePolicy);
        return OS_ENOTSUPPORTED;
    }

    // The kernel found one set of rules that is valid for every device on this
    // host. The cache setting has its own enum in this service, separate from
    // the one in the DDK, so it is converted here.
    result.CachePolicy = DmDmaCacheCoherent;
    result.Map.Count = 1;

    // The length is the number of bytes, which would be 2^64 for the full address
    // range. That does not fit in a uint64_t, so in that case we leave out the
    // last byte instead of letting the value wrap around to zero.
    result.Map.Ranges[0].Length = policy->AddressLimit == UINT64_MAX
        ? UINT64_MAX : policy->AddressLimit + 1;
    DEBUG("PciHostGetAcpiDma: host %u root %u coherent direct DMA limit=0x%llx",
          host->Identification.HostId, host->AcpiRootId,
          (unsigned long long)policy->AddressLimit);
    
    *description = result;
    return OS_EOK;
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
        WARNING("PciHostGetDmaDescription: missing host or output (host=%p, description=%p)",
                host, description);
        return OS_EINVALPARAMS;
    }

    // The controller must supply a DMA query callback before DMA can be used.
    if (host->Operations == NULL || host->Operations->GetDmaDescription == NULL) {
        TRACE("PciHostGetDmaDescription: host %u has no established DMA support",
              host->Identification.HostId);
        return OS_ENOTSUPPORTED;
    }

    status = host->Operations->GetDmaDescription(host, &result);
    if (status != OS_EOK) {
        ERROR("PciHostGetDmaDescription: host %u DMA query failed (%u)",
              host->Identification.HostId, status);
        return status;
    }

    // Use the identity assigned during PCI registration. Update the caller's
    // description only after the controller query succeeds.
    result.Host = *PciHostGetIdentification(host);

    *description = result;
    return OS_EOK;
}
