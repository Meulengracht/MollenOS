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

enum SHMDeviceOperationPhase {
    SHMDeviceOperationRunning,
    SHMDeviceOperationRangeAdmission
};

/**
 * @brief Ordered setup steps used to unwind a partially started operation.
 * Each value includes earlier steps. Context-level calls skip the mapping
 * reference, and unbound calls skip the recovery-group counter.
 */
enum SHMDeviceOperationSetup {
    SHMDeviceOperationSetupEmpty,
    SHMDeviceOperationSetupContext,
    SHMDeviceOperationSetupMapping,
    SHMDeviceOperationSetupCounted
};

/**
 * @brief Tracking token embedded inside a context or mapping, guarded by its SHMRecord.
 *
 * Only one system call can use the SHMRecord at a time. PendingOperation is
 * recorded before incrementing the recovery group's counter, and exit cleanup
 * reads these fields only after every caller has stopped. References held here
 * keep the struct and context in memory even if unmap drops the mapping's
 * initial reference. Only updating the recovery group's counter needs group->Lock.
 */
struct SHMDeviceOperation {
    SHMRecord_t*                  Record;
    SHMDeviceContext_t*           Context;
    SHMDeviceMapping_t*           Mapping;
    enum SHMDeviceOperationKind   Kind;
    enum SHMDeviceOperationPhase  Phase;
    enum SHMDeviceOperationSetup  Setup;
    // True when a release started after hardware stop was already confirmed.
    bool                          Stopped;
};

/**
 * @brief Device address rules, cache policy, and mappings waiting for cleanup.
 *
 * CachePolicy, AddressLimit, and AddressSpace stay fixed after creation.
 * Mappings holds both live mappings and abandoned ones waiting for stop cleanup.
 * MappingLock guards the list links, abandonment flag, and cleanup exclusion.
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
 * @brief Kernel views used to copy between the buffer and temporary device pages.
 *
 * BounceMapping is 0 when the device uses the original buffer pages directly.
 * Allocation tracks the temporary pages returned to the device pool on cleanup.
 */
typedef struct SHMDeviceBounce {
    vaddr_t                       SourceMapping;       // Kernel view of the buffer's pages.
    size_t                        SourceMappingLength;
    size_t                        SourceOffset;        // Where the range starts inside that view.
    vaddr_t                       BounceMapping;       // Kernel view of bounce pages. The range starts at 0.
    struct SystemMemoryAllocation Allocation;   // Allocation used for the bounce pages.
} SHMDeviceBounce_t;

typedef struct SHMDeviceStream {
    size_t            Offset;
    size_t            Length;
    SHMDeviceBounce_t Bounce;
} SHMDeviceStream_t;

enum SHMDeviceMappingState {
    SHMDeviceMappingReady,
    SHMDeviceMappingReleasing,
    SHMDeviceMappingReleasingAfterStop,
    SHMDeviceMappingStopped
};

/**
 * @brief Internal reference count and context list links for a mapping.
 *
 * MappingLock guards the links, Abandoned, and Cleaning, while References uses
 * atomic updates. Holding a reference keeps the C struct in memory, separate
 * from its DMA addresses and pages (touching those requires an active operation
 * token or an exclusive stop-cleanup pass).
 */
typedef struct SHMDeviceMappingLifetime {
    SHMDeviceMapping_t* Next;
    SHMDeviceMapping_t* Previous;
    atomic_uint         References;
    bool                Abandoned;
    bool                Cleaning;
} SHMDeviceMappingLifetime_t;

/**
 * @brief Buffer range, transfer state, and resources for one device mapping.
 *
 * Extents holds the physical memory blocks used for cache syncs, AddressMapping
 * holds the device addresses, and Access pins the buffer and checks for
 * conflicting CPU or device use. System calls on one mapping run one at a time
 * through SHMRecord, though a recovery-group stop can overlap them and update
 * Access under Buffer->Mutex. MappingLock guards list walks and keeps two
 * cleanups from running at once.
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
 * @brief Initialize a newly allocated device context's reference and lock state.
 * The caller fills in cache and address-space fields once their values are known.
 *
 * @param context Uninitialized storage for a device context.
 */
KERNELABI void KERNELAPI
SHMDeviceContextInitialize(
    _Out_ struct SHMDeviceContext* context);

/**
 * @brief Checks that the operation token matches this context, mapping, and kind.
 *
 * NULL is allowed only for unbound contexts. Any non-NULL token must match the
 * exact context, mapping, and kind (pass NULL for mapping on context-level work).
 */
KERNELABI bool KERNELAPI
SHMDeviceOperationIsValid(
    _In_Opt_ const SHMDeviceOperation_t* operation,
    _In_     const SHMDeviceContext_t*   context,
    _In_Opt_ const SHMDeviceMapping_t*   mapping,
    _In_     enum SHMDeviceOperationKind kind);

/**
 * @brief Waits for a device range and restores the token before touching buffers.
 *
 * Call with a Submit token (or NULL for non-overlapping unbound work). If
 * waiting hands off to a kernel worker thread (OS_EFORKED), the token stays in
 * the waiting-for-range phase for that worker. Every other return switches it
 * back to Running, and success rechecks that the binding is still live.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceOperationBeginAccess(
    _In_Opt_ SHMDeviceOperation_t*       operation,
    _In_     SHMAccess_t*                access,
    _In_     size_t                      offset,
    _In_     size_t                      length,
    _In_     bool                        write,
    _In_Opt_ const struct SHMAccessWait* wait);

// Requires a mapping whose kind was validated during construction.
static inline bool
__SHMDeviceMappingIsStreaming(
    _In_ const SHMDeviceMapping_t* mapping)
{
    return mapping->Kind == SHMDeviceMappingStreamingBounce ||
        mapping->Kind == SHMDeviceMappingStreamingDirect;
}

/**
 * @brief Convert physical byte ranges into a count or list of device segments.
 *
 * Hold a context reference until the call returns. The caller must check
 * requirements before calling. First count the segments, then provide enough
 * room for every entry. Keep extents and requirements unchanged while counting
 * and filling.
 *
 * @param context Valid context with fixed address rules.
 * @param extents Physical byte ranges in buffer order.
 * @param extentCount Number of entries in extents, at least one.
 * @param requirements Optional alignment requirements for completed segments.
 * @param segmentsOut NULL to count only, otherwise room for every segment.
 * @param countOut Receives the number of segments on success.
 * @return OS_EOK on success. OS_EINVALPARAMS for an empty or overflowing range.
 *         OS_ENOENT if a byte has no device address. OS_ENOTSUPPORTED for unmet
 *         alignment or a segment count that does not fit in uint32_t.
 *         Discard partial output on failure.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextWalkSegments(
    _In_  const struct SHMDeviceContext*   context,
    _In_  const SHMSG_t*                   extents,
    _In_  int                              extentCount,
    _In_  const struct DMAMapRequirements* requirements,
    _Out_ struct DMASegment*               segmentsOut,
    _Out_ uint32_t*                        countOut);

/**
 * @brief Release initialized bounce views and their device-pool allocation.
 * Call only when no other cleanup or device transfer can touch these pages.
 * Fields are cleared as each piece is freed, including after partial setup.
 *
 * @param bounce Bounce storage to clear. A zero-initialized record is valid.
 */
KERNELABI void KERNELAPI
SHMDeviceReleaseBounce(
    _InOut_ SHMDeviceBounce_t* bounce);

/**
 * @brief Finish cache handling for device writes to a validated mapping range.
 * Confirm that the device finished or stopped before calling. The range must
 * fit inside the mapping, and callers must keep access and cleanup from
 * overlapping this call.
 *
 * @param mapping Mapping with stable physical extents and cache policy.
 * @param offset First byte relative to the mapping.
 * @param length Number of bytes to complete. Passing 0 still runs the barrier.
 */
KERNELABI void KERNELAPI
SHMDeviceCompleteForCpu(
    _In_ SHMDeviceMapping_t* mapping,
    _In_ size_t              offset,
    _In_ size_t              length);

/**
 * @brief Adds a mapping to its context's list before publishing the handle.
 *
 * The mapping must already hold its context/buffer references and initial
 * reference count. Bound contexts must keep an active operation open until
 * publishing or cleanup on failure finishes.
 *
 * @param mapping Unlisted mapping whose context and access fields are ready.
 */
KERNELABI void KERNELAPI
SHMDeviceMappingLink(
    _In_ SHMDeviceMapping_t* mapping);

/**
 * @brief Increments a mapping's internal reference count without granting DMA use.
 *
 * Requires an existing reference or MappingLock protecting the list entry.
 *
 * @param mapping Mapping to keep in memory.
 * @return OS_EOK, OS_ECANCELLED if already 0, or OS_EOVERFLOW at the limit.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceMappingAcquire(
    _In_ SHMDeviceMapping_t* mapping);

/**
 * @brief Drops a mapping reference outside all locks and frees it when zero.
 *
 * Device resources must already be freed before the final reference drops.
 * Removes the entry from the context list, frees the struct, and drops its
 * context reference.
 *
 * @param mapping Reference pointer to drop and set to NULL. No-op if *mapping is NULL.
 */
KERNELABI void KERNELAPI
SHMDeviceMappingRelease(
    _InOut_ SHMDeviceMapping_t** mapping);

/**
 * @brief Frees device resources while keeping the mapping struct allocated.
 *
 * Hold Lifetime.Cleaning under MappingLock so no list walk reads these fields
 * concurrently. Passing stopped=true also requires confirmed hardware stop
 * with no background worker waiter able to wake up and resume.
 *
 * @param mapping Mapping protected against overlapping cleanup (including one
 *                partially cleaned earlier).
 * @param stopped True to sync full-range caches and clear any range lockdown.
 * @return OS_EOK, or an address-release error (keeping remaining pages allocated).
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceMappingCleanup(
    _In_ SHMDeviceMapping_t* mapping,
    _In_ bool                stopped);

#endif //!__MS_SHM_DEVICE_PRIVATE_H__
