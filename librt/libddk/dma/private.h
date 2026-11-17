/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __DDK_DMA_PRIVATE_H__
#define __DDK_DMA_PRIVATE_H__

#include <ddk/dma.h>
#include <os/types/shm.h>
#include <os/types/dma_attachment.h>
#include <stdatomic.h>

// A buffer or a mapping created from a context. Its device segment list is kept
// here so the accessors can return it without asking the kernel again.
struct __DmaResource {
    // Next resource in the context's Pending list. Only used while on that list.
    struct __DmaResource* Next;
    // The context this resource was created from.
    struct DMAContext*    Context;
    // Kernel handle of the device mapping.
    uuid_t                Handle;
    uint32_t              SegmentCount;
    // Holds the first segment. A single segment list needs no allocation.
    struct DMASegment     InlineSegment;
    // Full segment list when there is more than one segment, otherwise NULL.
    struct DMASegment*    Segments;
    // SHM attachment that DMAAllocate made for the buffer. NULL for mappings,
    // because the caller already has its own attachment to the SHM buffer they map.
    SHMHandle_t*          Storage;
};

// Tracks a DMA mapping created by DMAMapSHM, DMAMapSHMDirect, DMAMapSHMPersistent,
// or DMAMapSHMRegions. Setup and teardown calls on the mapping must be serialized.
// Individual region sessions serialize their own transfers independently.
struct DMAMapping {
    // Kernel handle and cached device segments shared with DMABuffer.
    struct __DmaResource Resource;
    // DMA_MAP_* flags fixed when the mapping was created.
    unsigned int         Flags;
    // Mapped byte count, used to check streaming transfer ranges.
    size_t               Length;
    // Widest data direction allowed for transfers on this mapping.
    enum DMADirection    Direction;
    // Number of open DMARegionSession_t objects referencing this mapping.
    // DMAUnmap refuses to run while any region session is still open.
    unsigned int         Sessions;
    // Set when DMAUnmap begins closing a region mapping so failed unmaps block
    // new region sessions until the unmap retry finishes.
    bool                 Closing;
};

// The driver's DMA access to one device. At most one context exists per device
// in this process. All contexts are linked in a registry guarded by a lock.
struct DMAContext {
    struct DMAContext*    Next;
    uuid_t                Device;
    // Kernel handle of the open context. UUID_INVALID until the open succeeds.
    uuid_t                Handle;
    // Identifies the binding request. If the reply is lost, the same ID is sent
    // again so the service returns the same binding instead of making a new one.
    uint64_t              RequestId;
    // Permission the device service gave this process to open a context.
    OSDmaBindingResult_t  Binding;
    // Set when the binding reply was lost and the outcome is still unknown.
    bool                  BindingPending;
    // Set once close has started or the context only exists for cleanup.
    // No new buffers or mappings can be created from a closing context.
    bool                  Closing;
    // Number of buffers and mappings from this context that are not released yet.
    // Resources waiting in Pending are included.
    atomic_uint           Children;
    // Resources whose creation failed and whose cleanup must be retried.
    struct __DmaResource* Pending;
};

/**
 * @brief Saves a resource whose creation failed and whose cleanup failed too.
 * DMAClose retries the cleanup later. Without this the kernel handle or the SHM
 * attachment would be lost.
 *
 * @param resource Resource that was never returned to the driver. The context takes
 * it over and frees it once cleanup succeeds.
 */
void
__DmaDeferCleanup(
    _In_ struct __DmaResource* resource);

/**
 * @brief Retries the cleanup of every resource saved by __DmaDeferCleanup.
 * The context registry lock must be held.
 *
 * @param context Context to clean up. The caller must make sure nothing else is
 * creating resources on it or closing it.
 * @return OS_EOK, or the first cleanup error. Resources that still fail stay
 * on the list for the next try.
 */
oserr_t
__DmaCollect(
    _In_ struct DMAContext* context);

#endif
