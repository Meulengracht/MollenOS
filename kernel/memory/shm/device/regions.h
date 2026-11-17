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
 * Internal structures and helpers for SHMDeviceMappingRegions mappings,
 * region sessions, and pre-allocated transfer token slots.
 */

#ifndef __SHM_DEVICE_REGIONS_PRIVATE_H__
#define __SHM_DEVICE_REGIONS_PRIVATE_H__

#include <shm/region.h>
#include <os/types/dma_region.h>

struct SHMDeviceRegionSession;

/**
 * @brief One registered buffer region inside a device mapping.
 *
 * Extents in the parent mapping describe contiguous chunks of physical RAM.
 * FirstExtent and ExtentOffset point to where this region starts in that list
 * so cache maintenance touches only this region's bytes. Fault records device
 * stop revocation on the region, and CacheDone prevents stop retries from
 * repeating final cache invalidation.
 */
typedef struct SHMDeviceRegion {
    SHMRegionBinding_t Binding;
    SHMRegionFault_t   Fault;
    size_t             Offset;
    unsigned int       FirstExtent;
    size_t             ExtentOffset;
    bool               CacheDone;
} SHMDeviceRegion_t;

/**
 * @brief Region table and session list attached to a SHMDeviceMappingRegions mapping.
 *
 * Context->MappingLock protects Sessions and SessionCount. Entries holds the
 * fixed array of Count regions determined when the mapping was prepared, and
 * lives until the mapping struct itself is freed.
 */
typedef struct SHMDeviceRegions {
    struct SHMDeviceRegionSession* Sessions;
    unsigned int                   SessionCount;
    unsigned int                   Count;
    SHMDeviceRegion_t               Entries[];
} SHMDeviceRegions_t;

/**
 * @brief Lifecycle state of one pre-allocated transfer token slot.
 */
enum SHMDeviceRegionPhase {
    // Slot has never been started yet (Generation is 0).
    SHMDeviceRegionIdle,
    // Begin reserved the region and is running pre-transfer cache work.
    SHMDeviceRegionPreparing,
    // Cache prep succeeded and hardware may now use the region.
    SHMDeviceRegionActive,
    // End is invalidating CPU caches before releasing the region reservation.
    SHMDeviceRegionFinishing,
    // Device stop occurred while Active. Waiting for final cache cleanup.
    SHMDeviceRegionStopped,
    // Transfer ended or was faulted by a device stop. Result holds its status.
    SHMDeviceRegionFinished
};

/**
 * @brief State for one transfer slot in a region session.
 *
 * Access links this slot into the buffer's region usage tracker. Generation and
 * Result preserve the last completed transfer's cycle number and status, so a
 * failed Begin or a repeated End call leaves the prior finished result intact.
 */
typedef struct SHMDeviceRegionToken {
    SHMRegionAccess_t          Access;
    enum SHMDeviceRegionPhase Phase;
    enum DMADirection         Direction;
    unsigned int              Generation;
    oserr_t                   Result;
} SHMDeviceRegionToken_t;

/**
 * @brief Open device access session bound to one region of a mapping.
 *
 * The session's SHMRecord ensures only one system call runs on it at a time.
 * Lock guards short token state updates and finished results against concurrent
 * recovery stops. The session keeps a reference on Mapping so the mapping
 * stays allocated until all sessions close or drain.
 *
 * Record and OperationSlot identify an in-flight Begin or End syscall so
 * process-exit cancellation can undo or finish that slot. Counted tracks
 * whether the recovery group's running-operation count was bumped for that call.
 * Abandoned means the handle closed while tokens were still running, leaving
 * mapping recovery to free the session later. Unlinked means it has already
 * been removed from Mapping->Regions->Sessions.
 */
typedef struct SHMDeviceRegionSession {
    struct SHMDeviceRegionSession* Next;
    SHMDeviceMapping_t*            Mapping;
    SHMDeviceRegion_t*             Region;
    Spinlock_t                    Lock;
    SHMRecord_t*                  Record;
    unsigned int                  Capacity;
    unsigned int                  OperationSlot;
    bool                          Counted;
    bool                          Abandoned;
    bool                          Unlinked;
    SHMDeviceRegionToken_t         Tokens[];
} SHMDeviceRegionSession_t;

/**
 * @brief Build the region entries and cache ranges for a region mapping.
 *
 * Checks that the mapped byte range aligns cleanly with registered buffer
 * regions and that each region's DMA segments meet the device requirements.
 * Call before installing the mapping's device addresses.
 *
 * @param mapping      Prepared SHMDeviceMappingRegions mapping with Extents and
 *                     AddressMapping populated.
 * @param requirements Optional device segment alignment limits, or NULL.
 * @return OS_EOK on success, OS_ENOTSUPPORTED if alignment fails, OS_EOOM on
 *         allocation failure, or an error from SHMRegionDescribeRange.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceRegionsPrepare(
    _In_ SHMDeviceMapping_t*              mapping,
    _In_ const struct DMAMapRequirements* requirements);

/**
 * @brief Fault every region in the mapping so new accesses see OS_EDEVFAULT.
 *
 * Marks all regions even when no transfer token is currently active, preventing
 * CPU or device callers from starting new work until faults are cleared.
 *
 * @param mapping Live or revoking region mapping.
 * @return OS_EOK if every region faulted, or the first error encountered.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceRegionsRevoke(
    _In_ SHMDeviceMapping_t* mapping);

/**
 * @brief Stop live region tokens and run post-stop cache cleanup.
 *
 * Call only after the recovery group confirms hardware has stopped. Moves all
 * active tokens out of the live state before invalidating CPU caches, then
 * marks affected tokens finished with OS_EDEVFAULT.
 *
 * @param mapping Region mapping whose hardware was stopped.
 * @return OS_EOK when all tokens and cache cleanups finished, or the first
 *         error from stopping or finishing a region (safe to retry).
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceRegionsStop(
    _In_ SHMDeviceMapping_t* mapping);

/**
 * @brief Clear device faults recorded on each region of the mapping.
 *
 * Call only after every active token has ended and the device addresses have
 * been released.
 *
 * @param mapping Region mapping being cleaned up after stop or release.
 * @return OS_EOK on success, or an error from SHMRegionUsageClearFault.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceRegionsClearFaults(
    _In_ SHMDeviceMapping_t* mapping);

/**
 * @brief Run CPU cache maintenance for one region before or after a DMA transfer.
 *
 * Surrounds cache operations with DMA memory barriers when maintenance is
 * needed. Callers must hold a region access or recovery access so concurrent
 * CPU or device writes cannot race with the cache lines.
 *
 * @param mapping   Parent region mapping carrying Cached and CachePolicy.
 * @param region    Target region specifying the byte range and starting extent.
 * @param direction Transfer direction (read, write, or both).
 * @param finish    False when starting a transfer, true when finishing it.
 */
KERNELABI void KERNELAPI
SHMDeviceRegionsCache(
    _In_ SHMDeviceMapping_t* mapping,
    _In_ SHMDeviceRegion_t*  region,
    _In_ enum DMADirection  direction,
    _In_ bool               finish);

/**
 * @brief Unlink and free abandoned sessions whose token slots have all drained.
 *
 * Call after clearing the mapping's Cleaning flag so any concurrent process-exit
 * abandon either frees its drained session itself or is picked up here.
 *
 * @param mapping Region mapping to scan for drained abandoned sessions.
 */
KERNELABI void KERNELAPI
SHMDeviceRegionsCollectSessions(
    _In_ SHMDeviceMapping_t* mapping);

#endif //!__SHM_DEVICE_REGIONS_PRIVATE_H__
