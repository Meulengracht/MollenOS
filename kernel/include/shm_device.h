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

// Bound copying and validation work. This accepts all 64 ranges currently
// produced by deviced without making kernel code depend on firmware headers.
#define SHM_DEVICE_MAX_RANGES 64

enum SHMDeviceCachePolicy {
    // An unknown policy cannot be used to create a context.
    SHMDeviceCacheUnknown,
    // CPU caches need explicit handling before memory changes hands.
    SHMDeviceCacheNonCoherent,
    // The complete device path provides hardware cache coherency.
    SHMDeviceCacheCoherent
};

/**
 * @brief Describe one device address block and its matching physical block.
 *
 * These are address rules, not allocated pages. Length counts bytes from both
 * bases. Device blocks must not overlap; physical blocks may overlap because
 * hardware can expose several device addresses for the same physical memory.
 */
struct SHMDeviceRange {
    uint64_t PhysicalBase;
    uint64_t DeviceBase;
    uint64_t Length;
};

// The context owns an immutable copy of the ranges and cache policy. Keeping
// it private prevents a caller from changing address rules while others use it.
struct SHMDeviceContext;

/**
 * @brief Copy validated address rules so SHM can keep them beyond a request.
 *
 * This kernel-only operation takes stable kernel memory, never user pointers.
 * It does not register a device, retain a deviced lease, reserve RAM, configure
 * caches or enable hardware. The future registration caller must separately
 * keep the actual device path alive and exclude reset while using these rules.
 *
 * @param ranges Unsorted or sorted ranges, stable throughout this call.
 * @param count Number of ranges; must be 1 through SHM_DEVICE_MAX_RANGES.
 * @param cachePolicy Known coherency policy for the complete device path.
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
    _InOut_ struct SHMDeviceContext**    contextOut);

/**
 * @brief Take another reference so an independent owner can keep the rules.
 *
 * @param context Context protected by an existing reference throughout this
 *                call. Success permits copying its pointer to a new owning
 *                slot. Acquiring from an unprotected pointer is not safe.
 * @return OS_EOK, OS_EINVALPARAMS for NULL, or OS_EOVERFLOW if the reference
 *         count cannot grow. Failure does not acquire a reference.
 */
oserr_t
SHMDeviceContextRetain(
    _In_ struct SHMDeviceContext* context);

/**
 * @brief Drop one owner and free the copied rules after the last owner leaves.
 *
 * @param context Owning slot, cleared by this call. NULL or an empty slot is
 *                harmless. Serialize access to the same slot; distinct owners
 *                may release concurrently. This releases no DMA buffers and
 *                does not stop hardware or release a deviced lease.
 */
void
SHMDeviceContextRelease(
    _InOut_ struct SHMDeviceContext** context);

/**
 * @brief Read the saved policy so future SHM mappings can choose cache handling.
 *
 * @param context Context held by the caller for the duration of this call.
 * @return The copied policy, or SHMDeviceCacheUnknown for NULL. Reading it does
 *         not perform cache maintenance or make memory visible to hardware.
 */
enum SHMDeviceCachePolicy
SHMDeviceContextGetCachePolicy(
    _In_ const struct SHMDeviceContext* context);

/**
 * @brief Find a device address for an entire physical buffer, not just its start.
 *
 * The buffer must fit inside one range. Adjacent ranges are not joined; future
 * segment-building code must split at their boundaries. Among physical aliases
 * that fit the full buffer and device limit, choose the lowest device address,
 * independently of input order. No RAM ownership or installed-memory check is
 * made, so a successful translation alone never authorizes a DMA transfer.
 *
 * @param context Context held by the caller throughout this call.
 * @param physicalBase First physical byte of the proposed buffer.
 * @param length Nonzero buffer length in bytes.
 * @param deviceLimit Largest address the controller may use, inclusive.
 *                    UINT64_MAX means no additional controller limit.
 * @param deviceAddressOut Receives the device address only on success.
 * @return OS_EOK; OS_EINVALPARAMS for NULL, zero length or physical overflow;
 *         OS_ENOENT if no single range covers the buffer within deviceLimit.
 *         Failure leaves the output unchanged.
 */
oserr_t
SHMDeviceContextTranslate(
    _In_  const struct SHMDeviceContext* context,
    _In_  uint64_t                       physicalBase,
    _In_  uint64_t                       length,
    _In_  uint64_t                       deviceLimit,
    _Out_ uint64_t*                      deviceAddressOut);

#endif //!__SHM_DEVICE_H__
