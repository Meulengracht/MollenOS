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

//#define __TRACE

#include <ddk/dma.h>
#include <ddk/utils.h>

oserr_t
DMABeginUseFenced(
    _In_    struct DMAMapping*   mapping,
    _InOut_ struct DMAFencedUse* use,
    _In_    OSFence_t*           fence,
    _In_    enum SHMWaitMode     mode,
    _In_    const OSTimestamp_t* deadline)
{
    oserr_t    status;
    OSFence_t* reference;

    if (mapping == NULL || use == NULL || fence == NULL) {
        WARNING("DMABeginUseFenced: missing mapping, use or fence");
        return OS_EINVALPARAMS;
    }

    if (use->Use.Mapping != NULL || use->Fence != NULL) {
        TRACE("DMABeginUseFenced: use %p is still active", use);
        return OS_EBUSY;
    }

    // Keep a token reference because consumers may release their references
    // as soon as they see completion, before notification finishes.
    status = OSFenceAcquire(fence);
    if (status != OS_EOK) {
        return status;
    }
    reference = fence;
    
    // Reserve the fence before DMA begin, which may wait for conflicting access.
    // Another producer must not submit the fence while begin is waiting.
    status = OSFencePrepare(fence);
    if (status != OS_EOK) {
        TRACE("DMABeginUseFenced: fence %p prepare failed (%u)", fence, status);
        OSFenceRelease(&reference);
        return status;
    }

    status = DMABeginUse(mapping, &use->Use, mode, deadline);
    if (status != OS_EOK) {
        // DMA begin failed. Return the fence to idle and release the reference
        // added here so the caller can retry.
        OSFenceCancel(fence);
        OSFenceRelease(&reference);
        if (status == OS_EBUSY || status == OS_ETIMEOUT || status == OS_EINTERRUPTED) {
            TRACE("DMABeginUseFenced: mapping %p begin deferred (%u)", mapping, status);
        } else {
            ERROR("DMABeginUseFenced: mapping %p begin failed (%u); fence is idle", mapping, status);
        }
        return status;
    }
    // DMA begin succeeded, so the token can now end the use. Make the fence
    // pending before the driver starts hardware or lets consumers poll it.
    use->Fence = fence;
    OSFenceSubmit(fence);
    
    TRACE("DMABeginUseFenced: use %p, mapping %p, fence %p submitted", use, mapping, fence);
    return OS_EOK;
}

oserr_t
DMAEndUseFenced(
    _InOut_ struct DMAFencedUse* use,
    _In_    oserr_t              result)
{
    oserr_t status;

    if (use == NULL || use->Fence == NULL) {
        WARNING("DMAEndUseFenced: missing or empty use");
        return OS_EINVALPARAMS;
    }

    // DMAEndUse clears Mapping on success, so a notification retry skips it.
    // Other uses may still be active. Cache synchronization and CPU access must
    // wait for the last use to end.
    if (use->Use.Mapping != NULL) {
        status = DMAEndUse(&use->Use);
        if (status != OS_EOK) {
            ERROR("DMAEndUseFenced: use %p, mapping %p end failed (%u); fence remains pending",
                use, use->Use.Mapping, status);
            return status;
        }
    }

    // Signal sets the result before waking waiters. On error, keep Fence for
    // retry with the same result. Polling consumers may already have finished.
    status = OSFenceSignal(use->Fence, result);
    if (status != OS_EOK) {
        ERROR("DMAEndUseFenced: use %p, fence %p signal failed (%u, result %u); keeping fence for retry",
            use, use->Fence, status, result);
        return status;
    }

    // Notification has finished. Consumers may have released every other
    // reference already, so this release may free the fence.
    OSFenceRelease(&use->Fence);
    TRACE("DMAEndUseFenced: use %p completed with result %u", use, result);
    return OS_EOK;
}
