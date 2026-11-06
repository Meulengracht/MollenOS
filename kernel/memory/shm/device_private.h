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
#include <mutex.h>
#include <stdatomic.h>

/**
 * @brief Device address rules, reference count and mappings awaiting a hardware stop.
 * CachePolicy, AddressLimit, Count and Ranges are fixed after creation.
 * QuarantineLock protects the list managed by the mapping lifecycle code.
 */
struct SHMDeviceContext {
    _Atomic(unsigned int)     References;
    Mutex_t                   QuarantineLock;
    struct SHMDeviceMapping*  Quarantine;
    enum SHMDeviceCachePolicy CachePolicy;
    uint64_t                  AddressLimit;
    uint32_t                  Count;
    struct SHMDeviceRange     Ranges[];
};

/**
 * @brief Translates physical extents and counts or writes the joined device segments.
 *
 * The caller must hold a context reference throughout the call. Extents must
 * contain at least one entry. The mapping caller must validate requirements
 * before calling.
 * Before writing, count with the same extents and requirements and provide
 * room for every segment. Keep those inputs unchanged between the two calls.
 *
 * @param context Valid context with fixed address rules.
 * @param extents Physical bytes in buffer order.
 * @param extentCount Number of extents, at least one.
 * @param requirements Optional alignment constraints on completed segments.
 * @param segmentsOut NULL to count only, otherwise room for every segment.
 * @param countOut Receives the number of segments on success.
 * @return OS_EOK on success. OS_EINVALPARAMS for an empty or wrapping extent.
 *         OS_ENOENT for unreachable bytes. OS_ENOTSUPPORTED for unmet alignment
 *         or a segment count that does not fit. A failed write may be partial.
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
