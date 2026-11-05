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
#include <os/types/dma_config.h>
#include <shm/access.h>

// This accepts all 64 ranges produced by deviced without 
// making the kernel aware of firmware-specific details.
#define SHM_DEVICE_MAX_RANGES DMA_MAX_RANGES

// Opaque types
struct SHMDeviceContext;
struct SHMDeviceMapping;

/** Device backing and synchronization mode for a mapping. */
enum SHMDeviceMappingKind {
    // Streaming transfers may use bounce pages when direct access is unsafe.
    SHMDeviceMappingStreamingBounce,
    // Streaming transfers must use the original SHM pages.
    SHMDeviceMappingStreamingDirect,
    // Direct pages with counted uses and whole-range cache handoffs.
    SHMDeviceMappingPersistent,
    // Pool storage continuously shared by the CPU and device.
    SHMDeviceMappingShared
};

/**
 * @brief Copy validated address rules so SHM can keep them beyond a request.
 *
 * This call must only use kernel memory, never user pointers.
 * It does not register a device, retain a deviced lease, reserve RAM, configure
 * caches or enable hardware. The DDK DMA session retains the lease
 * until all mappings and the kernel context have been released.
 *
 * @param ranges Unsorted or sorted ranges.
 * @param count Number of ranges; must be 1 through SHM_DEVICE_MAX_RANGES.
 * @param cachePolicy Known coherency policy for the complete device path.
 * @param addressLimit Largest address the device can use, inclusive. It is a
 *                     property of the device, so it is stored here instead of
 *                     passed on every call. UINT64_MAX means no limit;
 *                     UINT32_MAX describes a device limited to 32-bit addresses.
 * @param contextOut Empty owning slot; receives one reference on success.
 * @return OS_EOK; OS_EINVALPARAMS for invalid arguments, policy, empty ranges,
 *         overflow or overlapping device blocks; OS_ENOTSUPPORTED for too many
 *         ranges; OS_EBUSY for an occupied slot; OS_EOOM on allocation failure.
 *         Failure leaves the output slot unchanged.
 */
oserr_t
SHMDeviceContextCreate(
    _In_    const struct SHMDeviceRange* ranges,
    _In_    uint32_t                     count,
    _In_    enum SHMDeviceCachePolicy    cachePolicy,
    _In_    uint64_t                     addressLimit,
    _InOut_ struct SHMDeviceContext**    contextOut);

/**
 * @brief Increases the reference count for a device context.
 *
 * @return OS_EOK,
 *         OS_EINVALPARAMS for NULL
 *         OS_EOVERFLOW if the reference fails to increase. 
 * On error it does not acquire a reference.
 */
oserr_t
SHMDeviceContextAcquire(
    _In_ struct SHMDeviceContext* context);

/**
 * @brief Releases a reference to a device context. If the last reference is 
 * released, the context is freed.
 */
void
SHMDeviceContextRelease(
    _InOut_ struct SHMDeviceContext** context);

/**
 * @brief Close a context only when no mappings or other references remain.
 * The caller must exclude creation of new mappings and references during this call.
 * @param context Valid context slot, cleared on success.
 * @return OS_EOK or OS_EBUSY. Failure leaves the slot unchanged.
 */
oserr_t
SHMDeviceContextClose(
    _InOut_ struct SHMDeviceContext** context);

/**
 * @brief Get the policy for the context SHM mappings can choose cache handling.
 *
 * @return The policy, or SHMDeviceCacheUnknown for NULL. Reading it does
 *         not perform cache maintenance or make memory visible to hardware.
 */
enum SHMDeviceCachePolicy
SHMDeviceContextGetCachePolicy(
    _In_ const struct SHMDeviceContext* context);

/**
 * @brief Find a device address for an entire physical buffer, not just its start.
 *
 * The buffer must fit inside one range. Adjacent ranges are not joined; use
 * SHMDeviceContextBuildSegments to split at their boundaries. Among aliases
 * that fit the full buffer and the context's address limit, choose the lowest
 * device address, independently of input order. No RAM ownership or
 * installed-memory check is made, so a successful translation alone never
 * authorizes a DMA transfer.
 *
 * @param context Context held by the caller throughout this call.
 * @param physicalBase First physical byte of the proposed buffer.
 * @param length Nonzero buffer length in bytes.
 * @param deviceAddressOut Receives the device address only on success.
 * @return OS_EOK; OS_EINVALPARAMS for NULL, zero length or physical overflow;
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
 * @brief Turn a buffer's physical blocks into the blocks a device must use.
 *
 * A buffer is usually spread over several physical blocks, and one physical
 * block can cross from one address range into another. This splits the buffer
 * wherever the device's view changes, and joins neighbouring pieces back
 * together when they continue each other in device addresses. For each piece,
 * the alias reaching the most bytes is used, and the lowest device address
 * breaks ties, matching SHMDeviceContextTranslate.
 *
 * Call it once with segmentsOut NULL to learn the count, then again with an
 * array of that size. Like SHMDeviceContextTranslate, this is arithmetic only:
 * it does not check that the memory is committed, owned or kept alive.
 *
 * @param context Context held by the caller throughout this call.
 * @param extents Exact physical bytes of the buffer, in order. Each must be
 *                nonzero, must not wrap and must be committed memory: physical
 *                address zero is real memory on some boards, so an uncommitted
 *                page cannot be recognised here.
 * @param extentCount Number of extents, at least one.
 * @param segmentCount In: capacity of segmentsOut (ignored when it is NULL).
 *                     Out: segments needed, or written on success.
 * @param segmentsOut NULL to count only, otherwise receives the segments.
 * @return OS_EOK; OS_EINVALPARAMS for NULL arguments, no extents, or an empty
 *         or wrapping extent; OS_ENOENT if some byte has no device address
 *         within the context's address limit; OS_EBUFFER if segmentsOut is too
 *         small, with the needed count stored; OS_ENOTSUPPORTED if the count
 *         would not fit. segmentsOut is untouched on every failure.
 */
oserr_t
SHMDeviceContextBuildSegments(
    _In_    const struct SHMDeviceContext* context,
    _In_    const SHMSG_t*                 extents,
    _In_    int                            extentCount,
    _InOut_ uint32_t*                      segmentCount,
    _Out_   struct DMASegment*             segmentsOut);

/**
 * @brief Prepare part of an SHM buffer for a device and keep it alive.
 *
 * The mapping holds its own reference to the buffer, so the pages stay
 * allocated even if every process detaches it. It also holds a reference to
 * the context. It does not move or copy data, change caching, or start any
 * hardware, and it is not a permission check: callers are kernel code that has
 * already decided this device may use this buffer.
 *
 * Only buffers SHM owns are accepted. Every page in the range must already be
 * allocated (for example with SHM_COMMIT); this never allocates buffer pages
 * as a side effect.
 *
 * StreamingBounce permits zeroed bounce pages when the device cannot use the
 * buffer directly. StreamingDirect, Persistent and Shared require direct access.
 * Sync calls copy between the buffer and bounce pages. A bounce copy is needed
 * when part of the range is out of the device's reach, or when a
 * non-coherent device writes and the range does not start and end on a CPU
 * cache line boundary (cache work would otherwise erase neighbouring bytes).
 * Uncached pool buffers need no cache-line isolation. Ordinary SHM is cached.
 * The segments then describe the bounce pages.
 *
 * Mapping rules:
 * - Streaming mappings use SHMDeviceSyncForDevice before each transfer and
 *   SHMDeviceSyncForCpu after completion.
 * - Persistent mappings use SHMDeviceBeginUse/SHMDeviceEndUse. The first begin
 *   and last end perform cache handoffs for the whole range.
 * - Range checks round to cache lines. Overlapping writes conflict; readers
 *   may coexist. Callers still order conflicting jobs within a mapping.
 * - Create separate CPU sessions with SHMAccessCreate and bracket their ranges
 *   with SHMAccessBegin/SHMAccessEnd and CPU barriers. Serialize mapping calls.
 * - Shared mappings require pool storage that is uncached or used by a coherent
 *   device. SHMDeviceAllocate creates these mappings with new pool storage.
 *
 * No cache handoff occurs at creation.
 *
 * @param context Context held by the caller throughout this call.
 * @param shmID Buffer to use.
 * @param offset First byte of the range, counted from the start of the buffer.
 * @param length Nonzero number of bytes; the range must fit in the buffer.
 * @param direction Which way data will move for this mapping.
 * @param kind Device backing and synchronization mode.
 * @param mappingOut Empty owning slot; receives the mapping on success.
 * @return OS_EOK; OS_EINVALPARAMS for NULL arguments, a bad range or an
 *         unknown direction or kind; OS_EBUSY for an occupied slot; OS_ENOENT for an
 *         unknown buffer or bytes with no device address, even through
 *         bounce pages; OS_ENOTSUPPORTED for an exported buffer or unsafe
 *         direct cache-line sharing or incompatible shared storage;
 *         OS_EINCOMPLETE if a page in the range was never allocated;
 *         OS_EOVERFLOW if the context cannot take another reference; OS_EOOM.
 *         Failure leaves the slot unchanged and takes no references.
 */
oserr_t
SHMDeviceMap(
    _In_    struct SHMDeviceContext*  context,
    _In_    uuid_t                    shmID,
    _In_    size_t                    offset,
    _In_    size_t                    length,
    _In_    enum DMADirection         direction,
    _In_    enum SHMDeviceMappingKind kind,
    _InOut_ struct SHMDeviceMapping** mappingOut);

/**
 * @brief Count a device use before submitting it to hardware.
 * Wait behavior is defined by SHMWaitMode.
 * OS_EFORKED leaves completion to the async child.
 *
 * Preconditions:
 * - Use a persistent mapping with CPU access excluded.
 * - Serialize calls on the mapping.
 * - Keep the mapping valid until any async child finishes.
 *
 * Errors:
 * - OS_EINVALPARAMS for a NULL mapping or a different mapping kind.
 * - OS_EOVERFLOW if the use count cannot increase.
 * - OS_EBUSY if an active use batch must drain for a conflicting queued request.
 * - Shared wait errors pass through unchanged.
 *
 * @param mapping Persistent mapping to use.
 * @param wait Wait options, or NULL for nonblocking access.
 * @return OS_EOK, OS_EFORKED or an error described above.
 */
oserr_t
SHMDeviceBeginUse(
    _In_ struct SHMDeviceMapping*    mapping,
    _In_ const struct SHMAccessWait* wait);

/**
 * @brief End one use after completion, confirmed stop, or unsubmitted rollback.
 * @param mapping Persistent mapping with an active use. Serialize calls. CPU
 *                access remains excluded until the final use has ended.
 * @return OS_EOK or OS_EINVALPARAMS for a different kind or no active uses.
 */
oserr_t
SHMDeviceEndUse(
    _In_ struct SHMDeviceMapping* mapping);

/**
 * @brief Allocate memory a device keeps using alongside the CPU.
 *
 * Meant for long-lived structures such as rings and contexts, which both sides
 * read and write all the time. The memory is a new zeroed SHM buffer
 * using the device pool's cache policy (uncached on ARM64). Neither side needs
 * streaming sync calls to see the other's writes. The
 * mapping is bidirectional and never bounces, because a copy could never stay
 * up to date. Its segments cover the whole allocation, rounded up to pages.
 *
 * Pages come from the reserved device pool as a continuous physical run. The
 * pool enforces physical alignment and boundaries, and the translated device
 * segments are checked as well. An absent/full pool or an incompatible device
 * view fails; there is no fallback to ordinary memory.
 *
 * The buffer is created in, and mapped into, the calling process. Release with
 * SHMDeviceUnmap on the mapping and SHMDetach on the buffer, in that order.
 *
 * @param context Context held by the caller throughout this call.
 * @param requirements What the device needs; see DMARequirements.
 * @param bufferOut Receives the buffer handle, including the CPU pointer.
 * @param mappingOut Empty owning slot; receives the mapping on success.
 * @return OS_EOK; OS_EINVALPARAMS for NULL arguments or requirements no
 *         memory could meet; OS_EBUSY for an occupied slot; OS_ENOENT if the
 *         device cannot reach the memory; OS_ENOTSUPPORTED if the memory did
 *         not meet the requirements; or an error from creating the buffer.
 *         Failure leaves nothing allocated and the slot unchanged.
 */
oserr_t
SHMDeviceAllocate(
    _In_    struct SHMDeviceContext*      context,
    _In_    const struct DMARequirements* requirements,
    _Out_   SHMHandle_t*                  bufferOut,
    _InOut_ struct SHMDeviceMapping**     mappingOut);

/**
 * @brief Hand a mapping to the device before starting a transfer.
 *
 * Makes prior CPU writes visible to the device and starts a device reservation.
 * Bounce mappings first copy the range to bounce pages if the device reads it.
 * Cached noncoherent storage is cleaned before device access. Device writes
 * also require invalidation. Uncached pool storage skips cache maintenance but still
 * requires ordering, bounce copies when applicable, and access coordination.
 *
 * Wait behavior is defined by SHMWaitMode.
 * OS_EFORKED leaves completion to the async child.
 *
 * Preconditions:
 * - Use an idle streaming mapping and a nonempty range within it.
 * - Serialize calls on the mapping.
 * - Keep the mapping valid until any async child finishes.
 * - End conflicting CPU sessions before begin. Keep the mapping valid until
 *   SHMDeviceSyncForCpu succeeds, then begin a CPU session before accessing data.
 *
 * Errors:
 * - OS_EINVALPARAMS for NULL, a non-streaming mapping or an invalid range.
 * - OS_EBUSY if the mapping is already active.
 * - Shared wait errors pass through unchanged.
 *
 * @param mapping Streaming mapping to submit.
 * @param offset First byte the device will use, counted from the mapping.
 * @param length Nonzero number of bytes; must lie inside the mapping.
 * @param wait Wait options, or NULL for nonblocking access.
 * @return OS_EOK, OS_EFORKED or an error described above.
 */
oserr_t
SHMDeviceSyncForDevice(
    _In_ struct SHMDeviceMapping*    mapping,
    _In_ size_t                      offset,
    _In_ size_t                      length,
    _In_ const struct SHMAccessWait* wait);

/**
 * @brief Take a mapping back from the device after a transfer has ended.
 *
 * Call only once the driver knows the device has finished, for example from a
 * completion event, or after stopping the device. Makes the device's writes in
 * the completed range visible to the CPU (for a non-coherent context with a
 * device that writes, by throwing away stale cached copies, and when bouncing,
 * by copying the completed bytes back into the buffer), then ends the device
 * reservation. Begin a CPU session before accessing the data. Bytes outside
 * the completed range must be treated as unchanged.
 *
 * @param mapping Streaming mapping. Shared and persistent mappings are rejected.
 *                Calls on one mapping must not overlap.
 * @param length Completed prefix length, at most the submitted length; zero permits cancellation.
 * @return OS_EOK; OS_EINVALPARAMS for NULL, a non-streaming mapping, a bad range,
 *         or a mapping the device does not own.
 */
oserr_t
SHMDeviceSyncForCpu(
    _In_ struct SHMDeviceMapping* mapping,
    _In_ size_t                   length);

/**
 * @brief Drop a mapping and the buffer and context references it held.
 *
 * Refuses queued device access, an active streaming transfer or persistent use.
 * After device completion or confirmed stop, call SHMDeviceSyncForCpu for a
 * stream or SHMDeviceEndUse for each persistent use before releasing.
 * This function does not stop hardware itself.
 *
 * @param mapping Slot cleared on success. NULL or an empty slot is harmless.
 *                Serialize all access to the mapping.
 * @return OS_EOK, or OS_EBUSY while device work is active; failure leaves the
 *         slot unchanged.
 */
oserr_t
SHMDeviceUnmap(
    _InOut_ struct SHMDeviceMapping** mapping);

/**
 * @brief Read the device segments covering the mapped range, in order.
 *
 * @param mapping Mapping owned by the caller.
 * @param countOut Receives the number of segments.
 * @return The segment list, valid until the mapping is released, or NULL for
 *         NULL arguments.
 */
const struct DMASegment*
SHMDeviceMappingSegments(
    _In_  const struct SHMDeviceMapping* mapping,
    _Out_ uint32_t*                      countOut);

/**
 * @brief Release idle mappings after the last process thread has exited.
 * Active hardware ranges move to the context quarantine list and stay pinned.
 *
 * @param mapping Slot cleared on successful release or quarantine.
 * @return OS_EOK, or the idle mapping release error.
 */
oserr_t
SHMDeviceAbandon(
    _InOut_ struct SHMDeviceMapping** mapping);

/**
 * @brief Free quarantined mappings after a confirmed device stop or reset.
 * All device access to this context must have stopped, and calls that can add
 * mappings to quarantine must be serialized with this call. Incomplete bounce
 * contents are discarded. Conflicting ranges become available again.
 *
 * @param context Context reference held by the caller until this call returns.
 */
void
SHMDeviceReleaseQuarantine(
    _In_ struct SHMDeviceContext* context);

#endif //!__SHM_DEVICE_H__
