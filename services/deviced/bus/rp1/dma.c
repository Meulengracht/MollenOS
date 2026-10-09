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
 */

#include <bus/rp1/dma.h>
#include <bus/rp1/rp1.h>
#include <bus/pci/device.h>

oserr_t
Rp1GetDmaDescription(
    _In_  const struct PciDevice*   parent,
    _In_  uint32_t                  childNode,
    _Out_ struct PciDmaDescription* description)
{
    const struct Rp1Bus*     bus;
    const struct Rp1Child*   child;
    struct PciDmaDescription result;
    oserr_t                  status;

    // The PCI attachment already owns the inventory. Use that association
    // instead of keeping another registry or accepting an arbitrary host.
    if (parent == NULL || description == NULL) {
        return OS_EINVALPARAMS;
    }
    if (parent->Handler != &g_rp1PciHandler || parent->Attachment == NULL) {
        return OS_ENOTSUPPORTED;
    }
    
    bus = parent->Attachment;
    for (child = bus->Children; child != NULL; child = child->Next) {
        if (child->Firmware.NodeOffset == childNode) {
            break;
        }
    }
    if (child == NULL) {
        return OS_ENOENT;
    }

    // The firmware resolver supplies only the RP1 part of this path. The PCI
    // part must come from the host that actually installed the inbound windows.
    status = PciHostGetDmaDescription(parent->Host, &result);
    if (status != OS_EOK) {
        return status;
    }
    
    status = FdtComposeRp1Dma(bus->Host, childNode, &result.Map, &result.Map);
    if (status != OS_EOK) {
        return status;
    }

    // Even a coherent parent would not establish cache visibility across RP1.
    // Keep unknown policy unknown; otherwise require software-managed visibility.
    if (result.CachePolicy != DmDmaCacheUnknown) {
        result.CachePolicy = DmDmaCacheNonCoherent;
    }
    
    *description = result;
    return OS_EOK;
}
