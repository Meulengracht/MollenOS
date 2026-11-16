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

#include <dma-types.h>
#include <ddk/utils.h>

#include "private.h"

// Caller must hold Bus->Lock on a registered bus that is not being removed.
// Reads static DMA ranges from the device tree without preparing hardware or
// bumping DmaReferences.
static oserr_t
__PlatformDescribeDmaLocked(
    _In_  struct PlatformEntry* entry,
    _Out_ DmDmaDescription_t*   description)
{
    DmDmaDescription_t result = { 0 };
    oserr_t            status;

    status = FdtPlatformDma(entry->Bus->Tree, entry->Firmware.FirmwareNode, &result.Map);
    if (status != OS_EOK) {
        ERROR("__PlatformDescribeDmaLocked: node %u FdtPlatformDma failed: %u",
              entry->Firmware.FirmwareNode, status);
        return status;
    }

    result.HostId = entry->Bus->DeviceId;
    result.AddressLimit = UINT64_MAX;
    // Treat the path as non-coherent until we can check cache coherency along
    // every bridge and bus hop, even if the device tree marks this node dma-coherent.
    result.CachePolicy = DmDmaCacheNonCoherent;
    
    *description = result;
    return OS_EOK;
}

oserr_t
PlatformDescribeDma(
    _In_  void*               context,
    _Out_ DmDmaDescription_t* description)
{
    struct PlatformEntry* entry = context;
    oserr_t               status;

    usched_mtx_lock(&entry->Bus->Lock);
    if (entry->Bus->Removing || entry->Bus->DeviceId == UUID_INVALID) {
        usched_mtx_unlock(&entry->Bus->Lock);
        return OS_EBUSY;
    }

    // Device publication queries DMA before a driver loads or calls
    // DevicePrepare. Skip clock and power checks and leave DmaReferences alone.
    status = __PlatformDescribeDmaLocked(entry, description);
    if (status != OS_EOK) {
        WARNING("PlatformDescribeDma: node %u description failed: %u",
                entry->Firmware.FirmwareNode, status);
    }
    usched_mtx_unlock(&entry->Bus->Lock);
    return status;
}

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

    // Driver queries pass a process ID (principal), so verify that this process
    // already prepared the device before letting it start DMA. Internal callers
    // can pass UUID_INVALID once they have done their own checks.
    if (principal != UUID_INVALID) {
        status = PlatformActivationControl(entry, principal, DevicePrepare);
        if (status != OS_EOK) {
            ERROR("PlatformPrepareDma: node %u preparation for principal %u failed (%u)",
                  entry->Firmware.FirmwareNode, principal, status);
            usched_mtx_unlock(&bus->Lock);
            return status;
        }
    }

    status = __PlatformDescribeDmaLocked(entry, &result);
    if (status != OS_EOK) {
        ERROR("PlatformPrepareDma: node %u description failed: %u",
              entry->Firmware.FirmwareNode, status);
        usched_mtx_unlock(&bus->Lock);
        return status;
    }

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
