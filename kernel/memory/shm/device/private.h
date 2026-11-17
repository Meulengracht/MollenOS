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

#ifndef __MS_SHM_DEVICE_PRIVATE_H__
#define __MS_SHM_DEVICE_PRIVATE_H__

#include <shm/device.h>
#include <shm/access.h>
#include <component/memory.h>
#include <dma/address.h>
#include <mutex.h>
#include <dma/attachment.h>
#include <stdatomic.h>

#include "../private.h"
#include "../../utils.h"

enum SHMDeviceOperationPhase {
    SHMDeviceOperationRunning,
    SHMDeviceOperationRangeAdmission
};

/**
 * @brief Tracks how far operation setup got, so a failed start can be undone.
 *
 * Setup takes a context reference, then a mapping reference, then registers
 * the operation with the binding's recovery group. Each value means all the
 * earlier steps are done. Operations on a context alone skip the mapping step.
 * Once fully set up (Counted), the operation is part of the recovery group's
 * count of running operations. The group waits for that count to reach zero
 * before it frees anything.
 */
enum SHMDeviceOperationSetup {
    SHMDeviceOperationSetupEmpty,
    SHMDeviceOperationSetupContext,
    SHMDeviceOperationSetupMapping,
    SHMDeviceOperationSetupCounted
};

/**
 * @brief Token that marks one system call as using a context or mapping.
 *
 * The token lives inside the context or mapping struct, so starting an
 * operation never allocates. Its SHMRecord (the kernel handle wrapper) guards
 * it, and only one system call can use a record at a time. The record's
 * PendingOperation field is set before the recovery group's counter goes up.
 * When a process exits, cleanup reads these fields only after every thread in
 * the process has stopped, and uses them to tell an interrupted call from an
 * idle handle. The references taken here keep the struct and context in memory
 * even if unmap drops the mapping's initial reference. Only changing the
 * recovery group's counter needs group->Lock.
 */
struct SHMDeviceOperation {
    SHMRecord_t*                  Record;
    SHMDeviceContext_t*           Context;
    SHMDeviceMapping_t*           Mapping;
    enum SHMDeviceOperationKind   Kind;
    enum SHMDeviceOperationPhase  Phase;
    enum SHMDeviceOperationSetup  Setup;
    // True when this release started after the recovery group had already
    // confirmed that the hardware stopped. Unmap then frees the mapping the
    // same way stop cleanup does.
    bool                          Stopped;
};

/**
 * @brief One driver's DMA access to one device, with the mappings made through it.
 *
 * Binding links the context to the device and to its recovery group, which is
 * the set of devices that must be stopped together. CachePolicy says whether
 * the device sees the same data as the CPU caches. A coherent device does. A
 * non-coherent device reads and writes RAM directly, so the CPU must write
 * back or discard cached copies around each transfer.
 *
 * CachePolicy, AddressLimit, and AddressSpace never change after creation.
 * Mappings holds live mappings and abandoned ones. A mapping is abandoned
 * when its handle went away while the device might still be using it. It
 * waits on this list until the hardware stop is confirmed.
 * MappingLock guards the list links, the Abandoned flag, and the Cleaning flag
 * that keeps two cleanups from running on one mapping.
 */
struct SHMDeviceContext {
    _Atomic(unsigned int)     References;
    DMABinding_t*             Binding;
    SHMDeviceOperation_t      Operation;
    Mutex_t                   MappingLock;
    SHMDeviceMapping_t*       Mappings;
    enum SHMDeviceCachePolicy CachePolicy;
    uint64_t                  AddressLimit;
    struct DMAAddressSpace*   AddressSpace;
};

/**
 * @brief Kernel views used to copy between the buffer and bounce pages.
 *
 * Bounce pages are temporary pages from the device pool that the device can
 * reach. The kernel copies data between them and the real buffer when the
 * device cannot use the buffer's own pages. SourceMapping and BounceMapping
 * are the kernel views used for that copying.
 *
 * BounceMapping is 0 when the device uses the original buffer pages directly.
 * Allocation records the bounce pages so they go back to the device pool on
 * cleanup.
 */
typedef struct SHMDeviceBounce {
    vaddr_t                       SourceMapping;       // Kernel view of the buffer's pages.
    size_t                        SourceMappingLength;
    size_t                        SourceOffset;        // Where the range starts inside that view.
    vaddr_t                       BounceMapping;       // Kernel view of the bounce pages. The range starts at 0.
    struct SystemMemoryAllocation Allocation;   // Device pool allocation behind the bounce pages.
} SHMDeviceBounce_t;

// Offset and Length hold the range of the transfer that is in progress.
typedef struct SHMDeviceStream {
    size_t            Offset;
    size_t            Length;
    SHMDeviceBounce_t Bounce;
} SHMDeviceStream_t;

enum SHMDeviceMappingState {
    // The layout and the backing storage are done, but the device addresses
    // are not installed yet or the final check that the binding is still open
    // has not passed.
    // Once linked, cancellation can already lock the range of a Preparing mapping.
    SHMDeviceMappingPreparing,
    // The mapping can be used.
    SHMDeviceMappingReady,
    // Release has started and the device resources are being freed.
    SHMDeviceMappingReleasing,
    // Same as above, but the hardware stop is confirmed and the caches for the
    // full range are already synced.
    SHMDeviceMappingReleasingAfterStop,
    // All device resources are freed. Only the struct itself is left.
    SHMDeviceMappingStopped
};

/**
 * @brief Reference count and context list links for a mapping.
 *
 * The context's MappingLock guards the links, Abandoned, and Cleaning.
 * References is updated atomically. Holding a reference only keeps the struct
 * in memory. To touch the device addresses or pages, hold an active operation
 * token or be the stop-cleanup pass that has exclusive access.
 *
 * Abandoned is set when the handle went away before the hardware stopped, so
 * the context list now holds the handle's reference. Cleaning is set while one
 * thread frees the device resources, so no other thread starts the same work.
 */
typedef struct SHMDeviceMappingLifetime {
    SHMDeviceMapping_t* Next;
    SHMDeviceMapping_t* Previous;
    atomic_uint         References;
    bool                Abandoned;
    bool                Cleaning;
} SHMDeviceMappingLifetime_t;

/**
 * @brief A buffer range made visible to a device, with its transfer state.
 *
 * Extents lists the physical memory blocks, which cache syncs use to find the
 * addresses to work on. AddressMapping holds the device addresses the driver
 * programs into the hardware. Access reserves the range so the CPU and the
 * device do not use it at the same time. System calls on one mapping run one
 * at a time through its SHMRecord. A recovery group stop can still run
 * alongside them and change Access under Buffer->Mutex. MappingLock guards
 * list walks and keeps two cleanups from running at once.
 */
struct SHMDeviceMapping {
    struct SHMAccess           Access;
    SHMDeviceMappingLifetime_t Lifetime;
    SHMDeviceOperation_t       Operation;
    bool                       Cached;
    SHMDeviceContext_t*        Context;
    size_t                     Offset;
    size_t                     Length;
    enum DMADirection          Direction;
    enum SHMDeviceMappingKind  Kind;
    enum SHMDeviceMappingState State;

    union {
        SHMDeviceStream_t Stream;
        struct {
            unsigned int Uses;
        } Persistent;
    };

    SHMSG_t*                  Extents;
    int                       ExtentCount;
    struct DMAAddressMapping* AddressMapping;
};

/**
 * @brief Set up the reference count and lock of a newly allocated device context.
 * The context starts with one reference. The caller fills in the cache policy
 * and address space fields once their values are known.
 *
 * @param context Uninitialized storage for a device context.
 */
KERNELABI void KERNELAPI
SHMDeviceContextInitialize(
    _Out_ struct SHMDeviceContext* context);

/**
 * @brief Check that an operation token belongs to this context, mapping, and kind.
 *
 * The token must be fully set up (counted by the recovery group), must be the
 * one stored in its record, and must match all three values exactly. A NULL
 * token is invalid. Work on a context alone passes NULL for mapping.
 */
KERNELABI bool KERNELAPI
SHMDeviceOperationIsValid(
    _In_Opt_ const SHMDeviceOperation_t* operation,
    _In_     const SHMDeviceContext_t*   context,
    _In_Opt_ const SHMDeviceMapping_t*   mapping,
    _In_     enum SHMDeviceOperationKind kind);

/**
 * @brief Reserve a buffer range for the device, waiting if someone else holds it.
 *
 * Call with a fully set up Submit token. If the wait moves to a kernel worker
 * thread (OS_EFORKED), the token stays in the waiting phase so the worker can
 * carry on with it. Every other return sets the token back to Running. On
 * success this also checks again that the binding is still live, because it
 * may have been cut off while waiting.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceOperationBeginAccess(
    _In_     SHMDeviceOperation_t*       operation,
    _In_     SHMAccess_t*                access,
    _In_     size_t                      offset,
    _In_     size_t                      length,
    _In_     bool                        write,
    _In_Opt_ const struct SHMAccessWait* wait);

// Streaming mappings handle one transfer at a time. The mapping kind must
// already be validated, which mapping creation does.
static inline bool
__SHMDeviceMappingIsStreaming(
    _In_ const SHMDeviceMapping_t* mapping)
{
    return mapping->Kind == SHMDeviceMappingStreamingBounce ||
        mapping->Kind == SHMDeviceMappingStreamingDirect;
}

/**
 * @brief Prepare a buffer range and its device layout, without installing it.
 *
 * The device layout is what the device needs to reach the range. That is the
 * physical extents, the bounce pages if any, and the device addresses. The
 * device cannot use any of it until the mapping is installed later.
 *
 * Call this after the buffer kind, direction, requirements and the caller's
 * access to the range have been checked. Offset is counted from the start of
 * the backing buffer, and length must not be zero. The caller must hold the
 * buffer reference, the context reference and an active operation token. This
 * function does not take or release any of them.
 *
 * On success the mapping is returned in the Preparing state and is not on the
 * context list yet. The caller sets Access.ReferenceID and Access.Scope, links
 * the mapping and then installs its device addresses. Once linked, the mapping
 * holds the caller's buffer and context references. On failure, everything
 * allocated here is freed and mappingOut is unchanged. The caller still holds
 * its references and must release them.
 *
 * @param buffer Backing buffer. Its pages must stay allocated during the call.
 * @param context Context whose address space and cache policy describe the device.
 * @param offset First byte of the range, counted from the start of buffer.
 * @param length Number of bytes to map.
 * @param direction Whether the device reads, writes or both. Must be valid.
 * @param kind Streaming, persistent or shared mapping kind. Must be valid.
 * @param requirements Optional alignment limits for each device segment.
 * @param allocationRequirements Optional limits for the whole buffer as the
 *                               device sees it (one segment, alignment, boundary).
 * @param mappingOut Receives the prepared mapping only on success.
 * @return OS_EOK on success. OS_EINCOMPLETE if a page in the range was never
 *         allocated. OS_ENOTSUPPORTED if the layout cannot meet the limits.
 *         Otherwise the error from allocating bounce pages or building the
 *         device addresses.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceMappingPrepare(
    _In_  struct SHMBuffer*                buffer,
    _In_  SHMDeviceContext_t*              context,
    _In_  size_t                           offset,
    _In_  size_t                           length,
    _In_  enum DMADirection                direction,
    _In_  enum SHMDeviceMappingKind        kind,
    _In_  const struct DMAMapRequirements* requirements,
    _In_  const struct DMARequirements*    allocationRequirements,
    _Out_ SHMDeviceMapping_t**             mappingOut);

/**
 * @brief Free the kernel views of the bounce pages and return the pages to the
 *        device pool.
 *
 * Call only when no other cleanup and no device transfer can touch these
 * pages. Each field is cleared as its piece is freed, so this is safe to call
 * after setup stopped halfway.
 *
 * @param bounce Bounce storage to clear. A zero-initialized record is valid.
 */
KERNELABI void KERNELAPI
SHMDeviceReleaseBounce(
    _InOut_ SHMDeviceBounce_t* bounce);

/**
 * @brief Make data the device wrote visible to the CPU.
 *
 * For a non-coherent device this discards stale cached copies of the range.
 * Call only after the device has finished or has been confirmed stopped. The
 * range must lie inside the mapping, and callers must keep other access and
 * cleanup from running at the same time.
 *
 * @param mapping Mapping whose physical extents and cache policy do not change.
 * @param offset First byte relative to the mapping.
 * @param length Number of bytes to complete. Passing 0 still runs the memory
 *               barrier, which orders earlier memory accesses first.
 */
KERNELABI void KERNELAPI
SHMDeviceCompleteForCpu(
    _In_ SHMDeviceMapping_t* mapping,
    _In_ size_t              offset,
    _In_ size_t              length);

/**
 * @brief Add a mapping to its context's list before its handle is published.
 *
 * The mapping must already hold its context and buffer references and its
 * initial reference count. The caller must keep its operation token active
 * until the handle is published, or until failure cleanup is done.
 *
 * @param mapping Unlisted mapping whose context and access fields are ready.
 */
KERNELABI void KERNELAPI
SHMDeviceMappingLink(
    _In_ SHMDeviceMapping_t* mapping);

/**
 * @brief Add a reference that only keeps the mapping struct in memory.
 *
 * This does not allow the caller to use the mapping for DMA. The caller must
 * already hold a reference, or hold MappingLock while the entry is on the list.
 *
 * @param mapping Mapping to keep in memory.
 * @return OS_EOK, OS_ECANCELLED if the count already reached 0 (the mapping is
 *         being freed), or OS_EOVERFLOW if the count is at its maximum.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceMappingAcquire(
    _In_ SHMDeviceMapping_t* mapping);

/**
 * @brief Drop a mapping reference and free the mapping when the count reaches zero.
 *
 * Call this with no locks held. The device resources must already be freed
 * before the last reference goes. On the last reference this takes the mapping
 * off the context list, frees the struct, and drops the mapping's reference on
 * its context.
 *
 * @param mapping Pointer to the reference to drop. It is set to NULL. Does
 *                nothing if it already points to NULL.
 */
KERNELABI void KERNELAPI
SHMDeviceMappingRelease(
    _InOut_ SHMDeviceMapping_t** mapping);

/**
 * @brief Free the device resources of a mapping and keep the struct allocated.
 *
 * The caller must have set Lifetime.Cleaning under MappingLock, so no other
 * cleanup or list walk uses these fields at the same time. Passing true for
 * stopped also requires that the hardware stop is confirmed and that no
 * waiting worker thread can wake up and resume.
 *
 * @param mapping Mapping that no other cleanup is working on. An earlier
 *                attempt may have cleaned it partly.
 * @param stopped True when the hardware is confirmed stopped. Cleanup then
 *                syncs caches for the full range and lifts the lockdown on
 *                the buffer range.
 * @return OS_EOK, or the error from releasing the device addresses. After that
 *         error the remaining pages stay allocated and the call can be retried.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceMappingCleanup(
    _In_ SHMDeviceMapping_t* mapping,
    _In_ bool                stopped);

#endif //!__MS_SHM_DEVICE_PRIVATE_H__
