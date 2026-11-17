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
 *
 * DMA Region Session Implementation
 */

#include "private.h"
#include <ddk/dma_region.h>
#include <ddk/utils.h>
#include <internal/_syscalls.h>
#include <assert.h>
#include <limits.h>
#include <stdlib.h>

struct DMARegionSession {
    // Parent region mapping this session reads device addresses from.
    struct DMAMapping*   Mapping;
    // Handle, identity, and byte range returned by Syscall_DmaRegionSessionOpen.
    OSDmaRegionSession_t Kernel;
    // Device segments covering just this region, sliced from Mapping at open time.
    struct DMASegment*   Segments;
    // Number of valid entries in Segments.
    uint32_t             SegmentCount;
    // Count of currently active DMARegionUse_t records on this session.
    unsigned int         Active;
    // Set when DMARegionSessionClose starts tearing down the kernel handle.
    bool                 Closing;
    // Bitmap tracking which slot indices have already been bound via DMARegionUseInitialize.
    unsigned char        Initialized[];
};

// The kernel has already checked that this region fits inside the parent
// mapping. Walk the parent mapping's cached device segments, skip up to
// Kernel.Offset, and copy out the slice covering Kernel.Length. No syscall or
// allocation is needed here because Segments was sized for the worst case.
static void
__SliceSegments(
    _In_ DMARegionSession_t* session)
{
    const struct DMASegment* segments;
    uint32_t                 count;
    size_t                   skip = session->Kernel.Offset;
    size_t                   remaining = session->Kernel.Length;

    segments = DMAMappingSegments(session->Mapping, &count);

    for (uint32_t i = 0; i < count && remaining != 0; i++) {
        struct DMASegment* target;
        size_t             length;

        if (skip >= segments[i].Length) {
            skip -= segments[i].Length;
            continue;
        }

        length = segments[i].Length - skip;
        if (length > remaining) {
            length = remaining;
        }

        target = &session->Segments[session->SegmentCount++];
        target->Address = segments[i].Address + skip;
        target->Length = length;

        remaining -= length;
        skip = 0;
    }
    assert(remaining == 0);
}

oserr_t
DMARegionSessionOpen(
    _In_    struct DMAMapping*        mapping,
    _In_    const OSBufferIdentity_t* identity,
    _In_    unsigned int              capacity,
    _InOut_ DMARegionSession_t**      sessionOut)
{
    DMARegionSession_t* session;
    size_t              segmentBytes;
    oserr_t             status;

    if (mapping == NULL || identity == NULL || sessionOut == NULL ||
        capacity == 0 || capacity > SHM_REGION_TOKEN_LIMIT || (mapping->Flags & DMA_MAP_REGIONS) == 0) {
        return OS_EINVALPARAMS;
    }

    if (*sessionOut != NULL || mapping->Closing) {
        return OS_EBUSY;
    }

    if (mapping->Sessions == UINT_MAX) {
        return OS_EOVERFLOW;
    }

    // A single region slice can never contain more segments than the parent
    // mapping itself. Allocate both the session (with its slot bitmap) and
    // the segment array before entering the kernel so slicing cannot fail
    // after the kernel session is already open.
    segmentBytes = mapping->Resource.SegmentCount * sizeof(struct DMASegment);
    if (segmentBytes / sizeof(struct DMASegment) != mapping->Resource.SegmentCount) {
        return OS_EOVERFLOW;
    }

    session = calloc(1, sizeof(DMARegionSession_t) + (capacity + 7) / 8);
    if (session == NULL) {
        return OS_EOOM;
    }

    session->Segments = malloc(segmentBytes);
    if (session->Segments == NULL) {
        free(session);
        return OS_EOOM;
    }

    session->Mapping = mapping;
    status = Syscall_DmaRegionSessionOpen(
        mapping->Resource.Handle,
        identity,
        capacity,
        &session->Kernel
    );
    if (status != OS_EOK) {
        free(session->Segments);
        free(session);
        return status;
    }

    __SliceSegments(session);
    mapping->Sessions++;

    *sessionOut = session;
    return OS_EOK;
}

const struct DMASegment*
DMARegionSessionSegments(
    _In_  const DMARegionSession_t* session,
    _Out_ uint32_t*                  countOut)
{
    // Segments are sliced once when the session opens and never change after that.
    *countOut = session->SegmentCount;
    return session->Segments;
}

oserr_t
DMARegionUseInitialize(
    _In_  DMARegionSession_t* session,
    _In_  unsigned int        slot,
    _Out_ DMARegionUse_t*     use)
{
    unsigned char mask;

    if (session == NULL || use == NULL || slot >= session->Kernel.TokenCapacity) {
        return OS_EINVALPARAMS;
    }

    // Each slot index may only belong to one DMARegionUse_t record so two
    // callers cannot share a kernel slot and corrupt its generation counter.
    mask = (unsigned char)(1U << (slot % 8));
    if (session->Closing || use->Session != NULL || (session->Initialized[slot / 8] & mask) != 0) {
        return OS_EBUSY;
    }

    session->Initialized[slot / 8] |= mask;

    *use = (DMARegionUse_t) {
        .Session = session,
        .Slot = slot
    };
    return OS_EOK;
}

oserr_t
DMATryBeginRegionUse(
    _InOut_ DMARegionUse_t*   use,
    _In_    enum DMADirection direction)
{
    DMARegionSession_t* session;
    oserr_t             status;

    if (use == NULL || use->Session == NULL ||
        (direction != DMAToDevice && direction != DMAFromDevice && direction != DMABidirectional)) {
        return OS_EINVALPARAMS;
    }

    session = use->Session;
    if (use->Active || session->Closing) {
        return OS_EBUSY;
    }

    // Refuse wraparound before calling the kernel so old generation values can
    // never collide with new ones.
    if (use->Generation == UINT_MAX) {
        return OS_EOVERFLOW;
    }

    // Advance Generation and Active only after the kernel accepts the begin, so
    // any error leaves the record unchanged for a retry or teardown.
    status = Syscall_DmaRegionBeginUse(
        session->Kernel.Handle,
        use->Slot,
        use->Generation + 1,
        direction
    );
    if (status == OS_EOK) {
        use->Generation++;
        use->Active = true;
        session->Active++;
    }
    return status;
}

oserr_t
DMAEndRegionUse(
    _InOut_ DMARegionUse_t* use,
    _Out_   oserr_t*        resultOut)
{
    DMARegionSession_t* session;
    oserr_t              status;

    if (use == NULL || !use->Active || use->Session == NULL || resultOut == NULL) {
        return OS_EINVALPARAMS;
    }

    session = use->Session;
    status = Syscall_DmaRegionEndUse(
        session->Kernel.Handle,
        use->Slot,
        use->Generation
    );
    // Only OS_EOK and OS_EDEVFAULT mean the kernel finished ending this slot.
    // OS_EDEVFAULT reports that device recovery stopped and cleaned up the slot.
    // Any other error means the slot may still be active, so keep it marked for retry.
    if (status != OS_EOK && status != OS_EDEVFAULT) {
        return status;
    }

    *resultOut = status;

    // Mark the use as inactive and decrement the session's active count.
    use->Active = false;
    assert(session->Active != 0);
    session->Active--;
    return OS_EOK;
}

oserr_t
DMARegionSessionClose(
    _InOut_ DMARegionSession_t** session)
{
    DMARegionSession_t* current;
    oserr_t             status;

    if (session == NULL || *session == NULL) {
        return OS_EOK;
    }

    current = *session;

    if (current->Active != 0) {
        return OS_EBUSY;
    }

    // Mark closing before releasing the kernel handle so failed closes block new
    // slot initialization or begins while the caller retries close.
    current->Closing = true;
    status = Syscall_ResourceRelease(current->Kernel.Handle);
    if (status != OS_EOK) {
        WARNING("DMARegionSessionClose: session %u, buffer %u region %u close failed: %u",
            current->Kernel.Handle, current->Kernel.Identity.Buffer, current->Kernel.Identity.Region, status);
        return status;
    }

    assert(current->Mapping->Sessions != 0);
    current->Mapping->Sessions--;
    free(current->Segments);
    free(current);

    *session = NULL;
    return OS_EOK;
}
