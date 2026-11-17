/**
 * Copyright, Philip Meulengracht
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

#include <arch/acpi.h>
#include <debug.h>
#include "../smmu/discovery.h"
#include "tables.h"
#include "../../../acpi/pci.h"

static oserr_t
__GetPciDmaLimit(
    _In_  uint16_t  segment,
    _Out_ uint64_t* addressLimitOut)
{
    ArmSMMUDiscovery_t* discovery = NULL;
    ACPI_TABLE_HEADER* viot = NULL;
    ACPI_STATUS        acpiStatus;
    uint64_t           limit;
    oserr_t            status;

    if (addressLimitOut == NULL) {
        return OS_EINVALPARAMS;
    }

    // A VIOT table describes a different way of translating device addresses,
    // which this code does not support. If the table exists we give up, even when
    // the other tables look like a simple direct setup.
    acpiStatus = AcpiGetTable(ACPI_SIG_VIOT, 1, &viot);
    if (ACPI_SUCCESS(acpiStatus)) {
        AcpiPutTable(viot);
        WARNING("ACPI PCI DMA: segment %u requires VIOT support", segment);
        return OS_ENOTSUPPORTED;
    }

    if (acpiStatus != AE_NOT_FOUND) {
        ERROR("ACPI PCI DMA: segment %u VIOT lookup failed (%u)", segment, acpiStatus);
        return acpiStatus == AE_NO_MEMORY ? OS_EOOM : OS_EUNKNOWN;
    }

    // The IORT data is only needed while the PCI roots are being discovered. The
    // answer is stored with the root, so later queries and mappings never read
    // the firmware tables again.
    status = ArmSMMUDiscoverAcpi(&discovery);
    if (status != OS_EOK) {
        ERROR("ACPI PCI DMA: segment %u IORT discovery failed (%u)", segment, status);
        return status == OS_ENOENT ? OS_ENOTSUPPORTED : status;
    }

    status = ArmSMMUGetPciDirectLimit(discovery, segment, &limit);
    if (status != OS_EOK) {
        WARNING("ACPI PCI DMA: segment %u has no supported direct policy (%u)", segment, status);
        ArmSMMUDiscoveryDestroy(&discovery);
        return status;
    }
    ArmSMMUDiscoveryDestroy(&discovery);

    *addressLimitOut = limit;
    return OS_EOK;
}

oserr_t
ArchAcpiGetPciDma(
    _In_  ACPI_HANDLE               rootHandle,
    _In_  const struct AcpiPciRoot* root,
    _Out_ AcpiPciDma_t*             dmaOut)
{
    AcpiPciDmaProperties_t properties;
    AcpiPciDma_t           dma = { 0 };
    oserr_t                status;

    if (rootHandle == NULL || root == NULL || dmaOut == NULL) {
        return OS_EINVALPARAMS;
    }

    // On Arm we only allow DMA when the firmware says devices are cache coherent
    // (_CCA is 1). A _DMA list is also refused, because it would need the bridge
    // windows to be taken into account and we treat the whole root the same way.
    status = AcpiPciReadDmaProperties(rootHandle, &properties);
    if (status != OS_EOK) {
        ERROR("ACPI PCI DMA: root %u segment %u namespace read failed (%u)",
              root->Id, root->Segment, status);
        return status;
    }
    
    if (!properties.CoherencyPresent || !properties.Coherent || properties.HasDma) {
        WARNING("ACPI PCI DMA: root %u segment %u requires coherent _CCA and absent _DMA",
                root->Id, root->Segment);
        return OS_ENOTSUPPORTED;
    }

    status = __GetPciDmaLimit(root->Segment, &dma.AddressLimit);
    if (status != OS_EOK) {
        return status;
    }
    
    dma.CachePolicy = SHMDeviceCacheCoherent;
    dma.Flags = ACPI_PCI_DMA_DIRECT;

    *dmaOut = dma;
    return OS_EOK;
}
