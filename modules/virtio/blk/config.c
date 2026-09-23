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
 * Device configuration snapshots and logical-sector geometry.
 */

#define __TRACE
#include "private.h"

#include <ddk/utils.h>
#include <stddef.h>

#define VIRTIO_BLK_CONFIG_CAPACITY 0
#define VIRTIO_BLK_CONFIG_SIZE_MAX 8
#define VIRTIO_BLK_CONFIG_SEG_MAX  12
#define VIRTIO_BLK_CONFIG_BLK_SIZE 20

#define VIRTIO_BLK_CONFIG_RETRIES 8

static oserr_t
__ReadCommonConfiguration(
    _In_  VirtioBlkDevice_t* device,
    _In_  uint32_t           offset,
    _In_  size_t             width,
    _Out_ uint64_t*          valueOut)
{
    return VirtioPciRegionRead(
            &device->Transport.Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1],
            offset,
            width,
            valueOut
    );
}

static oserr_t
__ReadDeviceConfiguration(
    _In_  VirtioBlkDevice_t* device,
    _In_  uint32_t           offset,
    _In_  size_t             width,
    _Out_ uint64_t*          valueOut)
{
    VirtioPciRegion_t* region =
            &device->Transport.Regions[VIRTIO_PCI_CAP_DEVICE_CFG - 1];

    if (region->IoSpace == NULL) {
        return OS_ENOTSUPPORTED;
    }
    return VirtioPciRegionRead(region, offset, width, valueOut);
}

oserr_t
VirtioBlkReadConfiguration(
    _InOut_ VirtioBlkDevice_t* device)
{
    uint64_t capacity;
    uint64_t value;
    uint8_t  generationBefore;
    uint8_t  generationAfter;
    oserr_t  oserr;
    TRACE("VirtioBlkReadConfiguration()");

    // Device-specific configuration can change asynchronously. A snapshot is
    // usable only when ConfigGeneration is unchanged across all field reads.
    for (int retry = 0; retry < VIRTIO_BLK_CONFIG_RETRIES; retry++) {
        oserr = __ReadCommonConfiguration(
            device,
            offsetof(VirtioPciCommonConfiguration_t, ConfigGeneration),
            sizeof(uint8_t),
            &value
        );
        if (oserr != OS_EOK) {
            ERROR("Failed to read the initial configuration generation: %u", oserr);
            return oserr;
        }
        generationBefore = (uint8_t)value;

        oserr = __ReadDeviceConfiguration(
            device,
            VIRTIO_BLK_CONFIG_CAPACITY,
            sizeof(capacity),
            &capacity
        );
        if (oserr != OS_EOK) {
            ERROR("Failed to read the block-device capacity: %u", oserr);
            return oserr;
        }

        // A zero size_max means that the device imposes no segment-size limit.
        device->SizeMax = UINT32_MAX;
        if (device->Features & VIRTIO_BLK_F_SIZE_MAX) {
            oserr = __ReadDeviceConfiguration(
                device,
                VIRTIO_BLK_CONFIG_SIZE_MAX,
                sizeof(uint32_t),
                &value
            );
            if (oserr != OS_EOK) {
                ERROR("Failed to read size_max: %u", oserr);
                return oserr;
            }
            if ((uint32_t)value != 0) {
                device->SizeMax = (uint32_t)value;
            }
        }

        device->SegmentMax = UINT32_MAX;
        if (device->Features & VIRTIO_BLK_F_SEG_MAX) {
            oserr = __ReadDeviceConfiguration(
                device,
                VIRTIO_BLK_CONFIG_SEG_MAX,
                sizeof(uint32_t),
                &value
            );
            if (oserr != OS_EOK) {
                ERROR("Failed to read seg_max: %u", oserr);
                return oserr;
            }
            device->SegmentMax = (uint32_t)value;
        }

        // Virtio addresses requests in fixed 512-byte sectors. blk_size only
        // changes the logical sector size exposed through Vali's storage API.
        device->Descriptor.SectorSize = VIRTIO_BLK_SECTOR_SIZE;
        if (device->Features & VIRTIO_BLK_F_BLK_SIZE) {
            oserr = __ReadDeviceConfiguration(
                device,
                VIRTIO_BLK_CONFIG_BLK_SIZE,
                sizeof(uint32_t),
                &value
            );
            if (oserr != OS_EOK) {
                ERROR("Failed to read blk_size: %u", oserr);
                return oserr;
            }
            device->Descriptor.SectorSize = (size_t)(uint32_t)value;
        }

        oserr = __ReadCommonConfiguration(
            device,
            offsetof(VirtioPciCommonConfiguration_t, ConfigGeneration),
            sizeof(uint8_t),
            &value
        );
        if (oserr != OS_EOK) {
            ERROR("Failed to read the final configuration generation: %u", oserr);
            return oserr;
        }
        generationAfter = (uint8_t)value;
        if (generationBefore == generationAfter) {
            // Vali currently exposes integral logical sectors. Reject block
            // sizes that cannot be represented as whole Virtio sectors.
            if (device->Descriptor.SectorSize < VIRTIO_BLK_SECTOR_SIZE ||
                (device->Descriptor.SectorSize % VIRTIO_BLK_SECTOR_SIZE) != 0) {
                return OS_ENOTSUPPORTED;
            }
            device->Descriptor.SectorCount =
                    capacity / (device->Descriptor.SectorSize / VIRTIO_BLK_SECTOR_SIZE);
            return device->Descriptor.SectorCount == 0 ? OS_ENOTSUPPORTED : OS_EOK;
        }
    }
    WARNING("Could not obtain a stable block-device configuration");
    return OS_EBUSY;
}
