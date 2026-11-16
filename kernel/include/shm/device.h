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

#ifndef __SHM_DEVICE_H__
#define __SHM_DEVICE_H__

#include <os/osdefs.h>
#include <os/types/shm.h>
#include <os/types/dma.h>
#include <os/types/dma_attachment.h>
#include <os/types/async.h>
#include <os/types/time.h>
#include <shm/access.h>

// Accept all 64 ranges returned by deviced. Firmware parsing stays in deviced.
#define SHM_DEVICE_MAX_RANGES DMA_MAX_RANGES

// Opaque types
DECL_STRUCT(SHMDeviceContext);
DECL_STRUCT(SHMDeviceMapping);
DECL_STRUCT(SHMRecord);

/**
 * @brief Tracks an active operation on a context or mapping record.
 *
 * A bound context is tied to a DMA manager binding (used by user-space
 * drivers), while an unbound context is opened directly by kernel code without
 * a manager. Bound contexts require a tracking token from
 * SHMDeviceContextBeginOperation or SHMDeviceMappingBeginOperation so the
 * recovery group counts running work. Pass that token to every device call,
 * keep it alive until results are copied to user space and any cleanup on error
 * finishes, then release it with SHMDeviceOperationEnd. Unbound kernel callers
 * that already make sure their own calls do not overlap can pass NULL.
 */
typedef struct SHMDeviceOperation SHMDeviceOperation_t;

/**
 * @brief How a mapping uses buffer pages and handles CPU caches.
 */
enum SHMDeviceMappingKind {
    // One transfer at a time. Can copy data through extra device-reachable pages (bounce pages).
    SHMDeviceMappingStreamingBounce,
    // One transfer at a time, using the original SHM pages directly.
    SHMDeviceMappingStreamingDirect,
    // Original pages with a use count so multiple jobs can overlap. Cache sync covers the whole range.
    SHMDeviceMappingPersistent,
    // Device-pool pages shared by the CPU and device at the same time without sync calls.
    SHMDeviceMappingShared
};

enum SHMDeviceOperationKind {
    SHMDeviceOperationSubmit,
    SHMDeviceOperationComplete,
    SHMDeviceOperationRelease
};

/**
 * @brief Create a device context with a copy of the physical-to-device address ranges.
 *
 * Some buses offset or window RAM so devices see different addresses than the
 * CPU. Pass kernel pointers only. This call sets up the translation table only.
 * It does not register or start the device, reserve buffer pages, or change
 * cache settings. The DDK DMA session must hold its device assignment from
 * deviced until every mapping and the kernel context are released.
 *
 * @param ranges Physical-to-device ranges in any order. Keep them unchanged
 *               until the call returns.
 * @param count Number of ranges. Must be 1 through SHM_DEVICE_MAX_RANGES.
 * @param cachePolicy Whether the device sees CPU caches automatically (coherent)
 *                    or needs explicit cache flushes (non-coherent).
 * @param addressLimit Largest device address allowed, inclusive. Stored in the
 *                     context for all its mappings. UINT64_MAX means no limit.
 *                     Use UINT32_MAX for a 32-bit-only device.
 * @param contextOut Address of an initially NULL pointer. Receives a context
 *                   with one reference for the caller to release.
 * @return OS_EOK on success. OS_EINVALPARAMS for invalid arguments, policy,
 *         empty ranges, address overflow or overlapping device ranges.
 *         OS_ENOTSUPPORTED for too many ranges. OS_EBUSY if *contextOut is not NULL.
 *         OS_EOOM on allocation failure. Failure leaves *contextOut unchanged.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextCreate(
    _In_    const struct SHMDeviceRange* ranges,
    _In_    uint32_t                     count,
    _In_    enum SHMDeviceCachePolicy    cachePolicy,
    _In_    uint64_t                     addressLimit,
    _InOut_ SHMDeviceContext_t**         contextOut);

/**
 * @brief Add one reference to a device context.
 *
 * The caller must hold an existing reference until this call returns.
 *
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL.
 *         OS_EOVERFLOW if the reference count cannot increase.
 *         Failure adds no reference.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextAcquire(
    _In_ SHMDeviceContext_t* context);

/**
 * @brief Release one context reference and set the caller's pointer to NULL.
 *
 * The context is freed after its last reference is released.
 *
 * @param context Pointer to the reference to release. NULL or *context == NULL
 *                requires no work.
 */
KERNELABI void KERNELAPI
SHMDeviceContextRelease(
    _InOut_ SHMDeviceContext_t** context);

/**
 * @brief Closes a context once all of its mappings have been released.
 *
 * The caller must prevent new operations on the context until this returns.
 * For bound contexts, a recovery pass may still hold temporary references that
 * delay final cleanup. For unbound contexts, the caller must hold the only
 * remaining reference.
 *
 * @param context Must point to a valid context. Set to NULL on success.
 * @return OS_EOK on success, or OS_EBUSY if a mapping still exists (or an
 *         unbound context still has other references). Unchanged on failure.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextClose(
    _InOut_ SHMDeviceContext_t** context);

/**
 * @brief Read the device cache policy used by SHM.
 *
 * @return The policy, or SHMDeviceCacheUnknown for NULL. Reading the policy
 *         does not synchronize CPU caches or make writes visible to the device.
 */
KERNELABI enum SHMDeviceCachePolicy KERNELAPI
SHMDeviceContextGetCachePolicy(
    _In_ const SHMDeviceContext_t* context);

/**
 * @brief Find a device address for an entire physical buffer.
 *
 * This lookup uses the fixed address windows of a direct-DMA context (one with
 * fixed bus offsets, without an IOMMU page table). It computes the address
 * without reserving space or touching hardware. Address spaces that need
 * hardware page-table translation require a mapping object.
 *
 * The whole buffer must fit inside a single range. Use
 * SHMDeviceContextBuildSegments if it crosses range boundaries. When multiple
 * device addresses reach the same physical bytes, this picks the lowest address
 * that covers the whole buffer within AddressLimit. Input range order makes no
 * difference.
 *
 * Checking that the memory exists and that the device has permission to use it
 * is up to the caller. Getting an address here grants no permission to run DMA.
 *
 * @param context Context. Hold a reference until the call returns.
 * @param physicalBase Physical address of the buffer's first byte.
 * @param length Nonzero buffer length in bytes.
 * @param deviceAddressOut Receives the device address only on success.
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL, zero length or physical overflow.
 *         OS_ENOENT if no single range covers the buffer within the limit.
 *         Failure leaves the output unchanged.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextTranslate(
    _In_  const SHMDeviceContext_t* context,
    _In_  uint64_t                  physicalBase,
    _In_  uint64_t                  length,
    _Out_ uint64_t*                 deviceAddressOut);

/**
 * @brief Convert a buffer's physical byte ranges into device segments.
 *
 * Like SHMDeviceContextTranslate, this only computes addresses for fixed
 * direct-DMA contexts and never allocates a mapping or programs hardware.
 *
 * Physical ranges are split at device address-range boundaries, and pieces that
 * land at consecutive device addresses are joined back together. When multiple
 * ranges reach the same bytes, the one covering the most bytes wins (ties go
 * to the lowest device address).
 *
 * First call with segmentsOut NULL to get the count, then pass an array of that
 * size. Keep the inputs unchanged between counting and filling. Make sure the
 * memory is already allocated, belongs to the buffer, and stays allocated while
 * in use.
 *
 * @param context Context. Hold a reference until the call returns.
 * @param extents Allocated physical byte ranges in buffer order. Each length
 *                must be nonzero, and each last address must not overflow.
 *                Physical address zero is valid on some boards, so it cannot
 *                be used here to detect a missing page.
 * @param extentCount Number of entries in extents, at least one.
 * @param segmentCount On input, capacity of segmentsOut, ignored if it is NULL.
 *                     On output, number of segments needed or written.
 * @param segmentsOut NULL to count only, otherwise receives the segments.
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL arguments, no extents,
 *         or an empty or overflowing range. OS_ENOENT if a byte has no device
 *         address within the context's address limit. OS_EBUFFER if segmentsOut
 *         is too small, with the needed count stored in segmentCount.
 *         OS_ENOTSUPPORTED if the count does not fit in uint32_t.
 *         segmentsOut is unchanged on failure.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextBuildSegments(
    _In_    const SHMDeviceContext_t* context,
    _In_    const SHMSG_t*            extents,
    _In_    int                       extentCount,
    _InOut_ uint32_t*                 segmentCount,
    _Out_   struct DMASegment*        segmentsOut);

/**
 * @brief Prepare an SHM buffer range for device access.
 *
 * Creating a mapping keeps the buffer and context alive so the buffer's pages
 * stay allocated even if every process unmaps the buffer from its own space.
 * Setup leaves buffer contents and cache settings alone without touching CPU
 * caches or starting a transfer. Only kernel callers that have already checked
 * device permission should call this.
 *
 * Only SHM-managed buffers are allowed, and every page inside the range must
 * already be allocated (for instance, with SHM_COMMIT) since this call never
 * allocates pages on demand.
 *
 * StreamingBounce lets the kernel allocate zeroed temporary pages (bounce
 * pages) and point the device at those instead of the original buffer. Sync
 * calls then copy bytes back and forth. Use bounce pages when the device cannot
 * reach the buffer's physical addresses or satisfy segment alignment rules. You
 * also need them when a non-coherent device writes to a range that starts or
 * ends partway through a CPU cache line, because invalidating that cache line
 * could wipe out CPU changes to neighboring bytes. Uncached device-pool buffers
 * skip that cache-line rule, whereas normal SHM buffers are cached.
 *
 * StreamingDirect, Persistent, and Shared work directly on the original pages.
 * On a streaming mapping, bracket each transfer with SHMDeviceSyncForDevice
 * before and SHMDeviceSyncForCpu after. On a persistent mapping, bracket each
 * job with SHMDeviceBeginUse and SHMDeviceEndUse, which sync caches across the
 * full mapped range on the first begin and the final end.
 *
 * Conflict checks expand outward to whole cache lines. Multiple readers can
 * share a range, while any overlapping write conflicts. If you submit multiple
 * jobs through the same mapping, make sure their order is safe on the device
 * and never run overlapping calls on one mapping.
 *
 * For CPU access, open separate CPU sessions with SHMAccessCreate and wrap
 * CPU reads and writes in SHMAccessBegin and SHMAccessEnd with CPU memory
 * barriers. Shared mappings only accept pool pages that are uncached or paired
 * with a cache-coherent device (SHMDeviceAllocate sets these up for you).
 *
 * @param operation Operation token for this context (required for bound contexts,
 *                  or pass NULL for an unbound context with no overlapping calls).
 * @param context Context. Hold a reference until the call returns.
 * @param shmID Buffer to use.
 * @param offset First byte of the range, counted from the start of the buffer.
 * @param length Nonzero number of bytes. The range must fit in the buffer.
 * @param direction Which way data will move for this mapping.
 * @param kind How the mapping uses buffer pages and handles CPU caches.
 * @param requirements Optional constraints on each completed device segment.
 *                     Zero fields add no constraint. Nonzero values must be
 *                     powers of two. The mapped length must be a multiple of
 *                     LengthAlignment and is never padded. Only streaming
 *                     bounce mappings can use a copy to meet these constraints.
 * @param mappingOut Address of an initially NULL pointer. Receives a mapping
 *                   for the caller to release.
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL arguments, a bad range,
 *         an unknown direction or kind, invalid requirements, or a missing or
 *         wrong operation token.
 *         OS_EBUSY if *mappingOut is not NULL. OS_ENOENT for an unknown buffer or
 *         bytes with no device address, even through bounce pages.
 *         OS_ENOTSUPPORTED for unsafe direct cache-line sharing, unmet alignment
 *         or incompatible shared storage. OS_EINCOMPLETE if a page in the range
 *         was never allocated. OS_EOVERFLOW if the context cannot take another
 *         reference. OS_EOOM on allocation failure.
 *         OS_ECANCELLED if the binding closes while creating the mapping. In that
 *         case, the context holds on to the new mapping and its pages until
 *         hardware stop cleanup runs, and *mappingOut stays NULL. Other failures
 *         free everything created so far.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceMap(
    _In_Opt_ SHMDeviceOperation_t*            operation,
    _In_     SHMDeviceContext_t*              context,
    _In_     uuid_t                           shmID,
    _In_     size_t                           offset,
    _In_     size_t                           length,
    _In_     enum DMADirection                direction,
    _In_     enum SHMDeviceMappingKind        kind,
    _In_     const struct DMAMapRequirements* requirements,
    _InOut_  SHMDeviceMapping_t**             mappingOut);

/**
 * @brief Add one persistent use before submitting work to the device.
 *
 * Calls on this mapping must not overlap. Keep the CPU from reading or writing
 * the mapped range until every device use has ended.
 *
 * Wait behavior follows SHMWaitMode. Returning OS_EFORKED means a kernel worker
 * thread took over the wait. Keep the mapping and operation token alive until
 * that worker thread finishes.
 *
 * @param operation Mapping Submit token, or NULL for non-overlapping unbound calls.
 * @param mapping   Persistent mapping to use.
 * @param wait      Wait options, or NULL to fail immediately if busy.
 * @return OS_EOK or OS_EFORKED. OS_EINVALPARAMS for NULL, a different mapping
 *         kind, or an invalid operation token. OS_EOVERFLOW if the use count
 *         cannot increase.
 *         OS_EBUSY if existing uses must finish before a conflicting queued
 *         request can run, or if cleanup has started.
 *         OS_ECANCELLED if the binding was cancelled. The range stays locked down
 *         until hardware stop cleanup completes.
 *         Errors from SHMAccessBegin and SHMAccessExtend pass through unchanged.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceBeginUse(
    _In_Opt_ SHMDeviceOperation_t*       operation,
    _In_     SHMDeviceMapping_t*         mapping,
    _In_     const struct SHMAccessWait* wait);

/**
 * @brief End one persistent use after the device work has finished.
 *
 * Call only after the transfer completes, hardware stop is confirmed, or the
 * job was cancelled before submission.
 *
 * @param operation Mapping Complete token, or NULL for non-overlapping unbound calls.
 * @param mapping   Persistent mapping with an active use. Calls must not overlap.
 *                  CPU access stays blocked until the final use ends.
 * @return OS_EOK, OS_EINVALPARAMS for a different kind, no active uses, or an
 *         invalid token, or OS_ECANCELLED if stop cleanup already ran.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceEndUse(
    _In_Opt_ SHMDeviceOperation_t* operation,
    _In_     SHMDeviceMapping_t*   mapping);

/**
 * @brief Allocate memory for continuous CPU and device access.
 *
 * Use this for long-lived device structures, such as command rings. The new
 * SHM buffer is zeroed and uses the device pool's cache policy (uncached on
 * ARM64). Neither side needs sync calls before reading what the other wrote.
 * The mapping allows reads and writes in both directions. Both sides share the
 * same pages because a copy cannot keep up with concurrent writes from both.
 * Device segments cover the allocation rounded up to whole pages.
 *
 * Pages come from the reserved device pool and are consecutive in physical
 * memory. The pool checks physical alignment and boundary requirements, and
 * the resulting device segments are checked too. Allocation fails if the pool
 * is absent or full, or if the device addresses cannot meet the requirements.
 * Ordinary memory is never used as a fallback.
 *
 * The buffer is created in, and mapped into, the calling process. Release with
 * SHMDeviceUnmap on the mapping and SHMDetach on the buffer, in that order.
 *
 * @param operation Context Submit token, kept alive through result copy or
 *                  cleanup on failure. NULL is allowed for non-overlapping unbound calls.
 * @param context Context. Hold a reference until the call returns.
 * @param requirements Memory constraints. See DMARequirements.
 * @param bufferOut Receives the buffer handle, including the CPU pointer.
 * @param mappingOut Address of an initially NULL pointer. Receives a mapping
 *                   for the caller to release.
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL arguments or requirements
 *         no memory could meet, or an invalid token. OS_EBUSY if *mappingOut is
 *         not NULL. OS_ENOENT if the device cannot reach the memory.
 *         OS_ENOTSUPPORTED if the memory did not meet the requirements.
 *         Errors from creating the buffer pass through.
 *         Failure leaves both outputs unchanged. If cleanup also fails, the
 *         context queues the mapping for another cleanup attempt and keeps its
 *         pages allocated.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceAllocate(
    _In_Opt_ SHMDeviceOperation_t*         operation,
    _In_     SHMDeviceContext_t*           context,
    _In_     const struct DMARequirements* requirements,
    _Out_    SHMHandle_t*                  bufferOut,
    _InOut_  SHMDeviceMapping_t**          mappingOut);

/**
 * @brief Prepare a streaming mapping for a device transfer.
 *
 * Reserves the range for the device and makes earlier CPU writes visible to it.
 * If the mapping uses bounce pages and the device will read data, copies the
 * range into those bounce pages first. For a non-coherent device, flushes CPU
 * cache lines back to RAM (and invalidates them before device writes so old
 * cached bytes cannot later overwrite what the device wrote). Uncached pool
 * memory skips those cache calls, though it still runs memory barriers, access
 * checks, and any bounce copies.
 *
 * The mapping must be idle and the range nonempty. Calls on one mapping must
 * not overlap. End conflicting CPU sessions before calling. Keep the mapping
 * valid until SHMDeviceSyncForCpu succeeds, then begin a CPU session before
 * touching the data.
 *
 * Wait behavior follows SHMWaitMode. Returning OS_EFORKED means a kernel worker
 * thread took over the wait. Keep the mapping and operation token alive until
 * that worker thread finishes.
 *
 * @param operation Mapping Submit token, or NULL for non-overlapping unbound calls.
 * @param mapping   Streaming mapping to submit.
 * @param offset    First byte the device will use, counted from the mapping.
 * @param length    Nonzero number of bytes. Must lie inside the mapping.
 * @param wait      Wait options, or NULL to fail immediately if busy.
 * @return OS_EOK or OS_EFORKED. OS_EINVALPARAMS for NULL, a non-streaming mapping,
 *         an invalid range, or an invalid operation token. OS_EBUSY if the
 *         mapping is already active or cleanup has started. OS_ECANCELLED if
 *         the binding was cancelled, leaving the range locked down until stop
 *         cleanup. Errors from SHMAccessBegin pass through unchanged.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceSyncForDevice(
    _In_Opt_ SHMDeviceOperation_t*       operation,
    _In_     SHMDeviceMapping_t*         mapping,
    _In_     size_t                      offset,
    _In_     size_t                      length,
    _In_     const struct SHMAccessWait* wait);

/**
 * @brief Make completed device writes visible to the CPU and end device access.
 *
 * Confirm that the device actually finished (via a completion interrupt/event
 * or by stopping the hardware) before calling this. On a non-coherent device
 * write, discards cached CPU lines for the completed range. If bounce pages
 * were used, copies the completed bytes back to the buffer and leaves bytes
 * outside that range untouched. Device access then ends. Begin a CPU session
 * before reading or writing the buffer, and treat only the completed range as
 * device output.
 *
 * @param operation Mapping Complete token, or NULL for non-overlapping unbound calls.
 * @param mapping   Streaming mapping. Shared and persistent mappings are rejected.
 *                  Calls on one mapping must not overlap.
 * @param length    Number of completed bytes from the start of the submitted
 *                  range, at most the submitted length. Passing 0 cancels the
 *                  transfer and restores CPU access.
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL, a non-streaming mapping,
 *         a bad range, an invalid token, or a mapping without an active transfer.
 *         OS_ECANCELLED if stop cleanup already ran.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceSyncForCpu(
    _In_Opt_ SHMDeviceOperation_t* operation,
    _In_     SHMDeviceMapping_t*   mapping,
    _In_     size_t                length);

/**
 * @brief Release a mapping and its buffer and context references.
 *
 * Finish or stop all device work first. For a streaming mapping, call
 * SHMDeviceSyncForCpu before unmapping. For a persistent mapping, end every use
 * with SHMDeviceEndUse. No device access may still be queued. This call never
 * stops hardware on its own.
 *
 * @param operation Mapping Release token, or the context Submit token when
 *                  cleaning up a failed mapping creation. NULL is allowed for
 *                  non-overlapping unbound calls. End the token after this call.
 * @param mapping   Pointer set to NULL on success. NULL or *mapping == NULL
 *                  does nothing. Calls on the mapping must not overlap.
 * @return OS_EOK, OS_EINVALPARAMS for an invalid token, or OS_EBUSY while
 *         device work is active. If freeing device addresses fails, the pointer
 *         stays unchanged and pages stay allocated so cleanup can retry, while
 *         the mapping can no longer be used for transfers or segment queries.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceUnmap(
    _In_Opt_ SHMDeviceOperation_t* operation,
    _InOut_  SHMDeviceMapping_t**  mapping);

/**
 * @brief Read the device segments covering the mapped range, in order.
 *
 * @param operation Mapping Submit token, or the context Submit token while
 *                  creating a mapping. Keep it valid while reading the returned
 *                  segments. NULL is allowed for non-overlapping unbound calls.
 * @param mapping   Valid mapping. Release must not overlap this call.
 * @param countOut  Receives the number of segments.
 * @return The segment list, valid until release starts, or NULL for NULL
 *         arguments. An invalid token or started release returns NULL and sets
 *         *countOut to 0. On bound contexts, backing memory stays pinned until
 *         the token ends.
 */
KERNELABI KERNELAPI const struct DMASegment*
SHMDeviceMappingSegments(
    _In_Opt_ SHMDeviceOperation_t*     operation,
    _In_     const SHMDeviceMapping_t* mapping,
    _Out_    uint32_t*                 countOut);

/**
 * @brief Cleans up a mapping after the process holding its handle exits.
 *
 * For a bound mapping, this shuts down the entire recovery group. If the
 * mapping still holds device resources, it stays on the context list so stop
 * cleanup can free them later. Idle unbound mappings are freed right away,
 * while active unbound mappings stay listed until hardware stop is confirmed.
 *
 * @param mapping Must point to a mapping pointer. Set to NULL once freed or
 *                handed over to context stop cleanup.
 * @return OS_EOK once freed or handed over to stop cleanup.
 */
oserr_t
SHMDeviceAbandon(
    _InOut_ SHMDeviceMapping_t** mapping);

/**
 * @brief Cancels waiting requests and locks down ranges on all mappings in a context.
 *
 * Call this after the recovery group has already blocked new DMA work. Any
 * mapping still being built will lock down its own range when it links into
 * the list.
 *
 * @param context Context whose mapping list stays valid during the walk.
 * @return OS_EOK if every mapping was visited. OS_EBUSY or OS_EOVERFLOW leaves
 *         some work unblocked and means another cancellation pass is needed.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextRevoke(
    _In_ SHMDeviceContext_t* context);

/**
 * @brief Releases mapping resources after hardware has physically stopped.
 *
 * Call this only while holding a context reference and after confirming the
 * device can no longer touch RAM (with no background wait able to wake up and
 * resume). Unbound contexts clean only abandoned mappings. If freeing device
 * addresses fails, the mapping's pages and range lock stay intact so cleanup
 * can retry.
 *
 * @param context Context whose device has stopped accessing memory.
 * @return OS_EOK, or the first reference, busy, or address-release error.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextCompleteStop(
    _In_ SHMDeviceContext_t* context);

/**
 * @brief Prepares a bound device context from a driver's binding handle and generation.
 *
 * Sets up the context struct before attaching it to the binding, then builds
 * its fixed direct-DMA address rules outside the group lock. Leaves *contextOut
 * unchanged on error.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextPrepareBound(
    _In_  uuid_t                            binding,
    _In_  const OSDmaBoundOpenParameters_t* parameters,
    _Out_ SHMDeviceContext_t**              contextOut);

/**
 * @brief Finishes publishing a bound context after copying its handle to user space.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextPublishBound(
    _In_ SHMDeviceContext_t* context);

/**
 * @brief Cancels an unpublished bound context with no mappings and clears *context.
 *
 * Drops the setup operation count once the final temporary reference is released.
 */
KERNELABI void KERNELAPI
SHMDeviceContextCancelBound(
    _InOut_ SHMDeviceContext_t** context);

/**
 * @brief Cleans up a device context after all threads in its process have exited.
 *
 * If any mappings still exist on a bound context, shuts down the whole
 * recovery group before dropping the context reference. Temporary references
 * held by a recovery pass use SHMDeviceContextRelease.
 */
KERNELABI void KERNELAPI
SHMDeviceContextAbandon(
    _InOut_ SHMDeviceContext_t** context);

/**
 * @brief Starts an operation using storage embedded inside the context struct.
 *
 * The record must be in the Updating state with an empty PendingOperation slot.
 * Because the token lives inside the context struct, this never allocates memory.
 * Keep the token alive across setup, user-space copies, and failure cleanup,
 * then end it with SHMDeviceOperationEnd. If a wait returns OS_EFORKED, the
 * kernel worker thread keeps the token and ends it when finished.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceContextBeginOperation(
    _In_  SHMRecord_t*           record,
    _In_  SHMDeviceContext_t*         context,
    _In_  enum SHMDeviceOperationKind kind,
    _Out_ SHMDeviceOperation_t**      operationOut);

/**
 * @brief Starts an operation using storage embedded inside the mapping struct.
 *
 * Follows the same record and token rules as SHMDeviceContextBeginOperation.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceMappingBeginOperation(
    _In_  SHMRecord_t*           record,
    _In_  SHMDeviceMapping_t*         mapping,
    _In_  enum SHMDeviceOperationKind kind,
    _Out_ SHMDeviceOperation_t**      operationOut);

/**
 * @brief Checks that a bound context's binding is still live before committing work.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceOperationCheck(
    _In_ const SHMDeviceOperation_t* operation);

/**
 * @brief Clears the token and record slot, lowers the group count, and drops references.
 */
KERNELABI void KERNELAPI
SHMDeviceOperationEnd(
    _InOut_ SHMDeviceOperation_t** operation);

/**
 * @brief Retires a blocked range wait when its owning process exits.
 *
 * Call this only after every thread in the process (including any background
 * worker wait) has stopped. If the operation was interrupted outside a range
 * wait (for example, while copying or flushing caches), returns OS_EBUSY and
 * keeps its count and pages pinned because we cannot tell what state it left.
 */
KERNELABI oserr_t KERNELAPI
SHMDeviceOperationCancel(
    _In_ SHMRecord_t* record);

#endif //!__SHM_DEVICE_H__
