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

#ifndef __SHM_REGION_H__
#define __SHM_REGION_H__

#include <os/types/shm_region.h>

struct SHMRegionCpuSession;

/**
 * @brief Register a kernel snapshot of sorted regions. Publication is all or none.
 * @param shm Ordinary SHM_REGIONS allocation with committed pages.
 * @param ranges Kernel array of validated size.
 * @param count Nonzero number of ranges, at most SHM_REGION_LIMIT.
 * @return OS_EOK or a validation, permission, allocation or duplicate-layout error.
 */
oserr_t
SHMRegionRegister(
    _In_ uuid_t                       shm,
    _In_ const struct SHMRegionRange* ranges,
    _In_ unsigned int                 count);

/**
 * @brief Prepare fixed tokens and a reference to one canonical region.
 * @param shm Ordinary or restricted SHM reference.
 * @param region Layout index.
 * @param capacity Number of token slots.
 * @param sessionOut Receives the session on success.
 * @param identityOut Receives canonical identity on success.
 * @return OS_EOK or a setup error.
 */
oserr_t
SHMRegionCpuCreate(
    _In_  uuid_t                       shm,
    _In_  unsigned int                 region,
    _In_  unsigned int                 capacity,
    _Out_ struct SHMRegionCpuSession** sessionOut,
    _Out_ OSBufferIdentity_t*          identityOut);

/**
 * @brief Admit an exact CPU token without waiting or allocating.
 * @param session Serialized prepared session.
 * @param slot Token slot index.
 * @param generation Previous successful generation plus one. Zero is invalid.
 * @param access Read or read/write access.
 * @return OS_EOK, OS_EBUSY, OS_EOVERFLOW or a validation/permission error.
 */
oserr_t
SHMRegionCpuBegin(
    _In_ struct SHMRegionCpuSession* session,
    _In_ unsigned int                slot,
    _In_ unsigned int                generation,
    _In_ enum SHMCpuAccess           access);

/**
 * @brief Release one exact token after CPU accesses finish.
 * @param session Serialized prepared session.
 * @param slot Active token slot.
 * @param generation Generation used by successful begin.
 * @return OS_EOK or OS_ENOENT/OS_EINVALPARAMS for a stale or invalid token.
 */
oserr_t
SHMRegionCpuEnd(
    _In_ struct SHMRegionCpuSession* session,
    _In_ unsigned int                slot,
    _In_ unsigned int                generation);

/**
 * @brief Destroy a session. Abandon is valid only after all group threads stop.
 * @param session Cleared on success.
 * @param abandon End remaining CPU accesses during process cleanup.
 * @return OS_EOK or OS_EBUSY if normal close finds active tokens.
 */
oserr_t
SHMRegionCpuDestroy(
    _InOut_ struct SHMRegionCpuSession** session,
    _In_    bool                         abandon);

#endif //!__SHM_REGION_H__
