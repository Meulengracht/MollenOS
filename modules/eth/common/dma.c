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

#include "mac.h"

oserr_t
EthernetDmaAllocate(
    _In_    struct DMAContext*  context,
    _In_    size_t              length,
    _InOut_ struct EthernetDma* storage)
{
    const struct DMASegment* segments;
    uint32_t                 count;
    oserr_t                  status;
    struct DMARequirements requirements = {
        .Length = length,
        .Alignment = 64,
        .Contiguous = true
    };

    // The MAC uses base-plus-offset device addresses for its frame storage.
    // Request one device segment aligned to 64 bytes. DMAAllocate also rounds
    // the length to whole pages and keeps CPU and device access available.
    status = DMAAllocate(context, &requirements, &storage->Buffer);
    if (status != OS_EOK) {
        return status;
    }

    // Contiguous allocation guarantees one segment. These accessors cannot
    // fail for a live buffer. Address is for descriptors, Data is for CPU copies.
    segments = DMABufferSegments(storage->Buffer, &count);
    storage->Data = DMABufferData(storage->Buffer);
    storage->Address = segments[0].Address;
    return OS_EOK;
}
