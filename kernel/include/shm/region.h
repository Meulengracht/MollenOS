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
 * Region layout registration and CPU access sessions for shared-memory (SHM)
 * buffers. Buffers created with SHM_REGIONS can be split once into sorted,
 * cache-line-aligned regions so CPU code and devices can lock each region
 * independently without scanning an interval list.
 */

#ifndef __SHM_REGION_H__
#define __SHM_REGION_H__

#include <os/types/shm_region.h>
#include <shm/region_usage.h>

struct SHMBuffer;
struct SHMRegionCpuSession;

/**
 * @brief Describes one registered region inside a backing SHM buffer.
 *
 * Holds the region's public identity (buffer ID, region index, and generation),
 * a pointer to its shared usage tracker, and its byte offset and length from
 * the start of the backing buffer. Keep a reference to the backing buffer alive
 * for as long as Usage is read or modified.
 */
typedef struct SHMRegionBinding {
    OSBufferIdentity_t Identity;
    SHMRegionUsage_t*  Usage;
    size_t             Offset;
    size_t             Length;
} SHMRegionBinding_t;

/**
 * @brief Looks up the registered regions that exactly cover a byte range.
 *
 * Used while setting up a SHMDeviceMappingRegions mapping. The range must start
 * at the first byte of a registered region and end at the last byte of a
 * registered region, with contiguous regions in between and no gaps.
 *
 * Call this first with bindings set to NULL to find out how many entries are
 * needed in countOut, then allocate the array and call again.
 *
 * @param buffer   Backing SHM buffer. Hold a handle reference during the call.
 * @param offset   First byte of the range from the start of the backing buffer
 *                 (after adding any partial view's offset).
 * @param length   Nonzero byte count to cover.
 * @param bindings Optional array receiving one entry per region in buffer order,
 *                 or NULL to query only the required count.
 * @param capacity Number of entries available in bindings. Ignored when bindings is NULL.
 * @param countOut Receives the number of regions covering the range.
 * @return OS_EOK on success, OS_ENOTSUPPORTED if the buffer has no region
 *         support or no registered layout yet, OS_EBUFFER if bindings is
 *         non-NULL and capacity is smaller than countOut, or OS_EINVALPARAMS
 *         for an empty, overflowing, or misaligned range.
 */
KERNELABI oserr_t KERNELAPI
SHMRegionDescribeRange(
    _In_  struct SHMBuffer*    buffer,
    _In_  size_t              offset,
    _In_  size_t              length,
    _Out_ SHMRegionBinding_t* bindings,
    _In_  unsigned int        capacity,
    _Out_ unsigned int*       countOut);

/**
 * @brief Registers the fixed region layout for a shared-memory buffer.
 *
 * Splits the buffer into non-overlapping, cache-line-aligned regions, ordered
 * by offset. Registration happens once per buffer and applies to all processes
 * and devices that attach afterward. A partial view or read-only handle cannot
 * set the layout.
 *
 * @param shm    Writable handle to the full SHM_REGIONS buffer (not a partial view).
 * @param ranges Kernel-copied array of region offsets and lengths, sorted and disjoint.
 * @param count  Number of entries in ranges, from 1 up to SHM_REGION_LIMIT.
 * @return OS_EOK on success, OS_EBUSY if a layout is already registered,
 *         OS_ENOTSUPPORTED if the buffer lacks SHM_REGIONS, OS_EPERMISSIONS for
 *         a partial view or read-only handle, OS_EINVALPARAMS for bad ranges,
 *         or OS_EOOM if allocation fails.
 */
oserr_t
SHMRegionRegister(
    _In_ uuid_t                       shm,
    _In_ const struct SHMRegionRange* ranges,
    _In_ unsigned int                 count);

/**
 * @brief Opens a CPU access session on one region of a buffer.
 *
 * Pre-allocates a fixed array of token slots so later begin and end calls never
 * allocate memory or sleep. The caller's SHM handle must cover the entire region.
 *
 * @param shm         SHM handle or view covering the whole target region.
 * @param region      Zero-based index of the region in the buffer's layout.
 * @param capacity    Number of token slots to allocate (1 to SHM_REGION_TOKEN_LIMIT).
 * @param sessionOut  Receives the new session on success. Unchanged on error.
 * @param identityOut Receives the region's canonical identity on success.
 * @return OS_EOK on success, OS_ENOENT if no layout is registered or region is
 *         out of range, OS_EPERMISSIONS if the handle lacks read access or does
 *         not cover the full region, OS_EINVALPARAMS for bad capacity, or OS_EOOM.
 */
oserr_t
SHMRegionCpuCreate(
    _In_  uuid_t                       shm,
    _In_  unsigned int                 region,
    _In_  unsigned int                 capacity,
    _Out_ struct SHMRegionCpuSession** sessionOut,
    _Out_ OSBufferIdentity_t*          identityOut);

/**
 * @brief Starts a CPU read or write access in one token slot without waiting.
 *
 * Shared readers can overlap each other. A writer needs exclusive access with
 * no other reader or writer active. Each use of a slot must pass the next
 * generation number (1 on first use, then previous generation plus 1) so stale
 * end calls from earlier cycles cannot affect a newer access.
 *
 * @param session    Open CPU session. Calls on the same session must not overlap.
 * @param slot       Token slot index (0 to capacity - 1).
 * @param generation Next expected generation number for this slot (nonzero).
 * @param access     SHMCpuAccessRead or SHMCpuAccessWrite.
 * @return OS_EOK on success, OS_EBUSY if the slot is in use or another reader/writer
 *         conflicts, OS_EDEVFAULT if a device mapping on this region faulted,
 *         OS_EPERMISSIONS for write access on a read-only handle, OS_EOVERFLOW if
 *         generation or reader counts reached their limit, or OS_EINVALPARAMS /
 *         OS_ENOENT for a bad slot or wrong generation.
 */
oserr_t
SHMRegionCpuBegin(
    _In_ struct SHMRegionCpuSession* session,
    _In_ unsigned int                slot,
    _In_ unsigned int                generation,
    _In_ enum SHMCpuAccess           access);

/**
 * @brief Ends an active CPU access after the caller finishes touching the memory.
 *
 * @param session    Open CPU session.
 * @param slot       Active token slot index.
 * @param generation Exact generation number passed to the matching SHMRegionCpuBegin.
 * @return OS_EOK on success, or OS_EINVALPARAMS / OS_ENOENT if the slot is idle
 *         or generation does not match.
 */
oserr_t
SHMRegionCpuEnd(
    _In_ struct SHMRegionCpuSession* session,
    _In_ unsigned int                slot,
    _In_ unsigned int                generation);

/**
 * @brief Closes a CPU region session and frees its token slots.
 *
 * During normal close (abandon is false), every token slot must already be idle.
 * During process exit cleanup (abandon is true, called after every thread in
 * the process has stopped), any still-active tokens are ended automatically.
 *
 * @param session Pointer to the session. Set to NULL on success.
 * @param abandon True during process exit cleanup, false for normal close.
 * @return OS_EOK on success, or OS_EBUSY if abandon is false and any slot is still active.
 */
oserr_t
SHMRegionCpuDestroy(
    _InOut_ struct SHMRegionCpuSession** session,
    _In_    bool                         abandon);

#endif //!__SHM_REGION_H__
