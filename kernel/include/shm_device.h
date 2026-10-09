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

// This accepts all 64 ranges produced by deviced without 
// making the kernel aware of firmware-specific details.
#define SHM_DEVICE_MAX_RANGES 64

enum SHMDeviceCachePolicy {
    // An unknown policy cannot be used to create a context.
    SHMDeviceCacheUnknown,
    // CPU caches need explicit handling before memory changes hands.
    SHMDeviceCacheNonCoherent,
    // The complete device path provides hardware cache coherency.
    SHMDeviceCacheCoherent
};

// Which way data moves during a transfer. Later cache handling and bounce
// buffers use it to skip work that cannot matter for that direction.
enum SHMDeviceDirection {
    // The device only reads the memory, e.g. a USB OUT transfer.
    SHMDeviceToDevice,
    // The device only writes the memory, e.g. a USB IN transfer.
    SHMDeviceFromDevice,
    // The device both reads and writes, e.g. a controller's own structures.
    SHMDeviceBidirectional
};

/**
 * These are address rules, not allocated pages. Length counts bytes from both
 * bases. Device blocks must not overlap; physical blocks may overlap because
 * hardware can expose several device addresses for the same physical memory.
 */
struct SHMDeviceRange {
    uint64_t PhysicalBase;
    uint64_t DeviceBase;
    uint64_t Length;
};

/**
 * Unlike SHMSG_t, which holds CPU physical addresses in a pointer-sized type,
 * this always uses 64 bits because device addresses can exceed 4 GiB even on
 * 32-bit kernels, and they need not equal the physical address.
 */
struct SHMDeviceSegment {
    uint64_t Address;
    uint64_t Length;
};

/**
 * What a device needs from memory it keeps using, such as a controller's rings.
 * Zero for Alignment or Boundary means no requirement.
 */
struct SHMDeviceRequirements {
    // Number of bytes needed; the allocation is rounded up to whole pages.
    size_t   Length;
    // The device address of the first byte must be a multiple of this power of two.
    uint64_t Alignment;
    // No segment may cross a multiple of this power of two, e.g. 64 KiB for xHCI.
    uint64_t Boundary;
    // The device must see the whole allocation as a single segment.
    bool     Contiguous;
};

// Opaque types
struct SHMDeviceContext;
struct SHMDeviceMapping;

/**
 * @brief Copy validated address rules so SHM can keep them beyond a request.
 *
 * This call must only use kernel memory, never user pointers.
 * It does not register a device, retain a deviced lease, reserve RAM, configure
 * caches or enable hardware. The future registration caller must separately
 * keep the actual device path alive and exclude reset while using these rules.
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
    _Out_   struct SHMDeviceSegment*       segmentsOut);

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
 * When the device cannot use the buffer's own pages, the mapping uses zeroed
 * bounce pages instead, and the sync calls copy data between them. That
 * happens when part of the range is out of the device's reach, or when a
 * non-coherent device writes and the range does not start and end on a CPU
 * cache line boundary (cache work would otherwise erase neighbouring bytes).
 * The segments then describe the bounce pages.
 *
 * The new mapping starts out owned by the CPU. Hand it to the device with
 * SHMDeviceSyncForDevice before starting a transfer.
 *
 * @param context Context held by the caller throughout this call.
 * @param shmID Buffer to use.
 * @param offset First byte of the range, counted from the start of the buffer.
 * @param length Nonzero number of bytes; the range must fit in the buffer.
 * @param direction Which way data will move for this mapping.
 * @param mappingOut Empty owning slot; receives the mapping on success.
 * @return OS_EOK; OS_EINVALPARAMS for NULL arguments, a bad range or an
 *         unknown direction; OS_EBUSY for an occupied slot; OS_ENOENT for an
 *         unknown buffer or bytes with no device address, even through
 *         bounce pages; OS_ENOTSUPPORTED for an exported buffer;
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
    _In_    enum SHMDeviceDirection   direction,
    _InOut_ struct SHMDeviceMapping** mappingOut);

/**
 * @brief Allocate memory a device keeps using alongside the CPU.
 *
 * Meant for long-lived structures such as rings and contexts, which both sides
 * read and write all the time. The memory is a new zeroed, uncached SHM_DEVICE
 * buffer, so neither side needs sync calls to see the other's writes. The
 * mapping is bidirectional and never bounces, because a copy could never stay
 * up to date. Its segments cover the whole allocation, rounded up to pages.
 *
 * Until a reserved device memory pool exists, pages come from ordinary low
 * memory and the requirements are checked afterwards, not guaranteed. Requests
 * for several contiguous pages or for large alignments may therefore fail
 * with OS_ENOTSUPPORTED even though memory is available.
 *
 * The buffer is created in, and mapped into, the calling process. Release with
 * SHMDeviceUnmap on the mapping and SHMDetach on the buffer, in that order.
 *
 * @param context Context held by the caller throughout this call.
 * @param requirements What the device needs; see SHMDeviceRequirements.
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
    _In_    struct SHMDeviceContext*            context,
    _In_    const struct SHMDeviceRequirements* requirements,
    _Out_   SHMHandle_t*                        bufferOut,
    _InOut_ struct SHMDeviceMapping**           mappingOut);

/**
 * @brief Hand a mapping to the device before starting a transfer.
 *
 * Makes the CPU's writes to the range visible to the device, then marks the
 * whole mapping as device-owned. When bouncing, the range is first copied to
 * the bounce pages (unless the device only writes). For a non-coherent
 * context, cached data in the range is written back, and for a device that
 * writes, also thrown away.
 * Until SHMDeviceSyncForCpu, the CPU must not touch the mapping's bytes, and
 * the mapping cannot be released.
 *
 * @param mapping Mapping owned by the caller. Calls on one mapping must not
 *                overlap.
 * @param offset First byte the device will use, counted from the mapping.
 * @param length Nonzero number of bytes; must lie inside the mapping.
 * @return OS_EOK; OS_EINVALPARAMS for NULL or a bad range; OS_EBUSY if the
 *         device already owns the mapping.
 */
oserr_t
SHMDeviceSyncForDevice(
    _In_ struct SHMDeviceMapping* mapping,
    _In_ size_t                   offset,
    _In_ size_t                   length);

/**
 * @brief Take a mapping back from the device after a transfer has ended.
 *
 * Call only once the driver knows the device has finished, for example from a
 * completion event, or after stopping the device. Makes the device's writes in
 * the completed range visible to the CPU (for a non-coherent context with a
 * device that writes, by throwing away stale cached copies, and when bouncing,
 * by copying the completed bytes back into the buffer), then returns the
 * whole mapping to the CPU. Bytes outside the completed range must be treated
 * as unchanged.
 *
 * @param mapping Mapping owned by the caller. Calls on one mapping must not
 *                overlap.
 * @param offset First completed byte, counted from the mapping.
 * @param length Completed bytes; zero is allowed when nothing was transferred.
 * @return OS_EOK; OS_EINVALPARAMS for NULL, a bad range, or a mapping the
 *         device does not own.
 */
oserr_t
SHMDeviceSyncForCpu(
    _In_ struct SHMDeviceMapping* mapping,
    _In_ size_t                   offset,
    _In_ size_t                   length);

/**
 * @brief Drop a mapping and the buffer and context references it held.
 *
 * Refuses while the device owns the mapping, because releasing could free
 * pages the device is still using. Take it back with SHMDeviceSyncForCpu once
 * the device has stopped. This does not stop hardware itself.
 *
 * @param mapping Owning slot, cleared on success. NULL or an empty slot is
 *                harmless. Serialize access to the same slot.
 * @return OS_EOK, or OS_EBUSY if the device owns the mapping; the slot is then
 *         left unchanged.
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
const struct SHMDeviceSegment*
SHMDeviceMappingSegments(
    _In_  const struct SHMDeviceMapping* mapping,
    _Out_ uint32_t*                      countOut);

#endif //!__SHM_DEVICE_H__
