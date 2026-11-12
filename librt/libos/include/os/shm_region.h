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

#ifndef __OS_SHM_REGION_H__
#define __OS_SHM_REGION_H__

#include <os/types/shm_region.h>
#include <os/types/handle.h>

_CODE_BEGIN

/**
 * A reusable CPU token. Zero before first use and set Slot to a distinct session
 * slot. Keep Generation across uses. Do not copy a token or use its slot through
 * another token. Calls on the session and its tokens must be serialized.
 */
struct SHMRegionCpuToken {
    unsigned int Slot;
    unsigned int Generation;
    bool         Active;
};

/**
 * @brief Register the fixed region layout of a committed SHM_REGIONS allocation.
 * Only an ordinary writable SHM handle can register. Registration is permanent
 * until the allocation is destroyed. Layout indices are array indices, generation
 * is one. Regions must be sorted, nonempty, disjoint and cache-line aligned.
 * @param buffer Ordinary SHM handle.
 * @param ranges Array of logical ranges relative to the allocation.
 * @param count Number of regions, between one and SHM_REGION_LIMIT.
 * @return OS_EOK, OS_EBUSY if already registered, or a validation/allocation error.
 */
__EXTERN oserr_t
SHMRegionsRegister(
    _In_ const OSHandle_t*            buffer,
    _In_ const struct SHMRegionRange* ranges,
    _In_ unsigned int                count);

/**
 * @brief Prepare CPU tokens for one canonical region, outside job submission.
 * A restricted view must cover the entire region. The session keeps the backing
 * allocated and uses the creating security context. Open separate sessions in
 * other processes. No CPU mapping is created; map and commit before submission.
 * @param buffer Ordinary or restricted SHM handle with read access.
 * @param region Index in the registered layout.
 * @param capacity Token slots to allocate, up to SHM_REGION_TOKEN_LIMIT.
 * @param session Zeroed output. Unchanged on failure.
 * @return OS_EOK or a range, permission, resource or allocation error.
 */
__EXTERN oserr_t
SHMRegionSessionOpen(
    _In_    const OSHandle_t*         buffer,
    _In_    unsigned int             region,
    _In_    unsigned int             capacity,
    _InOut_ struct SHMRegionSession* session);

/**
 * @brief Begin CPU access after userspace dependencies have completed.
 * Read permits other readers; write excludes every other access. No allocation,
 * dependency wait or service IPC occurs. Admission does not trap pointer access.
 * @param session Prepared session. Serialize calls on this session.
 * @param token Idle token assigned to a slot in this session. Unchanged on failure.
 * @param access Read or read/write access within the session's permitted scope.
 * @return OS_EOK, OS_EBUSY, OS_EOVERFLOW or a validation/permission error.
 */
__EXTERN oserr_t
SHMRegionTryBeginCpu(
    _In_    const struct SHMRegionSession* session,
    _InOut_ struct SHMRegionCpuToken*      token,
    _In_    enum SHMCpuAccess              access);

/**
 * @brief End an exact CPU access before publishing its userspace completion.
 * @param session Session used for begin.
 * @param token Active token. On failure it remains active for retry.
 * @return OS_EOK or a resource, stale-token or validation error.
 */
__EXTERN oserr_t
SHMRegionEndCpu(
    _In_    const struct SHMRegionSession* session,
    _InOut_ struct SHMRegionCpuToken*      token);

/**
 * @brief Close a session after all its tokens have ended.
 * @param session Cleared on success. Empty sessions are unchanged.
 * @return OS_EOK, OS_EBUSY for active tokens, or a resource error.
 */
__EXTERN oserr_t
SHMRegionSessionClose(
    _InOut_ struct SHMRegionSession* session);

_CODE_END
#endif //!__OS_SHM_REGION_H__
