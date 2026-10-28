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

#ifndef __DEVICED_RP1_DMA_H__
#define __DEVICED_RP1_DMA_H__

#include <bus/pci/dma.h>

struct PciDevice;

/**
 * @brief Describe a child DMA path through its existing RP1 PCI attachment.
 *
 * The child must belong to the parent's RP1 inventory so that its firmware
 * mappings are combined with the correct PCI host's configured address ranges.
 * The call reads the existing objects without extending their lifetime or
 * creating DMA access permissions.
 *
 * @param parent Live PCI device with an RP1 attachment. Its owner must exclude
 *               reset, removal and host destruction while using the description.
 * @param childNode Firmware node offset from this attachment's child inventory.
 * @param description Copied ranges using RP1 device addresses and the PCI host
 *                    identity. Unchanged on failure. The cache policy requires
 *                    software cache maintenance, or remains unknown if the host
 *                    cannot report its cache requirements.
 * @return OS_EOK, OS_EINVALPARAMS for a NULL parent or output,
 *         OS_ENOENT for a child outside this inventory,
 *         OS_ENOTSUPPORTED for a missing attachment or unsupported DMA path,
 *         or a host-query/firmware error. Success does not clear DMA pending,
 *         allocate buffers, establish installed RAM size or enable the device.
 */
__EXTERN oserr_t
Rp1GetDmaDescription(
    _In_  const struct PciDevice*   parent,
    _In_  uint32_t                  childNode,
    _Out_ struct PciDmaDescription* description);

#endif
