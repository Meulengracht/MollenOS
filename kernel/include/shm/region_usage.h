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
 * Shared access tracking for one registered region. CPU sessions and DMA
 * device sessions both go through this tracker so a region allows either
 * multiple readers or one exclusive writer at a time, and halts new work while
 * a device fault or hardware stop is being cleaned up.
 */

#ifndef __SHM_REGION_USAGE_H__
#define __SHM_REGION_USAGE_H__

#include <os/osdefs.h>
#include <spinlock.h>

/**
 * @brief Tracks active readers, writers, and device faults on one region.
 *
 * Shared across all CPU and device sessions attached to the same region. Lock
 * disables interrupts and protects every field in this struct.
 *
 * - Readers:    number of active shared readers (CPU or device).
 * - Stopped:    number of device accesses whose hardware was confirmed stopped
 *               after a revoke or fault, still waiting for cache cleanup and
 *               final token retirement.
 * - Faults:     number of device mappings on this region that are currently
 *               faulted. New Begin calls fail with OS_EDEVFAULT while nonzero.
 * - Writer:     true when one exclusive writer (CPU or device) is active.
 * - Recovering: true while stop cleanup is syncing CPU caches for this region.
 */
typedef struct SHMRegionUsage {
    Spinlock_t   Lock;
    unsigned int Readers;
    unsigned int Stopped;
    unsigned int Faults;
    bool         Writer;
    bool         Recovering;
} SHMRegionUsage_t;

/**
 * @brief Lifecycle state of a single access token slot.
 *
 * Idle:    slot is free and ready for a new access with Generation + 1.
 * Live:    an active CPU or device access is counted in Readers or Writer.
 * Stopped: a device access whose hardware was confirmed stopped. Moved out of
 *          Readers/Writer into Stopped until cache recovery and End retire it.
 */
enum SHMRegionAccessState {
    SHMRegionAccessIdle,
    SHMRegionAccessLive,
    SHMRegionAccessStopped
};

/**
 * @brief Tracks one active or past access in a single token slot.
 *
 * Zero-initialize before first use, and do not reset Generation between uses.
 * Keeping the last generation prevents a late End or Stop call from an older
 * cycle from affecting a newer access in the same slot. Callers must ensure
 * calls on the same access record do not overlap. The session holding this
 * record keeps Region allocated.
 */
typedef struct SHMRegionAccess {
    SHMRegionUsage_t*         Region;
    unsigned int              Generation;
    enum SHMRegionAccessState State;
    bool                      Write;
} SHMRegionAccess_t;

/**
 * @brief Tracks one device mapping's fault mark on a region.
 *
 * Zero-initialize before first use. A mapping adds its fault here when its
 * recovery group is revoked, and clears it only after hardware stops, caches
 * are synced, and its device addresses are freed. Repeating Fault or ClearFault
 * on the same entry is safe.
 */
typedef struct SHMRegionFault {
    SHMRegionUsage_t* Region;
} SHMRegionFault_t;

/**
 * @brief Initializes a region usage tracker before the region layout is published.
 *
 * @param region Storage inside the SHM buffer's region table.
 */
KERNELABI void KERNELAPI
SHMRegionUsageInitialize(
    _Out_ SHMRegionUsage_t* region);

/**
 * @brief Starts a reader or writer access on a region and updates the token.
 *
 * Fails right away with OS_EBUSY instead of waiting if a conflicting reader,
 * writer, or cache recovery is active, or with OS_EDEVFAULT if any mapping on
 * the region has an active fault mark. Runs under an IRQ-safe spinlock so
 * state and token publication happen together.
 *
 * @param region     Target region tracker, kept alive by the caller's session.
 * @param access     Idle token record. Calls on the same record must not overlap.
 *                   Unchanged on failure.
 * @param generation Must equal the slot's last generation plus 1 (1 on first use).
 * @param write      True for an exclusive writer, false for a shared reader.
 * @return OS_EOK on success, OS_EBUSY if the slot is busy or access conflicts,
 *         OS_EDEVFAULT if the region is faulted, OS_EOVERFLOW if generation or
 *         reader counts hit UINT_MAX, or OS_EINVALPARAMS / OS_ENOENT for a
 *         zero or mismatched generation.
 */
KERNELABI oserr_t KERNELAPI
SHMRegionUsageBegin(
    _In_    SHMRegionUsage_t*  region,
    _InOut_ SHMRegionAccess_t* access,
    _In_    unsigned int       generation,
    _In_    bool               write);

/**
 * @brief Releases an access after CPU work, device work, and cache work finish.
 *
 * Works for both live accesses and stopped device accesses. Checks that
 * generation matches the active token so a late or duplicate call never changes
 * a newer access. Clears the token back to Idle while keeping Generation intact.
 *
 * @param access     Live or stopped token record.
 * @param generation Exact generation number used to start this access.
 * @return OS_EOK on success, OS_EINVALPARAMS if generation is 0, or OS_ENOENT
 *         if the token is idle or has a different generation.
 */
KERNELABI oserr_t KERNELAPI
SHMRegionUsageEnd(
    _InOut_ SHMRegionAccess_t* access,
    _In_    unsigned int       generation);

/**
 * @brief Moves an active device access from live counts into the stopped count.
 *
 * Call this only during stop cleanup, after hardware is confirmed stopped and
 * all running region syscalls for the group have drained. Moving stopped
 * tokens out of Readers and Writer lets cache recovery proceed as soon as
 * unrelated live users finish, while keeping the region's fault mark locked in
 * place until this token is retired with SHMRegionUsageEnd.
 *
 * At least one fault mark must already be installed on the region. Repeating
 * this call on an already stopped token with the same generation returns OS_EOK.
 *
 * @param access     Token record for a device transfer.
 * @param generation Exact generation number used to start the access.
 * @return OS_EOK on success, OS_ENOENT if idle or wrong generation,
 *         OS_EINVALPARAMS if generation is 0 or no fault is installed, or
 *         OS_EOVERFLOW if the stopped counter is at UINT_MAX. Unchanged on error.
 */
KERNELABI oserr_t KERNELAPI
SHMRegionUsageStop(
    _InOut_ SHMRegionAccess_t* access,
    _In_    unsigned int       generation);

/**
 * @brief Records a device mapping's fault mark on a region.
 *
 * Safe to call under locks or during revocation because it never allocates or
 * sleeps. Blocks any new SHMRegionUsageBegin calls on this region until all
 * faults are cleared. Calling this again for the same region is a no-op.
 *
 * @param region Region tracker kept alive by the mapping's buffer reference.
 * @param fault  Per-mapping fault entry. Calls on one entry must not overlap.
 * @return OS_EOK on success, OS_EINVALPARAMS if fault already belongs to a
 *         different region, or OS_EOVERFLOW if the fault count hit UINT_MAX.
 */
KERNELABI oserr_t KERNELAPI
SHMRegionUsageFault(
    _In_    SHMRegionUsage_t* region,
    _InOut_ SHMRegionFault_t* fault);

/**
 * @brief Removes a device mapping's fault mark after stop cleanup finishes.
 *
 * Refuses to remove the last fault while any stopped access or cache recovery
 * is still in progress, so new readers or writers cannot enter before the
 * region's memory and caches are clean. Does nothing if fault is empty.
 *
 * @param fault Fault entry to clear.
 * @return OS_EOK on success, or OS_EBUSY if removing the last fault would
 *         reopen the region while stopped tokens or recovery remain.
 */
KERNELABI oserr_t KERNELAPI
SHMRegionUsageClearFault(
    _InOut_ SHMRegionFault_t* fault);

/**
 * @brief Reserves exclusive cache recovery on a faulted region.
 *
 * Succeeds only once all live Readers and Writer have finished. Stopped device
 * tokens do not block this step because their hardware can no longer touch RAM.
 * Callers must ensure only one recovery pass runs at a time on a region.
 *
 * @param region Faulted region whose caches are about to be synced.
 * @return OS_EOK on success, OS_EINVALPARAMS if no fault is installed, or
 *         OS_EBUSY while live readers, a live writer, or another recovery remain.
 */
KERNELABI oserr_t KERNELAPI
SHMRegionUsageBeginRecovery(
    _In_ SHMRegionUsage_t* region);

/**
 * @brief Ends cache recovery on a region while its fault mark stays set.
 *
 * Orders cache maintenance before clearing Recovering so subsequent steps see
 * the cleaned memory state.
 *
 * @param region Region for which SHMRegionUsageBeginRecovery returned OS_EOK.
 */
KERNELABI void KERNELAPI
SHMRegionUsageEndRecovery(
    _In_ SHMRegionUsage_t* region);

#endif //!__SHM_REGION_USAGE_H__
