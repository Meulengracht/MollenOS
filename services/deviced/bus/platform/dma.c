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
        status = OS_EBUSY;
    } else if (entry->DmaReferences == UINT32_MAX) {
        status = OS_EOVERFLOW;
    } else {
        // The trusted internal lease API supplies no principal. Session queries
        // always carry one and must match the prepared activation owner.
        status = principal == UUID_INVALID ? OS_EOK :
            PlatformActivationControl(entry, principal, DevicePrepare);
        if (status == OS_EOK) {
            status = FdtPlatformDma(bus->Tree, entry->Firmware.FirmwareNode, &result.Map);
        }

        if (status == OS_EOK) {
            result.HostId = bus->DeviceId;
            // Require software cache synchronization even for dma-coherent nodes
            // until the backend can verify coherency for the entire DMA path.
            result.CachePolicy = DmDmaCacheNonCoherent;
            entry->DmaReferences++;
            *description = result;
        }
    }
    usched_mtx_unlock(&bus->Lock);
    return status;
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
