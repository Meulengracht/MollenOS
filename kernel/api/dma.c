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

#include <os/types/syscall.h>

// SHMDevice* trusts kernel callers. Until device grants and process-exit recovery
// exist, every DMA syscall is unavailable. None reads user pointers, writes any
// output, acquires resources or treats a global device/SHM ID as authorization.

oserr_t
ScDmaOpen(
    _In_  uuid_t          deviceGrant,
    _In_  const uint64_t* addressLimit,
    _Out_ uuid_t*         contextOut)
{
    // Opening requires an authorized device association, not a guessed ID.
    // The limit is pointer-based to preserve all 64 bits on 32-bit kernels.
    (void)deviceGrant;
    (void)addressLimit;
    (void)contextOut;
    return OS_ENOTSUPPORTED;
}

oserr_t
ScDmaAllocate(
    _In_  uuid_t                              context,
    _In_  const struct SHMDeviceRequirements* requirements,
    _Out_ struct OSDmaAllocationResult*       resultOut)
{
    // Allocation cannot start until its context and hardware lifetime are checked.
    (void)context;
    (void)requirements;
    (void)resultOut;
    return OS_ENOTSUPPORTED;
}

oserr_t
ScDmaMapShm(
    _In_  uuid_t                           context,
    _In_  uuid_t                           shm,
    _In_  const struct OSDmaMapParameters* parameters,
    _Out_ struct OSDmaMappingResult*       resultOut)
{
    // Both device authority and SHM access need kernel checks before retaining pages.
    (void)context;
    (void)shm;
    (void)parameters;
    (void)resultOut;
    return OS_ENOTSUPPORTED;
}

oserr_t
ScDmaGetSegments(
    _In_  uuid_t                   mapping,
    _Out_ struct SHMDeviceSegment* entries,
    _In_  uint32_t                 capacity,
    _Out_ uint32_t*                countOut)
{
    // Never expose addresses through a fabricated mapping handle while gated.
    (void)mapping;
    (void)entries;
    (void)capacity;
    (void)countOut;
    return OS_ENOTSUPPORTED;
}

oserr_t
ScDmaSyncForDevice(
    _In_ uuid_t mapping,
    _In_ size_t offset,
    _In_ size_t length)
{
    // A future implementation must check the owner and serialize this handoff.
    (void)mapping;
    (void)offset;
    (void)length;
    return OS_ENOTSUPPORTED;
}

oserr_t
ScDmaSyncForCpu(
    _In_ uuid_t mapping,
    _In_ size_t offset,
    _In_ size_t length)
{
    // Reclaim requires a checked mapping and the device completion contract.
    (void)mapping;
    (void)offset;
    (void)length;
    return OS_ENOTSUPPORTED;
}

oserr_t
ScDmaUnmap(
    _In_ uuid_t mapping)
{
    // A caller must not consume another owner's reference through a raw ID.
    (void)mapping;
    return OS_ENOTSUPPORTED;
}

oserr_t
ScDmaClose(
    _In_ uuid_t context)
{
    // Do not fall back to global handle destruction when device grants are absent.
    (void)context;
    return OS_ENOTSUPPORTED;
}
