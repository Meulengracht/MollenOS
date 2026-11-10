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
#include <os/types/async.h>
#include <os/types/time.h>
#include <shm/access.h>

// Accept all 64 ranges returned by deviced. Firmware parsing stays in deviced.
#define SHM_DEVICE_MAX_RANGES DMA_MAX_RANGES

// Opaque types
struct SHMDeviceContext;
struct SHMDeviceMapping;

/** How a mapping uses buffer pages and synchronizes CPU caches. */
enum SHMDeviceMappingKind {
    // Each transfer may use temporary bounce pages instead of the original pages.
    SHMDeviceMappingStreamingBounce,
    // Streaming transfers must use the original SHM pages.
    SHMDeviceMappingStreamingDirect,
    // Original pages with a use count. Cache sync covers the whole mapped range.
    SHMDeviceMappingPersistent,
    // Pool pages shared by the CPU and device at the same time.
    SHMDeviceMappingShared
};

/**
 * @brief Create a device context with a copy of the physical-to-device address ranges.
 *
 * Pass kernel pointers only. This call does not register or start the device,
 * reserve buffer pages, or change cache settings. The DDK DMA session must
 * hold its deviced lease until every mapping and the kernel context have been
 * released.
 *
 * @param ranges Physical-to-device ranges in any order. Keep them unchanged
 *               until the call returns.
 * @param count Number of ranges. Must be 1 through SHM_DEVICE_MAX_RANGES.
 * @param cachePolicy Known cache policy for the device and its bus path.
 * @param addressLimit Largest device address allowed, inclusive. Stored in the
 *                     context for all its mappings. UINT64_MAX means no limit.
 *                     Use UINT32_MAX for a device limited to 32-bit addresses.
 * @param contextOut Address of an initially NULL pointer. Receives a context
 *                   with one reference for the caller to release.
 * @return OS_EOK on success. OS_EINVALPARAMS for invalid arguments, policy,
 *         empty ranges, address overflow or overlapping device ranges.
 *         OS_ENOTSUPPORTED for too many ranges. OS_EBUSY if *contextOut is not NULL.
 *         OS_EOOM on allocation failure. Failure leaves *contextOut unchanged.
 */
oserr_t
SHMDeviceContextCreate(
    _In_    const struct SHMDeviceRange* ranges,
    _In_    uint32_t                     count,
    _In_    enum SHMDeviceCachePolicy    cachePolicy,
    _In_    uint64_t                     addressLimit,
    _InOut_ struct SHMDeviceContext**    contextOut);

/**
 * @brief Add one reference to a device context.
 *
 * The caller must hold an existing reference until this call returns.
 *
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL.
 *         OS_EOVERFLOW if the reference count cannot increase.
 *         Failure adds no reference.
 */
oserr_t
SHMDeviceContextAcquire(
    _In_ struct SHMDeviceContext* context);

/**
 * @brief Release one context reference and set the caller's pointer to NULL.
 *
 * The context is freed after its last reference is released.
 *
 * @param context Pointer to the reference to release. NULL or *context == NULL
 *                requires no work.
 */
void
SHMDeviceContextRelease(
    _InOut_ struct SHMDeviceContext** context);

/**
 * @brief Close a context only when no mappings or other references remain.
 *
 * The caller must prevent new mappings and references until the call returns.
 *
 * @param context Must point to a valid context. Set to NULL on success.
 * @return OS_EOK on success. OS_EBUSY if other references remain, leaving
 *         *context unchanged.
 */
oserr_t
SHMDeviceContextClose(
    _InOut_ struct SHMDeviceContext** context);

/**
 * @brief Read the device cache policy used by SHM.
 *
 * @return The policy, or SHMDeviceCacheUnknown for NULL. Reading the policy
 *         does not synchronize CPU caches or make writes visible to the device.
 */
enum SHMDeviceCachePolicy
SHMDeviceContextGetCachePolicy(
    _In_ const struct SHMDeviceContext* context);

/**
 * @brief Find a device address for an entire physical buffer.
 *
 * This query uses the fixed ranges of a direct-DMA context. It calculates an
 * address without reserving device addresses or changing hardware mappings.
 * Address spaces that require hardware translation need a mapping object.
 *
 * The whole buffer must fit in one range. Use SHMDeviceContextBuildSegments
 * when it crosses range boundaries. If several device addresses reach the
 * same physical bytes, choose the lowest address that covers the whole buffer
 * within the context's limit. Range order does not affect the result.
 *
 * The caller must separately check that the memory exists and that the device
 * is allowed to use it. A calculated address grants no permission to transfer.
 *
 * @param context Context. Hold a reference until the call returns.
 * @param physicalBase Physical address of the buffer's first byte.
 * @param length Nonzero buffer length in bytes.
 * @param deviceAddressOut Receives the device address only on success.
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL, zero length or physical overflow.
 *         OS_ENOENT if no single range covers the buffer within the limit.
 *         Failure leaves the output unchanged.
 */
oserr_t
SHMDeviceContextTranslate(
    _In_  const struct SHMDeviceContext* context,
    _In_  uint64_t                       physicalBase,
    _In_  uint64_t                       length,
    _Out_ uint64_t*                      deviceAddressOut);

/**
 * @brief Convert a buffer's physical byte ranges into device segments.
 *
 * This query only calculates addresses for direct-DMA contexts. It does not
 * create a mapping or program hardware.
 *
 * Split the physical ranges at device address-range boundaries. Join pieces
 * with consecutive device addresses. If several ranges reach the same bytes,
 * choose the one that covers the most bytes. For equal lengths, choose the
 * lowest device address, as in SHMDeviceContextTranslate.
 *
 * First call with segmentsOut NULL to get the count, then supply an array of
 * that size. Keep the inputs unchanged while counting and filling. The caller
 * must check that the memory is allocated, belongs to the buffer and stays
 * allocated while in use.
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
oserr_t
SHMDeviceContextBuildSegments(
    _In_    const struct SHMDeviceContext* context,
    _In_    const SHMSG_t*                 extents,
    _In_    int                            extentCount,
    _InOut_ uint32_t*                      segmentCount,
    _Out_   struct DMASegment*             segmentsOut);

/**
 * @brief Prepare an SHM buffer range for device access.
 *
 * The mapping keeps the buffer and context alive. Buffer pages stay allocated
 * even if every process detaches the buffer. Creation leaves buffer contents
 * and cache settings unchanged. It synchronizes no caches and starts no transfer.
 * The caller must be kernel code that has already checked permission for the
 * device to use the buffer.
 *
 * Only buffers whose pages are managed by SHM are accepted. Every page in the
 * range must already be allocated (for example with SHM_COMMIT). This call
 * never allocates buffer pages.
 *
 * StreamingBounce allows zeroed temporary pages, called bounce pages, in place
 * of the original buffer pages. Sync calls copy data between them. Use them
 * when the device cannot address the buffer or meet its segment requirements.
 * They are also needed for non-coherent device writes when the range starts
 * or ends inside a CPU cache line, because invalidating that line could discard
 * CPU changes to nearby bytes. The device segments then describe the bounce
 * pages. Uncached pool buffers need no such cache-line separation.
 * Ordinary SHM is cached.
 *
 * StreamingDirect, Persistent and Shared use the original pages. For a
 * streaming mapping, call SHMDeviceSyncForDevice before each transfer and
 * SHMDeviceSyncForCpu after it completes. For a persistent mapping, pair
 * SHMDeviceBeginUse with SHMDeviceEndUse. The first begin and final end
 * synchronize caches for the whole mapped range.
 *
 * Range conflict checks include whole cache lines. Readers may share a range,
 * but overlapping writes conflict. The caller must still order conflicting
 * jobs submitted through the same mapping. Calls on one mapping must not overlap.
 *
 * Create separate CPU sessions with SHMAccessCreate. Surround CPU buffer access
 * with SHMAccessBegin and SHMAccessEnd, using CPU memory barriers to keep the
 * accesses in order. Shared mappings require pool pages that are uncached or
 * used by a coherent device. SHMDeviceAllocate creates these mappings with new
 * pool pages.
 *
 * @param context Context. Hold a reference until the call returns.
 * @param shmID Buffer to use.
 * @param offset First byte of the range, counted from the start of the buffer.
 * @param length Nonzero number of bytes. The range must fit in the buffer.
 * @param direction Which way data will move for this mapping.
 * @param kind How the mapping uses buffer pages and synchronizes caches.
 * @param requirements Optional constraints on each completed device segment.
 *                     Zero fields add no constraint. Nonzero values must be
 *                     powers of two. The mapped length must be a multiple of
 *                     LengthAlignment and is never padded. Only streaming
 *                     bounce mappings may use a copy to meet these constraints.
 * @param mappingOut Address of an initially NULL pointer. Receives a mapping
 *                   for the caller to release.
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL arguments, a bad range,
 *         an unknown direction or kind, or invalid requirements.
 *         OS_EBUSY if *mappingOut is not NULL. OS_ENOENT for an unknown buffer or
 *         bytes with no device address, even through bounce pages.
 *         OS_ENOTSUPPORTED for unsafe direct cache-line sharing, unmet alignment
 *         or incompatible shared storage. OS_EINCOMPLETE if a page in the range
 *         was never allocated. OS_EOVERFLOW if the context cannot take another
 *         reference. OS_EOOM on allocation failure.
 *         Failure leaves *mappingOut unchanged and keeps no new references.
 */
oserr_t
SHMDeviceMap(
    _In_    struct SHMDeviceContext*         context,
    _In_    uuid_t                           shmID,
    _In_    size_t                           offset,
    _In_    size_t                           length,
    _In_    enum DMADirection                direction,
    _In_    enum SHMDeviceMappingKind        kind,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct SHMDeviceMapping**        mappingOut);

/**
 * @brief Add one persistent use before submitting work to the device.
 *
 * Calls on this mapping must not overlap. Keep the CPU from using the mapped
 * range until every device use has ended.
 *
 * Wait behavior follows SHMWaitMode. OS_EFORKED means the call is still running
 * asynchronously. Keep the mapping valid until that call finishes.
 *
 * @param mapping Persistent mapping to use.
 * @param wait Wait options, or NULL for nonblocking access.
 * @return OS_EOK or OS_EFORKED. OS_EINVALPARAMS for NULL or a different mapping
 *         kind. OS_EOVERFLOW if the use count cannot increase.
 *         OS_EBUSY if existing uses must finish before a conflicting queued
 *         request can run, or if address release has started.
 *         Errors from SHMAccessBegin and SHMAccessExtend pass through unchanged.
 */
oserr_t
SHMDeviceBeginUse(
    _In_ struct SHMDeviceMapping*    mapping,
    _In_ const struct SHMAccessWait* wait);

/**
 * @brief End one persistent use after the device work has finished.
 *
 * Call only after completion, a confirmed stop, or cancellation before submission.
 *
 * @param mapping Persistent mapping with an active use. Calls must not overlap.
 *                CPU access stays blocked until the final use ends.
 * @return OS_EOK or OS_EINVALPARAMS for a different kind or no active uses.
 */
oserr_t
SHMDeviceEndUse(
    _In_ struct SHMDeviceMapping* mapping);

/**
 * @brief Allocate memory for continuous CPU and device access.
 *
 * Use this for long-lived device structures, such as command rings. The new
 * SHM buffer is zeroed and uses the device pool's cache policy (uncached on
 * ARM64). Neither side needs streaming sync calls to see the other's writes.
 * The mapping allows reads and writes in both directions. Both sides use the
 * same pages because a copy cannot track their concurrent writes.
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
 * @param context Context. Hold a reference until the call returns.
 * @param requirements Memory constraints. See DMARequirements.
 * @param bufferOut Receives the buffer handle, including the CPU pointer.
 * @param mappingOut Address of an initially NULL pointer. Receives a mapping
 *                   for the caller to release.
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL arguments or requirements
 *         no memory could meet. OS_EBUSY if *mappingOut is not NULL. OS_ENOENT if the
 *         device cannot reach the memory. OS_ENOTSUPPORTED if the memory did
 *         not meet the requirements. Errors from creating the buffer pass through.
 *         Failure leaves both outputs unchanged. If cleanup also fails, the
 *         context queues the mapping for another cleanup attempt and keeps its
 *         pages allocated.
 */
oserr_t
SHMDeviceAllocate(
    _In_    struct SHMDeviceContext*      context,
    _In_    const struct DMARequirements* requirements,
    _Out_   SHMHandle_t*                  bufferOut,
    _InOut_ struct SHMDeviceMapping**     mappingOut);

/**
 * @brief Prepare a streaming mapping for a device transfer.
 *
 * Reserve the range for device use and make earlier CPU writes visible to it.
 * If the device reads through bounce pages, copy the range into those pages
 * first. For a non-coherent device, write cached CPU data back to memory.
 * Device writes also need the old cache entries invalidated. Uncached pool
 * memory skips those cache operations, but still needs memory barriers, access
 * checks and any required bounce copies.
 *
 * The mapping must be idle and the range nonempty. Calls on one mapping must
 * not overlap. End conflicting CPU sessions before calling. Keep the mapping
 * valid until SHMDeviceSyncForCpu succeeds, then begin a CPU session before
 * accessing the data.
 *
 * Wait behavior follows SHMWaitMode. OS_EFORKED means the call is still running
 * asynchronously. Keep the mapping valid until that call finishes.
 *
 * @param mapping Streaming mapping to submit.
 * @param offset First byte the device will use, counted from the mapping.
 * @param length Nonzero number of bytes. Must lie inside the mapping.
 * @param wait Wait options, or NULL for nonblocking access.
 * @return OS_EOK or OS_EFORKED. OS_EINVALPARAMS for NULL, a non-streaming mapping
 *         or an invalid range. OS_EBUSY if the mapping is already active or
 *         address release has started. Errors from SHMAccessBegin pass through.
 */
oserr_t
SHMDeviceSyncForDevice(
    _In_ struct SHMDeviceMapping*    mapping,
    _In_ size_t                      offset,
    _In_ size_t                      length,
    _In_ const struct SHMAccessWait* wait);

/**
 * @brief Make completed device writes visible to the CPU and end device access.
 *
 * The driver must confirm that the device has finished, for example through a
 * completion event or by stopping the device, before making this call.
 * If a non-coherent device wrote data, discard old CPU cache entries for the
 * completed range. Copy completed writes from bounce pages back to the buffer,
 * leaving bytes outside that range untouched. Device access then ends.
 * Begin a CPU session before reading or writing the data. Treat only the
 * completed range as device output.
 *
 * @param mapping Streaming mapping. Shared and persistent mappings are rejected.
 *                Calls on one mapping must not overlap.
 * @param length Number of completed bytes from the start of the submitted
 *               range, at most the submitted length. Zero permits cancellation.
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL, a non-streaming mapping,
 *         a bad range or a mapping without an active device transfer.
 */
oserr_t
SHMDeviceSyncForCpu(
    _In_ struct SHMDeviceMapping* mapping,
    _In_ size_t                   length);

/**
 * @brief Release a mapping and its buffer and context references.
 *
 * Finish or stop all device work first. For a stream, call SHMDeviceSyncForCpu
 * before releasing. For a persistent mapping, call SHMDeviceEndUse for every
 * use. No device access may still be queued. This function does not stop hardware.
 *
 * @param mapping Pointer set to NULL on success. NULL or *mapping == NULL
 *                requires no work. Calls on the mapping must not overlap.
 * @return OS_EOK, or OS_EBUSY while device work is active. Address release errors
 *         leave the pointer unchanged and the pages allocated for another
 *         release attempt. Once address release starts, the mapping cannot be
 *         used for transfers or segment queries, even if release fails.
 */
oserr_t
SHMDeviceUnmap(
    _InOut_ struct SHMDeviceMapping** mapping);

/**
 * @brief Read the device segments covering the mapped range, in order.
 *
 * @param mapping Valid mapping. Release must not overlap this call.
 * @param countOut Receives the number of segments.
 * @return The segment list, valid until release starts, or NULL for NULL
 *         arguments. After release starts, returns NULL and sets *countOut to zero.
 */
const struct DMASegment*
SHMDeviceMappingSegments(
    _In_  const struct SHMDeviceMapping* mapping,
    _Out_ uint32_t*                      countOut);

/**
 * @brief Clean up a mapping after the last process thread has exited.
 *
 * Release idle mappings immediately. If the device may still use the pages,
 * add the mapping to the context's quarantine list and keep the pages allocated.
 * This list holds mappings awaiting a confirmed device stop or another cleanup
 * attempt after a failed release.
 *
 * @param mapping Must point to a mapping pointer. Set to NULL after release
 *                or after adding the mapping to the quarantine list.
 * @return OS_EOK after release or transfer to the quarantine list.
 */
oserr_t
SHMDeviceAbandon(
    _InOut_ struct SHMDeviceMapping** mapping);

/**
 * @brief Free quarantined mappings after a confirmed device stop or reset.
 *
 * Stop all device access through this context before calling. Calls that add
 * mappings to the quarantine list must not overlap this call. Discard incomplete
 * bounce data and finish cache synchronization before allowing conflicting
 * buffer access again.
 *
 * @param context Context. Hold a reference until the call returns.
 * @return OS_EOK if cleanup completed, otherwise the first address release error.
 *         Failed mappings stay on the context's list with their pages allocated
 *         for another release attempt. Cache synchronization and ending device
 *         access are not repeated on a retry.
 */
oserr_t
SHMDeviceReleaseQuarantine(
    _In_ struct SHMDeviceContext* context);

#endif //!__SHM_DEVICE_H__
