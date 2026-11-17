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
 * Driver-side DMA region sessions for SHM_REGIONS mappings.
 */

#ifndef __DDK_DMA_REGION_H__
#define __DDK_DMA_REGION_H__

#include <ddk/dma.h>
#include <os/types/dma_region.h>

_CODE_BEGIN

/**
 * @brief Handle to a driver session bound to one registered region of an
 * SHM_REGIONS mapping. Separate queues can open their own sessions on the same
 * region, and each session tracks its own active transfers.
 */
typedef struct DMARegionSession DMARegionSession_t;

/**
 * @brief Tracks one reusable transfer slot inside a DMA region session.
 *
 * When a session opens, the kernel allocates a fixed table of transfer slots
 * (indexed 0 to capacity - 1). Keep one DMARegionUse_t per slot, usually inside
 * your driver's descriptor or job struct, and zero it before calling
 * DMARegionUseInitialize. Do not copy or reset a slot after that. Generation
 * counts how many times the slot has been started so the kernel can reject
 * stale completions.
 *
 * Calls on a session and its use records must be serialized by the caller. Wait
 * for any earlier CPU or device dependencies before calling begin, and confirm
 * the hardware has finished before calling end. All use records stop being
 * valid once their session closes.
 */
typedef struct DMARegionUse {
    // Session this slot belongs to. NULL before initialization.
    DMARegionSession_t* Session;
    // Zero-based index of this slot within the session's kernel table.
    unsigned int        Slot;
    // Number of times this slot has been started. Advanced on each begin.
    unsigned int        Generation;
    // True from a successful DMATryBeginRegionUse until DMAEndRegionUse ends it.
    bool                Active;
} DMARegionUse_t;

/**
 * @brief Maps one or more whole registered regions of an SHM_REGIONS buffer
 * using their original pages.
 *
 * An SHM_REGIONS buffer is divided ahead of time into fixed byte ranges. This
 * call maps one or more adjacent regions for a device without making a
 * temporary copy (a bounce buffer). IOMMU address translation is still allowed.
 * Setup may allocate memory and call into the kernel, so do not run it on hot
 * submission paths.
 *
 * Because SHM_REGIONS buffers forbid uncoordinated CPU reads and writes, CPU
 * code must go through SHMRegionSessionOpen while the driver accesses those
 * regions through DMARegionSessionOpen. All setup and teardown calls on a
 * mapping, including opening and closing region sessions, must be serialized.
 *
 * @param context Open device context. Must be serialized against DMAClose.
 * @param buffer Existing SHM_REGIONS handle. The caller keeps its own attachment.
 * @param offset First byte to map, counted from the start of the whole SHM buffer.
 * Must fall on the start of a registered region.
 * @param length Number of bytes to map. Must be nonzero and cover one or more
 * whole adjacent regions without gaps or partial tails.
 * @param direction Widest data direction allowed for any session on this mapping.
 * For example, DMABidirectional lets sessions request reads or writes, while
 * DMAToDevice limits them to reads.
 * @param requirements Optional limits on the device segments. NULL means no limits.
 * @param mappingOut Must point to a NULL pointer. Unchanged on failure.
 * @return OS_EOK, or an argument, range, allocation, or address translation error.
 */
DDKDECL(oserr_t,
DMAMapSHMRegions(
    _In_    struct DMAContext*               context,
    _In_    const OSHandle_t*                buffer,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAMapping**              mappingOut));

/**
 * @brief Opens a region session for one registered region and a fixed number of
 * transfer slots.
 *
 * The parent mapping must cover the entire region named by identity. Opening a
 * session checks that the buffer, region index, and generation match, allocates
 * the slot table, and slices the parent mapping's device address list down to
 * just this region's bytes. Because those segments are ready here, later segment
 * lookups and transfer calls never allocate.
 *
 * Independent hardware queues can open separate sessions on the same region at
 * the same time. Calls to open or close sessions must be serialized with other
 * setup and teardown calls on the mapping, such as DMAUnmap.
 *
 * @param mapping Live mapping created by DMAMapSHMRegions.
 * @param identity Exact buffer, region index, and generation to bind to. A stale
 * identity from a recycled buffer is rejected.
 * @param capacity Number of reusable use slots to prepare, from 1 up to
 * SHM_REGION_TOKEN_LIMIT (4096).
 * @param sessionOut Must point to a NULL pointer. Unchanged on error.
 * @return OS_EOK, OS_EBUSY if the mapping is closing or sessionOut is non-NULL,
 * OS_ENOENT if the region identity is not in this mapping, or a setup error.
 */
DDKDECL(oserr_t,
DMARegionSessionOpen(
    _In_    struct DMAMapping*      mapping,
    _In_    const OSBufferIdentity_t* identity,
    _In_    unsigned int             capacity,
    _InOut_ DMARegionSession_t**    sessionOut));

/**
 * @brief Returns the device address list for a single region session.
 *
 * The segments describe just this session's region in byte order, already sliced
 * from the parent mapping at open time. This call reads cached metadata and
 * makes no syscall or allocation. Do not free the returned array.
 *
 * The pointer stays valid until DMARegionSessionClose succeeds or the device
 * binding is revoked during recovery. Even though the addresses never move, you
 * must still get a successful DMATryBeginRegionUse before pointing hardware at
 * them so caches and conflicts are handled first.
 *
 * @param session Open region session.
 * @param countOut Receives the number of entries in the returned array.
 * @return Pointer to the cached DMASegment array for this region.
 */
DDKDECL(const struct DMASegment*,
DMARegionSessionSegments(
    _In_  const DMARegionSession_t* session,
    _Out_ uint32_t*                  countOut));

/**
 * @brief Binds a caller-allocated DMARegionUse_t record to one slot in a session.
 *
 * Each slot index (0 to capacity - 1) can only be bound once over the life of a
 * session. Makes no syscall and allocates nothing. Zero the use struct before
 * calling this function.
 *
 * @param session Open region session. Must not be closing.
 * @param slot Zero-based slot index, strictly less than the capacity given to
 * DMARegionSessionOpen.
 * @param use Caller-owned record to initialize. Must not already belong to a
 * session.
 * @return OS_EOK, OS_EINVALPARAMS for invalid arguments or an out-of-range slot,
 * or OS_EBUSY if the session is closing or the slot was already initialized.
 */
DDKDECL(oserr_t,
DMARegionUseInitialize(
    _In_  DMARegionSession_t* session,
    _In_  unsigned int        slot,
    _Out_ DMARegionUse_t*     use));

/**
 * @brief Requests kernel permission for the device to access a region for one job.
 *
 * Call this before handing the job's descriptors to the hardware. It checks for
 * conflicts with CPU or other device accesses, increments the slot's generation,
 * and syncs CPU caches for the requested direction. Because the call makes one
 * syscall with no memory allocation or service messages, it never sleeps.
 *
 * If the region is busy with a conflicting access, the call fails right away
 * with OS_EBUSY. Retry after other work makes progress. On any failure, use and
 * its generation counter stay unchanged.
 *
 * Errors:
 * - OS_EBUSY if the slot is already active, the session is closing, or another
 *   access conflicts with direction.
 * - OS_EDEVFAULT or OS_ECANCELLED if the device binding faulted or was revoked.
 *   Tear down the session and recover the device before trying again.
 * - OS_EOVERFLOW if the slot's Generation reached UINT_MAX. Open a new session.
 * - OS_EINVALPARAMS if use is uninitialized or direction is invalid.
 *
 * @param use Initialized, inactive use record for a slot in the session.
 * @param direction How the hardware will touch the region for this job. Must fit
 * within the direction passed to DMAMapSHMRegions.
 * @return OS_EOK on success, or one of the errors listed above.
 */
DDKDECL(oserr_t,
DMATryBeginRegionUse(
    _InOut_ DMARegionUse_t* use,
    _In_    enum DMADirection direction));

/**
 * @brief Marks a hardware transfer on a region slot as finished.
 *
 * Call this once the hardware has completed the job, after you have confirmed the
 * hardware stopped, or if submission failed before the hardware could start.
 * This function never stops the hardware or waits for it to finish. It makes one
 * syscall, syncs CPU caches if needed, and drops the slot's active state.
 *
 * When the call returns OS_EOK, the kernel has finished ending the slot and
 * written the final job status to resultOut. An OS_EOK result means a normal
 * completion, while OS_EDEVFAULT reports that the device binding was stopped and
 * cleaned up while this transfer ran. Any other return code means the kernel
 * call itself failed, so use stays active and resultOut stays untouched for a
 * retry.
 *
 * If a job touches several regions or signals a completion fence to callers, wait
 * until DMAEndRegionUse succeeds for every region before signaling that fence.
 * If signaling the fence fails and needs a retry, do not call DMAEndRegionUse
 * again for slots that already ended.
 *
 * @param use Active use record returned by DMATryBeginRegionUse.
 * @param resultOut Receives OS_EOK or OS_EDEVFAULT when the call returns OS_EOK.
 * Unchanged if the call returns any other error.
 * @return OS_EOK if the slot is now idle, OS_EINVALPARAMS for invalid arguments
 * or an inactive slot, or a kernel error to retry on.
 */
DDKDECL(oserr_t,
DMAEndRegionUse(
    _InOut_ DMARegionUse_t* use,
    _Out_   oserr_t*        resultOut));

/**
 * @brief Closes an idle region session, frees its memory, and clears *session.
 *
 * Passing NULL or a pointer to NULL does nothing and returns OS_EOK. If any
 * slot on this session is still active, the call returns OS_EBUSY and leaves the
 * session open and usable.
 *
 * Once no slots are active locally, close marks the session as closing so no new
 * slots can be initialized or started, then releases the kernel handle. If that
 * release fails, the session keeps its state so you can keep calling
 * DMARegionSessionClose with the same pointer until it succeeds. Every session
 * on a mapping, even an idle one, must be closed before DMAUnmap can succeed.
 *
 * @param session Pointer to the session pointer. Cleared to NULL on success.
 * @return OS_EOK, OS_EBUSY while transfers are still active, or a kernel error.
 */
DDKDECL(oserr_t,
DMARegionSessionClose(
    _InOut_ DMARegionSession_t** session));

_CODE_END
#endif //!__DDK_DMA_REGION_H__
