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

#include <bus/pci/dma.h>
#include <bus/pci/host-private.h>

oserr_t
PciHostGetDmaDescription(
    _In_  PciHost_t*                host,
    _Out_ struct PciDmaDescription* description)
{
    struct PciDmaDescription result = { 0 };
    oserr_t status;

    // Dispatch through the existing host operations so callers never need to
    // know the controller type. No callback means no established DMA support.
    if (host == NULL || description == NULL) {
        return OS_EINVALPARAMS;
    }
    if (host->Operations == NULL || host->Operations->GetDmaDescription == NULL) {
        return OS_ENOTSUPPORTED;
    }
   
    status = host->Operations->GetDmaDescription(host, &result);
    if (status != OS_EOK) {
        return status;
    }

    // Identity belongs to PCI registration, not the backend. Commit it together
    // with the ranges so a failed query cannot leave a partly updated result.
    result.Host = *PciHostGetIdentification(host);
    
    *description = result;
    return OS_EOK;
}
