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
 */

#include <stdlib.h>
#include "xhci.h"

oserr_t
XhciDmaAllocate(
    _In_    struct DMAContext*   context,
    _In_    size_t               length,
    _InOut_ struct DMABuffer**   buffer,
    _Out_   uint64_t*            deviceAddress,
    _Out_   void**               memory)
{
    struct DMARequirements requirements = {
        .Length = length,
        .Alignment = XHCI_PAGE_SIZE,
        .Contiguous = true
    };
    const struct DMASegment* segments;
    uint32_t                count;
    oserr_t                  status;

    // Ring segments and contexts fit inside a 64 KiB boundary. Scratchpad
    // backing pages can exceed that size and have no whole-allocation boundary.
    if (length <= 0x10000) {
        requirements.Boundary = 0x10000;
    }
    status = DMAAllocate(context, &requirements, buffer);
    if (status != OS_EOK) {
        return status;
    }
    // The DDK publishes only fully initialized buffers. Segment access cannot
    // fail or issue another syscall for the contiguous allocation requested here.
    segments = DMABufferSegments(*buffer, &count);
    *deviceAddress = segments[0].Address;
    if (memory != NULL) {
        *memory = DMABufferData(*buffer);
    }
    return OS_EOK;
}

oserr_t
XhciScratchpadsAllocate(
    _In_    struct DMAContext*      context,
    _In_    size_t                  count,
    _Out_   reg64_t*                addresses,
    _InOut_ struct XhciScratchpad** pages)
{
    uint64_t address;
    size_t   i;
    oserr_t  status;

    if (*pages != NULL) {
        return OS_EBUSY;
    }
    *pages = calloc(count, sizeof(**pages));
    if (*pages == NULL) {
        return OS_EOOM;
    }
    for (i = 0; i < count; i++) {
        // Each hardware pointer names one page. The DMA allocator need not
        // find a contiguous run spanning all the controller's scratchpads.
        status = XhciDmaAllocate(
            context,
            XHCI_PAGE_SIZE,
            &(*pages)[i].Buffer,
            &address,
            NULL
        );
        if (status != OS_EOK) {
            return status;
        }
        addresses[i] = address;
    }
    return OS_EOK;
}

oserr_t
XhciScratchpadsFree(
    _InOut_ struct XhciScratchpad** pages,
    _In_    size_t                  count)
{
    size_t  i;
    oserr_t  status;

    if (*pages == NULL) {
        return OS_EOK;
    }
    // A failed free leaves its slot owned; already-cleared slots are harmless
    // on retry. Never discard the array while a later page is still allocated.
    for (i = 0; i < count; i++) {
        status = DMAFree(&(*pages)[i].Buffer);
        if (status != OS_EOK) {
            return status;
        }
    }
    free(*pages);
    *pages = NULL;
    return OS_EOK;
}

