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
#include <dma/address.h>
#include <mutex.h>
#include <stdatomic.h>

/**
 * @brief Device address rules, cache policy and mappings waiting for cleanup.
 *
 * CachePolicy, AddressLimit and AddressSpace are fixed after creation.
 * Quarantine holds mappings that need a confirmed device stop or another
 * release attempt. QuarantineLock protects that list.
 */
struct SHMDeviceContext {
    _Atomic(unsigned int)     References;
    Mutex_t                   QuarantineLock;
    struct SHMDeviceMapping*  Quarantine;
    enum SHMDeviceCachePolicy CachePolicy;
    uint64_t                  AddressLimit;
    struct DMAAddressSpace*   AddressSpace;
};

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
oserr_t
SHMDeviceContextWalkSegments(
    _In_  const struct SHMDeviceContext*   context,
    _In_  const SHMSG_t*                   extents,
    _In_  int                              extentCount,
    _In_  const struct DMAMapRequirements* requirements,
    _Out_ struct DMASegment*               segmentsOut,
    _Out_ uint32_t*                        countOut);

#endif //!__MS_SHM_DEVICE_PRIVATE_H__
