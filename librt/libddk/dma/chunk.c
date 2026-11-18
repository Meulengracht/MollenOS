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
 * Chunked SHM mapping for drivers that move a large client buffer one piece at
 * a time. Built only from the public DMAMapSHMDirect and DMAMapSHM calls, so
 * it keeps no state and needs no private DMA structures.
 */

#include <ddk/dma.h>
#include <ddk/utils.h>

static size_t
__RoundDownToBlocks(
    _In_ size_t length,
    _In_ size_t blockLength)
{
    return length - (length % blockLength);
}

static size_t
__ClipLength(
    _In_ size_t length,
    _In_ size_t limit)
{
    return length < limit ? length : limit;
}

/**
 * Maps the range with DMAMapSHM, which may copy it through a bounce buffer.
 * Starts at length and halves it while the device pool cannot supply the copy.
 * length must be a nonzero multiple of blockLength. On failure both outputs
 * are left unchanged.
 */
static oserr_t
__MapWithBounce(
    _In_    struct DMAContext*               context,
    _In_    const OSHandle_t*                buffer,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _In_    size_t                           blockLength,
    _InOut_ struct DMAMapping**              mappingOut,
    _Out_   size_t*                          mappedLengthOut)
{
    size_t  attemptLength = length;
    oserr_t status;

    for (;;) {
        status = DMAMapSHM(
            context,
            buffer,
            offset,
            attemptLength,
            direction,
            requirements,
            mappingOut
        );
        if (status == OS_EOK) {
            *mappedLengthOut = attemptLength;
            return OS_EOK;
        }

        // Only two errors depend on the length. OS_EBUFFER means the copy is
        // larger than one IOMMU bounce mapping may be. OS_EOOM means the device
        // pool has no free piece this large. A smaller length can succeed for
        // both. Every other error would fail again at any length. DMAMapSHM
        // already logs a failed map syscall, so no second message is written.
        if (status != OS_EOOM && status != OS_EBUFFER) {
            return status;
        }

        // One block is the smallest length the caller accepts. If even that
        // fails, the pool is short right now. Return the error unchanged so the
        // caller can tell this apart from a real fault and retry later.
        if (attemptLength == blockLength) {
            WARNING("DMAMapSHMChunk: buffer %u, offset %zu, one block of %zu bytes failed (%u)",
                buffer->ID, offset, blockLength, status);
            return status;
        }

        // Halving reaches one block in a few steps, while still trying the
        // largest lengths first. Round down to keep whole blocks, but never go
        // below one block, which happens when attemptLength is under two blocks.
        attemptLength = __RoundDownToBlocks(attemptLength / 2, blockLength);
        if (attemptLength < blockLength) {
            attemptLength = blockLength;
        }
        TRACE("DMAMapSHMChunk: buffer %u, offset %zu, bounce failed (%u), retrying with %zu bytes",
            buffer->ID, offset, status, attemptLength);
    }
}

oserr_t
DMAMapSHMChunk(
    _In_    struct DMAContext*               context,
    _In_    const OSHandle_t*                buffer,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _In_    const struct DMAChunkLimits*     limits,
    _InOut_ struct DMAMapping**              mappingOut,
    _Out_   size_t*                          mappedLengthOut)
{
    size_t  directLength;
    size_t  bounceLength;
    oserr_t status;

    if (context == NULL || buffer == NULL || limits == NULL || mappingOut == NULL || mappedLengthOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (*mappingOut != NULL) {
        return OS_EINVALPARAMS;
    }

    if (limits->BlockLength == 0 || length == 0 || (length % limits->BlockLength) != 0) {
        return OS_EINVALPARAMS;
    }

    if (limits->DirectLength < limits->BlockLength || limits->BounceLength < limits->BlockLength) {
        return OS_EINVALPARAMS;
    }

    // Both limits are at least one block here, so rounding them down keeps them
    // at one block or more. length is already whole blocks, so the clipped
    // lengths are whole blocks too.
    directLength = __ClipLength(
        length,
        __RoundDownToBlocks(limits->DirectLength, limits->BlockLength)
    );
    bounceLength = __ClipLength(
        length,
        __RoundDownToBlocks(limits->BounceLength, limits->BlockLength)
    );

    // Try the original pages first. This path takes no device pool memory, so
    // it can map the largest chunk without affecting other drivers.
    status = DMAMapSHMDirect(
        context,
        buffer,
        offset,
        directLength,
        direction,
        requirements,
        mappingOut
    );
    if (status == OS_EOK) {
        *mappedLengthOut = directLength;
        return OS_EOK;
    }

    // OS_ENOENT means the device cannot reach the original pages. OS_ENOTSUPPORTED
    // means they do not meet the cache or alignment rules. A bounce buffer fixes
    // both. Any other error means the request itself failed, and a bounce
    // attempt would fail the same way. DMAMapSHMDirect has already logged a
    // failed map syscall.
    if (status != OS_ENOENT && status != OS_ENOTSUPPORTED) {
        return status;
    }

    TRACE("DMAMapSHMChunk: buffer %u, offset %zu, direct map failed (%u), using %zu bytes with bounce",
        buffer->ID, offset, status, bounceLength);
    return __MapWithBounce(
        context,
        buffer,
        offset,
        bounceLength,
        direction,
        requirements,
        limits->BlockLength,
        mappingOut,
        mappedLengthOut
    );
}
