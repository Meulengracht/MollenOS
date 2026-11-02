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

#include <device-dma.h>
#include <ddk/utils.h>

#include "private.h"

oserr_t
PlatformPrepareDma(
    _In_  void*                    context,
    _In_  uuid_t                   principal,
    _Out_ struct DmDmaDescription* description)
{
    struct PlatformEntry*   entry = context;
    struct PlatformBus*     bus = entry->Bus;
    struct DmDmaDescription result = { 0 };
    oserr_t                 status;

    usched_mtx_lock(&bus->Lock);
    if (bus->Removing || !PlatformIsActivationReady(entry) || bus->DeviceId == UUID_INVALID) {
        TRACE("PlatformPrepareDma: node %u bus %u is removing or not ready for DMA",
              entry->Firmware.FirmwareNode, bus->DeviceId);
        usched_mtx_unlock(&bus->Lock);
        return OS_EBUSY;
    }
    
    if (entry->DmaReferences == UINT32_MAX) {
        ERROR("PlatformPrepareDma: node %u DMA reference count overflow (%u)",
              entry->Firmware.FirmwareNode, OS_EOVERFLOW);
        usched_mtx_unlock(&bus->Lock);
        return OS_EOVERFLOW;
    }

    // The trusted internal lease API supplies no principal. Session queries
    // always carry one and must match the prepared activation owner.
    if (principal != UUID_INVALID) {
        status = PlatformActivationControl(entry, principal, DevicePrepare);
        if (status != OS_EOK) {
            ERROR("PlatformPrepareDma: node %u preparation for principal %u failed (%u)",
                  entry->Firmware.FirmwareNode, principal, status);
            usched_mtx_unlock(&bus->Lock);
            return status;
        }
    }

    status = FdtPlatformDma(bus->Tree, entry->Firmware.FirmwareNode, &result.Map);
    if (status != OS_EOK) {
        ERROR("PlatformPrepareDma: node %u DMA map query failed (%u)",
              entry->Firmware.FirmwareNode, status);
        usched_mtx_unlock(&bus->Lock);
        return status;
    }

    result.HostId = bus->DeviceId;
    // Require software cache synchronization even for dma-coherent nodes
    // until the backend can verify coherency for the entire DMA path.
    result.CachePolicy = DmDmaCacheNonCoherent;
    entry->DmaReferences++;
    *description = result;
    usched_mtx_unlock(&bus->Lock);
    return OS_EOK;
}

void
PlatformReleaseDma(
    _In_ void* context)
{
    struct PlatformEntry* entry = context;

    usched_mtx_lock(&entry->Bus->Lock);
    entry->DmaReferences--;
    usched_mtx_unlock(&entry->Bus->Lock);
}
